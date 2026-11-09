#include "Compress.h"

#include <qpdf/Constants.h>
#include <qpdf/Pl_Flate.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace dp::tools {

namespace {

struct CancelledError {};

// 3 x 3 matrix as PDF writes it: [a b c d e f]
struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    // this applied first, then `o`
    Matrix then(const Matrix& o) const
    {
        return {a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d, e * o.a + f * o.c + o.e, e * o.b + f * o.d + o.f};
    }
};

double number(const QPDFObjectHandle& h) { return h.isNumber() ? h.getNumericValue() : 0.0; }

// The largest size (points) at which each image is shown in the page contents, found by walking the content streams.
struct Usage {
    double width = 0;
    double height = 0;
};
using UsageMap = std::map<QPDFObjGen, Usage>;

class Scanner : public QPDFObjectHandle::ParserCallbacks {
public:
    Scanner(QPDFObjectHandle resources, Matrix start, UsageMap& usage, int depth, std::set<QPDFObjGen>& activeForms)
        : m_resources(std::move(resources)), m_ctm(start), m_usage(usage), m_depth(depth), m_forms(activeForms)
    {
    }
    void handleObject(QPDFObjectHandle obj) override
    {
        if (!obj.isOperator()) {
            m_operands.push_back(obj);
            if (m_operands.size() > 64)
                m_operands.erase(m_operands.begin());
            return;
        }
        const std::string op = obj.getOperatorValue();
        if (op == "q") {
            m_stack.push_back(m_ctm);
        } else if (op == "Q") {
            if (!m_stack.empty()) {
                m_ctm = m_stack.back();
                m_stack.pop_back();
            }
        } else if (op == "cm" && m_operands.size() >= 6) {
            const size_t n = m_operands.size();
            const Matrix m{number(m_operands[n - 6]), number(m_operands[n - 5]), number(m_operands[n - 4]), number(m_operands[n - 3]),
                           number(m_operands[n - 2]), number(m_operands[n - 1])};
            m_ctm = m.then(m_ctm);
        } else if (op == "Do" && !m_operands.empty() && m_operands.back().isName()) {
            use(m_operands.back().getName());
        }
        m_operands.clear();
    }
    void handleEOF() override {}

private:
    void use(const std::string& name)
    {
        if (!m_resources.isDictionary())
            return;
        QPDFObjectHandle xobjects = m_resources.getKey("/XObject");
        if (!xobjects.isDictionary())
            return;
        QPDFObjectHandle xo = xobjects.getKey(name);
        if (!xo.isStream())
            return;
        QPDFObjectHandle subtype = xo.getDict().getKey("/Subtype");
        if (subtype.isName() && subtype.getName() == "/Image") {
            Usage& u = m_usage[xo.getObjGen()];
            u.width = std::max(u.width, std::hypot(m_ctm.a, m_ctm.b));
            u.height = std::max(u.height, std::hypot(m_ctm.c, m_ctm.d));
        } else if (subtype.isName() && subtype.getName() == "/Form" && m_depth < 4 && !m_forms.count(xo.getObjGen())) {
            Matrix fm;
            QPDFObjectHandle mat = xo.getDict().getKey("/Matrix");
            if (mat.isArray() && mat.getArrayNItems() == 6)
                fm = {number(mat.getArrayItem(0)), number(mat.getArrayItem(1)), number(mat.getArrayItem(2)),
                      number(mat.getArrayItem(3)), number(mat.getArrayItem(4)), number(mat.getArrayItem(5))};
            QPDFObjectHandle res = xo.getDict().getKey("/Resources");
            m_forms.insert(xo.getObjGen());
            Scanner inner(res.isDictionary() ? res : m_resources, fm.then(m_ctm), m_usage, m_depth + 1, m_forms);
            try {
                QPDFObjectHandle::parseContentStream(xo, &inner);
            } catch (const std::exception&) {
                // a broken form: its images are simply not measured (they are then not reduced)
            }
            m_forms.erase(xo.getObjGen());
        }
    }

    QPDFObjectHandle m_resources;
    Matrix m_ctm;
    UsageMap& m_usage;
    int m_depth;
    std::set<QPDFObjGen>& m_forms;
    std::vector<QPDFObjectHandle> m_operands;
    std::vector<Matrix> m_stack;
};

// components of the colour space for the image kinds we handle (3 = RGB, 1 = grey), 0 = not handled
int componentsOf(const QPDFObjectHandle& cs)
{
    if (cs.isName()) {
        if (cs.getName() == "/DeviceRGB")
            return 3;
        if (cs.getName() == "/DeviceGray")
            return 1;
        return 0;
    }
    if (cs.isArray() && cs.getArrayNItems() == 2 && cs.getArrayItem(0).isName() && cs.getArrayItem(0).getName() == "/ICCBased") {
        QPDFObjectHandle profile = cs.getArrayItem(1);
        if (profile.isStream()) {
            QPDFObjectHandle n = profile.getDict().getKey("/N");
            if (n.isInteger() && (n.getIntValue() == 1 || n.getIntValue() == 3))
                return int(n.getIntValue());
        }
    }
    return 0;
}

