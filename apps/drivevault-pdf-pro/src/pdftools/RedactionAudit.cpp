#include "RedactionAudit.h"

#include <QCryptographicHash>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <set>

namespace dp::tools {

bool auditRedaction(const QString& path, const QList<QByteArray>& imageHashes, const std::function<bool(int)>& beforePage)
{
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        const auto filename = path.toUtf8();
        pdf.processFile(filename.constData());
        const auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
        if (pages.size() != size_t(imageHashes.size()) || pdf.isEncrypted()
            || pdf.getAllObjects().size() != 2 + pages.size() * 3
            || pdf.getTrailer().getKeys() != std::set<std::string>{"/Root", "/Size"}
            || pdf.getRoot().getKeys() != std::set<std::string>{"/Pages", "/Type"}
            || pdf.getRoot().getKey("/Pages").getKeys() != std::set<std::string>{"/Count", "/Kids", "/Type"})
            return false;
        for (size_t i = 0; i < pages.size(); ++i) {
            if (beforePage && !beforePage(int(i)))
                return false;
            auto page = pages[i].getObjectHandle();
            auto resources = page.getKey("/Resources");
            auto images = resources.getKey("/XObject");
            auto stream = images.getKey("/Image");
            auto content = page.getKey("/Contents");
            if (page.getKeys() != std::set<std::string>{"/Contents", "/MediaBox", "/Parent", "/Resources", "/Type"}
                || resources.getKeys() != std::set<std::string>{"/XObject"}
                || images.getKeys() != std::set<std::string>{"/Image"}
                || !stream.isStream() || !content.isStream()
                || stream.getDict().getKeys() != std::set<std::string>{
                    "/BitsPerComponent", "/ColorSpace", "/Filter", "/Height", "/Length", "/Subtype", "/Type", "/Width"}
                || content.getDict().getKeys() != std::set<std::string>{"/Length"})
                return false;
            auto dict = stream.getDict();
            if (!dict.getKey("/Type").isNameAndEquals("/XObject")
                || !dict.getKey("/Subtype").isNameAndEquals("/Image")
                || !dict.getKey("/Filter").isNameAndEquals("/FlateDecode")
                || !dict.getKey("/ColorSpace").isNameAndEquals("/DeviceRGB")
                || dict.getKey("/BitsPerComponent").getIntValue() != 8)
                return false;
            const auto pixels = stream.getStreamData();
            const auto widthPixels = dict.getKey("/Width").getIntValue();
            const auto heightPixels = dict.getKey("/Height").getIntValue();
            if (widthPixels <= 0 || widthPixels > 16000 || heightPixels <= 0 || heightPixels > 16000
                || qint64(pixels->getSize()) != widthPixels * heightPixels * 3)
                return false;
            const QByteArrayView data(reinterpret_cast<const char*>(pixels->getBuffer()), qsizetype(pixels->getSize()));
            if (QCryptographicHash::hash(data, QCryptographicHash::Sha256) != imageHashes.at(qsizetype(i)))
                return false;
            auto box = page.getKey("/MediaBox");
            const auto width = QByteArray::number(box.getArrayItem(2).getNumericValue(), 'f', 6);
            const auto height = QByteArray::number(box.getArrayItem(3).getNumericValue(), 'f', 6);
            const QByteArray expected = "q " + width + " 0 0 " + height + " 0 0 cm /Image Do Q\n";
            const auto commands = content.getStreamData();
            if (QByteArrayView(reinterpret_cast<const char*>(commands->getBuffer()), qsizetype(commands->getSize())) != expected)
                return false;
        }
        return pdf.getWarnings().empty();
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace dp::tools
