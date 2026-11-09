#include "PdfTools.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace dp::tools {

namespace {

struct CancelledError {};

// The failure of one step, as the code and detail of a WriteResult.
struct Failure {
    QString error;
    QString detail;
    QString file;
};

struct Source {
    std::unique_ptr<QPDF> pdf;
    std::vector<QPDFPageObjectHelper> pages;
    bool bookmarks = false;
    bool forms = false;
    bool encrypted = false;
};

// The opened source files of one job; they stay open (and unchanged) for as many writes as needed.
struct Context {
    std::map<QString, std::unique_ptr<Source>> sources;     // key: file, newline, password

    static QString key(const QString& file, const QString& password) { return file + QLatin1Char('\n') + password; }

    // opens the file once; null and `failure` set on error
    Source* open(const QString& file, const QString& password, Failure& failure)
    {
        const QString k = key(file, password);
        if (auto it = sources.find(k); it != sources.end())
            return it->second.get();
        auto src = std::make_unique<Source>();
        src->pdf = std::make_unique<QPDF>();
        try {
            src->pdf->setSuppressWarnings(true);
            src->pdf->processFile(file.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
            QPDFPageDocumentHelper helper(*src->pdf);
            src->pages = helper.getAllPages();
            src->encrypted = src->pdf->isEncrypted();
            src->bookmarks = QPDFOutlineDocumentHelper(*src->pdf).hasOutlines();
            src->forms = QPDFAcroFormDocumentHelper(*src->pdf).hasAcroForm();
        } catch (const QPDFExc& e) {
            failure.file = file;
            failure.error = e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format");
            failure.detail = QString::fromUtf8(e.what());
            return nullptr;
        } catch (const QPDFSystemError& e) {
            failure.file = file;
            failure.error = QStringLiteral("file");
            failure.detail = QString::fromUtf8(e.what());
            return nullptr;
        } catch (const std::exception& e) {
            failure.file = file;
            failure.error = QStringLiteral("format");
            failure.detail = QString::fromUtf8(e.what());
            return nullptr;
        }
        Source* raw = src.get();
        sources.emplace(k, std::move(src));
        return raw;
    }
};

// The annotations of a COPY of a page, made safe for the new file (the source is never touched):
//  - a link to a page that is in the new file is pointed at that page's copy; a link to any other page, or to a named destination (the name tree is
//    not carried over), is removed - it would copy the whole source page tree into the new file, or point nowhere;
//  - the back reference /P of an annotation is dropped (it would copy the source page as well).
// `copies` maps a source page to the copy that is added to the new file. Returns how many links were removed.
int cleanAnnotations(QPDFPageObjectHelper page, const std::map<QPDFObjGen, QPDFObjectHandle>& copies)
{
    QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
    if (!annots.isArray())
        return 0;
    QPDFObjectHandle filtered = QPDFObjectHandle::newArray();
    int removed = 0;
    bool changed = false;
    for (int i = 0; i < annots.getArrayNItems(); ++i) {
        QPDFObjectHandle a = annots.getArrayItem(i);
        if (!a.isDictionary()) {
            filtered.appendItem(a);
            continue;
        }
        QPDFObjectHandle item = a;
        bool copied = false;                  // `item` is already our own copy of the annotation
        bool drop = false;
        if (a.getKey("/Subtype").isNameAndEquals("/Link")) {
            QPDFObjectHandle dest = a.getKey("/Dest");
            bool inAction = false;
            if (dest.isNull()) {
                QPDFObjectHandle action = a.getKey("/A");
                if (action.isDictionary() && action.getKey("/S").isNameAndEquals("/GoTo")) {
                    dest = action.getKey("/D");
                    inAction = true;
                }
            }
            if (!dest.isNull()) {
                QPDFObjectHandle target = dest.isArray() && dest.getArrayNItems() > 0 ? dest.getArrayItem(0) : QPDFObjectHandle::newNull();
                const auto it = target.isIndirect() ? copies.find(target.getObjGen()) : copies.end();
                if (it == copies.end()) {
                    drop = true;
                } else {
                    // a copy of the annotation (and of its action) that leads to the copy of the page
                    QPDFObjectHandle newDest = dest.shallowCopy();
                    newDest.setArrayItem(0, it->second);
                    item = a.shallowCopy();
                    copied = true;
                    if (inAction) {
                        QPDFObjectHandle newAction = a.getKey("/A").shallowCopy();
                        newAction.replaceKey("/D", newDest);
                        item.replaceKey("/A", newAction);
                    } else {
                        item.replaceKey("/Dest", newDest);
                    }
                    changed = true;
                }
            }
        }
        if (drop) {
            ++removed;
            changed = true;
            continue;
        }
        if (item.hasKey("/P")) {
            if (!copied)
                item = a.shallowCopy();
            item.removeKey("/P");
            changed = true;
        }
        filtered.appendItem(item);
    }
    if (changed)
        page.getObjectHandle().replaceKey("/Annots", filtered);
    return removed;
}

int effectiveRotation(QPDFPageObjectHelper page)
{
    QPDFObjectHandle r = page.getAttribute("/Rotate", false);
    const int deg = (r.isInteger() ? int(r.getIntValue()) : 0) % 360;
    return (deg + 360) % 360;
}

// Cuts `m` points (as the page is shown) off the CropBox. false when nothing would be left.
bool applyCrop(QPDFPageObjectHelper page, const QMarginsF& m)
{
    if (m.left() == 0 && m.top() == 0 && m.right() == 0 && m.bottom() == 0)
        return true;
    const QPDFObjectHandle::Rectangle box = page.getCropBox().getArrayAsRectangle();
    double left, top, right, bottom;                       // the same margins in the page's own space
    switch (effectiveRotation(page)) {
    case 90: left = m.top(); top = m.right(); right = m.bottom(); bottom = m.left(); break;
    case 180: left = m.right(); top = m.bottom(); right = m.left(); bottom = m.top(); break;
    case 270: left = m.bottom(); top = m.left(); right = m.top(); bottom = m.right(); break;
    default: left = m.left(); top = m.top(); right = m.right(); bottom = m.bottom(); break;
    }
    const double llx = box.llx + left, lly = box.lly + bottom, urx = box.urx - right, ury = box.ury - top;
    if (urx - llx < 1.0 || ury - lly < 1.0)
        return false;
    page.getObjectHandle().replaceKey("/CropBox", QPDFObjectHandle::newFromRectangle(QPDFObjectHandle::Rectangle(llx, lly, urx, ury)));
    return true;
}

WriteResult failed(const Failure& f)
{
    WriteResult r;
    r.error = f.error;
    r.detail = f.detail;
    r.errorFile = f.file;
    return r;
}

// Builds and writes one file from the opened sources.
WriteResult writeWith(Context& ctx, const QList<PageSpec>& specs, const QString& outFile, const Progress& progress, bool overwrite)
{
    WriteResult result;
    if (specs.isEmpty()) {
        result.error = QStringLiteral("range");
        return result;
    }
    if (!overwrite && QFileInfo::exists(outFile)) {
        result.error = QStringLiteral("exists");
        return result;
    }

    // 1. open the sources, check the pages, and know which pages of each source are used
    Failure failure;
    std::map<Source*, std::set<QPDFObjGen>> keptPages;
    std::vector<Source*> specSource(size_t(specs.size()), nullptr);
    for (int i = 0; i < specs.size(); ++i) {
        const PageSpec& s = specs.at(i);
        if (s.blank)
            continue;
        Source* src = ctx.open(s.file, s.password, failure);
        if (!src)
            return failed(failure);
        if (s.page < 0 || s.page >= int(src->pages.size())) {
            result.error = QStringLiteral("range");
            result.errorFile = s.file;
            return result;
        }
        specSource[size_t(i)] = src;
        keptPages[src].insert(src->pages[size_t(s.page)].getObjectHandle().getObjGen());
    }
    std::set<Source*> seen;
    for (const auto& [src, pages] : keptPages) {
        Q_UNUSED(pages);
        seen.insert(src);
        if (src->bookmarks && !result.warnings.contains(QStringLiteral("bookmarks")))
            result.warnings << QStringLiteral("bookmarks");
        if (src->forms && !result.warnings.contains(QStringLiteral("forms")))
            result.warnings << QStringLiteral("forms");
        if (src->encrypted && !result.warnings.contains(QStringLiteral("encrypted")))
            result.warnings << QStringLiteral("encrypted");
    }

    const QString tmp = outFile + QStringLiteral(".part.tmp");
    try {
        QPDF out;
        out.emptyPDF();
        out.setSuppressWarnings(true);
        QPDFPageDocumentHelper outHelper(out);
        int linksRemoved = 0;
        const int total = int(specs.size());
        // the copies of the source pages that go into the new file (a page used twice has two; links lead to the first)
        std::vector<QPDFPageObjectHelper> copies(size_t(specs.size()), QPDFPageObjectHelper(QPDFObjectHandle::newNull()));
        std::map<QPDFObjGen, QPDFObjectHandle> firstCopy;
        for (int i = 0; i < specs.size(); ++i) {
            if (specs.at(i).blank)
                continue;
            Source* src = specSource[size_t(i)];
            QPDFPageObjectHelper original = src->pages[size_t(specs.at(i).page)];
            copies[size_t(i)] = original.shallowCopyPage();                                  // a copy: the source stays as it is
            firstCopy.emplace(original.getObjectHandle().getObjGen(), copies[size_t(i)].getObjectHandle());
        }
        for (int i = 0; i < specs.size(); ++i) {
            if (progress && !progress(i, total))
                throw CancelledError();
            const PageSpec& s = specs.at(i);
            if (s.blank) {
                QPDFObjectHandle page = QPDFObjectHandle::parse(
                    &out, "<< /Type /Page /MediaBox [0 0 " + std::to_string(s.blankSize.width()) + " " + std::to_string(s.blankSize.height())
                              + "] /Resources << >> >>");
                outHelper.addPage(QPDFPageObjectHelper(out.makeIndirectObject(page)), false);
                continue;
            }
            Source* src = specSource[size_t(i)];
            QPDFPageObjectHelper copy = copies[size_t(i)];
            linksRemoved += cleanAnnotations(copy, firstCopy);
            outHelper.addPage(copy, false);
        }
        // rotation and crop are applied to the pages as they are in the new file
        std::vector<QPDFPageObjectHelper> outPages = outHelper.getAllPages();
        int cropFailed = 0;
        for (int i = 0; i < specs.size() && i < int(outPages.size()); ++i) {
            const PageSpec& s = specs.at(i);
            const int turns = ((s.rotation % 4) + 4) % 4;
            if (turns != 0)
                outPages[size_t(i)].rotatePage(turns * 90, true);
            if (!applyCrop(outPages[size_t(i)], s.crop))
                ++cropFailed;
        }
        if (linksRemoved > 0)
            result.warnings << QStringLiteral("linksRemoved:%1").arg(linksRemoved);
        if (cropFailed > 0)
            result.warnings << QStringLiteral("cropTooBig:%1").arg(cropFailed);

        QFile::remove(tmp);
        const QByteArray outputName = tmp.toUtf8();        // QPDFWriter retains the filename pointer through write().
        QPDFWriter writer(out, outputName.constData());
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setStreamDataMode(qpdf_s_compress);
        writer.write();
        if (progress && !progress(total, total))
            throw CancelledError();
    } catch (const CancelledError&) {
        QFile::remove(tmp);
        result.cancelled = true;
        result.error = QStringLiteral("cancelled");
        return result;
    } catch (const QPDFSystemError& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const std::exception& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("unknown");
        result.detail = QString::fromUtf8(e.what());
        return result;
    }

    if (overwrite)
        QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        result.detail = QStringLiteral("cannot create ") + outFile;
        return result;
    }
    result.ok = true;
    result.pages = int(specs.size());
    result.bytes = QFileInfo(outFile).size();
    return result;
}

} // namespace