// the filter names of a stream
std::vector<std::string> filtersOf(const QPDFObjectHandle& dict)
{
    std::vector<std::string> out;
    QPDFObjectHandle f = dict.getKey("/Filter");
    if (f.isName())
        out.push_back(f.getName());
    else if (f.isArray())
        for (int i = 0; i < f.getArrayNItems(); ++i)
            if (f.getArrayItem(i).isName())
                out.push_back(f.getArrayItem(i).getName());
    return out;
}

// many different colours = a photograph (worth storing as JPEG); few = a drawing, a screenshot or a scan of text (JPEG would blur the edges)
bool photoLike(const QImage& img)
{
    QSet<QRgb> colours;
    const int step = std::max(1, int(std::sqrt(double(img.width()) * img.height() / 6000.0)));
    for (int y = 0; y < img.height(); y += step)
        for (int x = 0; x < img.width(); x += step) {
            colours.insert(img.pixel(x, y) & 0xF8F8F8);           // 5 bits per channel: noise does not count as variety
            if (colours.size() > 400)
                return true;
        }
    return false;
}

// the picture of an image stream (null when the format is not handled)
QImage decodeImage(QPDFObjectHandle image, int comps, int width, int height, bool& isJpeg)
{
    isJpeg = false;
    const std::vector<std::string> filters = filtersOf(image.getDict());
    if (filters.size() == 1 && filters[0] == "/DCTDecode") {
        isJpeg = true;
        std::shared_ptr<Buffer> raw = image.getRawStreamData();
        QImage img = QImage::fromData(reinterpret_cast<const uchar*>(raw->getBuffer()), int(raw->getSize()), "JPEG");
        if (img.isNull() || img.width() != width || img.height() != height)
            return {};
        return img.convertToFormat(comps == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    }
    for (const std::string& f : filters)
        if (f != "/FlateDecode" && f != "/LZWDecode" && f != "/ASCII85Decode" && f != "/ASCIIHexDecode")
            return {};
    std::shared_ptr<Buffer> data = image.getStreamData(qpdf_dl_generalized);
    if (!data || data->getSize() != size_t(width) * size_t(height) * size_t(comps))
        return {};
    QImage img(width, height, comps == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    if (img.isNull())
        return {};
    const size_t row = size_t(width) * size_t(comps);
    for (int y = 0; y < height; ++y)
        std::memcpy(img.scanLine(y), data->getBuffer() + size_t(y) * row, row);
    return img;
}

std::string rawBytes(const QImage& img, int comps)
{
    std::string out;
    out.reserve(size_t(img.width()) * size_t(img.height()) * size_t(comps));
    for (int y = 0; y < img.height(); ++y)
        out.append(reinterpret_cast<const char*>(img.constScanLine(y)), size_t(img.width()) * size_t(comps));
    return out;
}

} // namespace

CompressResult compressFile(const QString& inFile, const QString& password, const QString& outFile, const CompressOptions& options, const Progress& progress)
{
    CompressResult result;
    result.inBytes = QFileInfo(inFile).size();
    if (options.jpegQuality > 0 && !QImageWriter::supportedImageFormats().contains("jpeg")) {
        result.error = QStringLiteral("noJpeg");
        return result;
    }
    const QString tmp = outFile + QStringLiteral(".part.tmp");
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(inFile.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        result.warnings.clear();
        if (pdf.isEncrypted())
            result.warnings << QStringLiteral("encrypted");

        if (options.stripExtras) {
            pdf.getRoot().removeKey("/Metadata");
            pdf.getRoot().removeKey("/PieceInfo");
            for (QPDFPageObjectHelper page : QPDFPageDocumentHelper(pdf).getAllPages()) {
                QPDFObjectHandle p = page.getObjectHandle();
                p.removeKey("/Thumb");
                p.removeKey("/PieceInfo");
                p.removeKey("/Metadata");
            }
        }

        // images
        std::vector<QPDFObjectHandle> images;
        for (QPDFObjectHandle obj : pdf.getAllObjects())
            if (obj.isStream()) {
                QPDFObjectHandle st = obj.getDict().getKey("/Subtype");
                if (st.isName() && st.getName() == "/Image")
                    images.push_back(obj);
            }
        result.images = int(images.size());

        if ((options.imageDpi > 0 || options.jpegQuality > 0) && !images.empty()) {
            UsageMap usage;
            if (options.imageDpi > 0)
                for (QPDFPageObjectHelper page : QPDFPageDocumentHelper(pdf).getAllPages()) {
                    std::set<QPDFObjGen> forms;
                    Scanner scanner(page.getAttribute("/Resources", false), Matrix{}, usage, 0, forms);
                    try {
                        QPDFObjectHandle::parseContentStream(page.getObjectHandle().getKey("/Contents"), &scanner);
                    } catch (const std::exception&) {
                        // unreadable contents: the images of this page are not measured
                    }
                }
            const int total = int(images.size());
            for (int i = 0; i < total; ++i) {
                if (progress && !progress(i, total))
                    throw CancelledError();
                QPDFObjectHandle image = images[size_t(i)];
                QPDFObjectHandle dict = image.getDict();
                auto skip = [&] { ++result.imagesSkipped; };
                const QPDFObjectHandle bpc = dict.getKey("/BitsPerComponent");
                const QPDFObjectHandle w = dict.getKey("/Width");
                const QPDFObjectHandle h = dict.getKey("/Height");
                const int comps = componentsOf(dict.getKey("/ColorSpace"));
                if (comps == 0 || !bpc.isInteger() || bpc.getIntValue() != 8 || !w.isInteger() || !h.isInteger() || dict.hasKey("/Mask")
                    || dict.hasKey("/Decode") || (dict.getKey("/ImageMask").isBool() && dict.getKey("/ImageMask").getBoolValue())) {
                    skip();
                    continue;
                }
                const int width = int(w.getIntValue());
                const int height = int(h.getIntValue());
                const qint64 stored = qint64(image.getRawStreamData()->getSize());
                if (width < 32 || height < 32 || stored < options.minImageBytes) {
                    skip();
                    continue;
                }
                // size to store
                int newW = width;
                int newH = height;
                bool down = false;
                if (options.imageDpi > 0) {
                    if (auto it = usage.find(image.getObjGen()); it != usage.end() && it->second.width > 1 && it->second.height > 1) {
                        const double dpi = std::min(width * 72.0 / it->second.width, height * 72.0 / it->second.height);
                        if (dpi > options.imageDpi * 1.15) {
                            const double scale = options.imageDpi / dpi;
                            newW = std::max(16, int(std::lround(width * scale)));
                            newH = std::max(16, int(std::lround(height * scale)));
                            down = newW < width && newH < height;
                        }
                    }
                }
                if (!down && options.jpegQuality == 0) {
                    skip();
                    continue;
                }
                bool isJpeg = false;
                QImage img = decodeImage(image, comps, width, height, isJpeg);
                if (img.isNull()) {
                    skip();
                    continue;
                }
                if (down)
                    img = img.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                const bool toJpeg = options.jpegQuality > 0 && (isJpeg || photoLike(img));
                if (toJpeg) {
                    QByteArray jpeg;
                    QBuffer buffer(&jpeg);
                    buffer.open(QIODevice::WriteOnly);
                    QImageWriter writer(&buffer, "jpeg");
                    writer.setQuality(options.jpegQuality);
                    writer.setOptimizedWrite(true);
                    if (!writer.write(img) || jpeg.size() >= stored * 9 / 10) {       // must be at least 10 % smaller
                        skip();
                        continue;
                    }
                    image.replaceStreamData(std::string(jpeg.constData(), size_t(jpeg.size())), QPDFObjectHandle::newName("/DCTDecode"), QPDFObjectHandle::newNull());
                } else if (down && !isJpeg) {
                    // a drawing / scan that is shown smaller than it is: fewer pixels, still lossless
                    image.replaceStreamData(rawBytes(img, comps), QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
                } else {
                    skip();
                    continue;
                }
                dict.replaceKey("/Width", QPDFObjectHandle::newInteger(img.width()));
                dict.replaceKey("/Height", QPDFObjectHandle::newInteger(img.height()));
                dict.removeKey("/DecodeParms");
                if (!toJpeg)
                    dict.removeKey("/Filter");
                ++result.imagesChanged;
                if (down)
                    ++result.imagesDownsampled;
            }
        }
        if (progress && !progress(int(images.size()), std::max(1, int(images.size()))))
            throw CancelledError();

        QFile::remove(tmp);
        QPDFWriter writer(pdf, tmp.toUtf8().constData());
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setStreamDataMode(qpdf_s_compress);
        writer.setRecompressFlate(true);
        Pl_Flate::setCompressionLevel(9);
        writer.setLinearization(options.linearize);
        writer.write();
    } catch (const CancelledError&) {
        QFile::remove(tmp);
        result.cancelled = true;
        result.error = QStringLiteral("cancelled");
        return result;
    } catch (const QPDFExc& e) {
        QFile::remove(tmp);
        result.error = e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const QPDFSystemError& e) {
        QFile::remove(tmp);
        result.error = QFileInfo::exists(inFile) ? QStringLiteral("write") : QStringLiteral("file");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const std::exception& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("unknown");
        result.detail = QString::fromUtf8(e.what());
        return result;
    }

    result.outBytes = QFileInfo(tmp).size();
    if (result.outBytes <= 0 || result.outBytes >= result.inBytes) {
        QFile::remove(tmp);                              // never make a file bigger: keep the original
        result.ok = true;
        result.written = false;
        result.outBytes = result.inBytes;
        return result;
    }
    QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        result.detail = QStringLiteral("cannot create ") + outFile;
        return result;
    }
    result.ok = true;
    result.written = true;
    return result;
}

} // namespace dp::tools