WriteResult writePages(const QList<PageSpec>& pages, const QString& outFile, const Progress& progress, bool overwrite)
{
    Context ctx;
    return writeWith(ctx, pages, outFile, progress, overwrite);
}

int pageCountOf(const QString& file, const QString& password, QString* error)
{
    Context ctx;
    Failure failure;
    Source* src = ctx.open(file, password, failure);
    if (!src) {
        if (error)
            *error = failure.error;
        return 0;
    }
    if (error)
        error->clear();
    return int(src->pages.size());
}

QList<SplitPart> planSplitBySize(const QString& file, const QString& password, qint64 maxBytes, const Progress& progress, QString* error)
{
    QList<SplitPart> parts;
    Context ctx;
    Failure failure;
    Source* src = ctx.open(file, password, failure);
    if (!src) {
        if (error)
            *error = failure.error;
        return parts;
    }
    const int n = int(src->pages.size());
    QTemporaryDir dir;
    if (!dir.isValid() || maxBytes < 1024) {
        if (error)
            *error = QStringLiteral("range");
        return parts;
    }
    auto sizeOf = [&](int start, int count) -> qint64 {
        QList<PageSpec> specs;
        for (int p = start; p < start + count; ++p) {
            PageSpec s;
            s.file = file;
            s.password = password;
            s.page = p;
            specs.append(s);
        }
        const WriteResult r = writeWith(ctx, specs, dir.filePath(QStringLiteral("probe.pdf")), {}, true);
        return r.ok ? r.bytes : -1;
    };
    for (int start = 0; start < n;) {
        if (progress && !progress(start, n)) {
            if (error)
                *error = QStringLiteral("cancelled");
            return {};
        }
        // the most pages from `start` that still fit: grow while it fits, then narrow down (at least one page, whatever its size)
        int fit = 1;
        int step = 1;
        int firstTooBig = n - start + 1;
        while (fit < n - start) {
            const int tryCount = std::min(n - start, fit + step);
            const qint64 size = sizeOf(start, tryCount);
            if (size < 0) {
                if (error)
                    *error = QStringLiteral("write");
                return {};
            }
            if (size <= maxBytes) {
                fit = tryCount;
                step *= 2;
            } else {
                firstTooBig = tryCount;
                break;
            }
        }
        while (firstTooBig - fit > 1) {
            const int mid = (fit + firstTooBig) / 2;
            const qint64 size = sizeOf(start, mid);
            if (size < 0) {
                if (error)
                    *error = QStringLiteral("write");
                return {};
            }
            if (size <= maxBytes)
                fit = mid;
            else
                firstTooBig = mid;
        }
        SplitPart part;
        for (int p = start; p < start + fit; ++p)
            part.pages.append(p);
        part.suffix = QStringLiteral("pages-") + rangeLabel(part.pages);
        parts.append(part);
        start += fit;
    }
    if (progress)
        progress(n, n);
    if (error)
        error->clear();
    return parts;
}

} // namespace dp::tools
