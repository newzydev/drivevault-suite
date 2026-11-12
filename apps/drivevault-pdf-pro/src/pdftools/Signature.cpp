#include "Signature.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

#include <map>
#include <memory>

namespace dp::tools {

namespace {

constexpr int kSignatureBytes = 12000;                 // room for the PKCS#7 data in the file (hex: twice as many characters)
constexpr DWORD kEncoding = PKCS_7_ASN_ENCODING | X509_ASN_ENCODING;

struct StoreCloser {
    void operator()(void* s) const { if (s) CertCloseStore(static_cast<HCERTSTORE>(s), 0); }
};
struct CertFree {
    void operator()(const CERT_CONTEXT* c) const { if (c) CertFreeCertificateContext(c); }
};

QString nameOf(const CERT_CONTEXT* cert)
{
    wchar_t buf[256] = {};
    CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, buf, 255);
    return QString::fromWCharArray(buf);
}

// PKCS#7 data -> exact length of its DER encoding (the file has zeros after it)
int derLength(const QByteArray& b)
{
    if (b.size() < 4 || uchar(b[0]) != 0x30)
        return int(b.size());
    const uchar l = uchar(b[1]);
    if (!(l & 0x80))
        return std::min<int>(2 + l, int(b.size()));
    const int n = l & 0x7F;
    if (n < 1 || n > 4 || b.size() < 2 + n)
        return int(b.size());
    qint64 len = 0;
    for (int i = 0; i < n; ++i)
        len = (len << 8) | uchar(b[2 + i]);
    return int(std::min<qint64>(2 + n + len, b.size()));
}

QString zeroPad(qint64 v) { return QStringLiteral("%1").arg(v, 10, 10, QLatin1Char('0')); }

} // namespace

SignResult signPdf(const QString& inFile, const QString& outFile, const SignOptions& options, bool overwrite)
{
    SignResult result;
    if (!overwrite && QFileInfo::exists(outFile)) {
        result.error = QStringLiteral("exists");
        return result;
    }
    // ---- the certificate -----------------------------------------------------------------------------------------------------
    QFile pfx(options.pfxPath);
    if (!pfx.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("badPfx");
        return result;
    }
    QByteArray pfxBytes = pfx.readAll();
    CRYPT_DATA_BLOB blob{DWORD(pfxBytes.size()), reinterpret_cast<BYTE*>(pfxBytes.data())};
    const std::wstring password = options.pfxPassword.toStdWString();
    std::unique_ptr<void, StoreCloser> store(PFXImportCertStore(&blob, password.c_str(), PKCS12_NO_PERSIST_KEY));
    if (!store) {
        result.error = GetLastError() == ERROR_INVALID_PASSWORD ? QStringLiteral("wrongPfxPassword") : QStringLiteral("badPfx");
        return result;
    }
    std::unique_ptr<const CERT_CONTEXT, CertFree> signerCert;
    for (const CERT_CONTEXT* c = CertEnumCertificatesInStore(static_cast<HCERTSTORE>(store.get()), nullptr); c;
         c = CertEnumCertificatesInStore(static_cast<HCERTSTORE>(store.get()), c)) {
        DWORD size = 0;
        if (CertGetCertificateContextProperty(c, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size) || CertGetCertificateContextProperty(c, CERT_KEY_CONTEXT_PROP_ID, nullptr, &size)) {
            signerCert.reset(CertDuplicateCertificateContext(c));
            CertFreeCertificateContext(c);
            break;
        }
    }
    if (!signerCert) {
        result.error = QStringLiteral("noKey");
        return result;
    }
    result.signer = options.name.isEmpty() ? nameOf(signerCert.get()) : options.name;

    // ---- the file with an empty signature --------------------------------------------------------------------------------------
    const QString tmp = outFile + QStringLiteral(".part.tmp");
    QByteArray data;
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(inFile.toUtf8().constData());
        if (pdf.isEncrypted()) {
            result.error = QStringLiteral("encrypted");
            return result;
        }
        for (QPDFObjectHandle obj : pdf.getAllObjects())
            if (obj.isDictionary() && obj.hasKey("/ByteRange") && obj.hasKey("/Contents")) {
                result.error = QStringLiteral("alreadySigned");
                return result;
            }
        std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(pdf).getAllPages();
        if (pages.empty()) {
            result.error = QStringLiteral("format");
            return result;
        }
        QPDFObjectHandle sig = QPDFObjectHandle::newDictionary();
        sig.replaceKey("/Type", QPDFObjectHandle::newName("/Sig"));
        sig.replaceKey("/Filter", QPDFObjectHandle::newName("/Adobe.PPKLite"));
        sig.replaceKey("/SubFilter", QPDFObjectHandle::newName("/adbe.pkcs7.detached"));
        QPDFObjectHandle range = QPDFObjectHandle::newArray();
        range.appendItem(QPDFObjectHandle::newInteger(0));
        for (int i = 0; i < 3; ++i)
            range.appendItem(QPDFObjectHandle::newInteger(9999999999LL));        // replaced below by the real values (same width)
        sig.replaceKey("/ByteRange", range);
        sig.replaceKey("/Contents", QPDFObjectHandle::newString(std::string(size_t(kSignatureBytes), '\0')));
        sig.replaceKey("/M", QPDFObjectHandle::newString("D:" + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmss")).toStdString() + "+00'00'"));
        sig.replaceKey("/Name", QPDFObjectHandle::newUnicodeString(result.signer.toStdString()));
        if (!options.reason.isEmpty())
            sig.replaceKey("/Reason", QPDFObjectHandle::newUnicodeString(options.reason.toStdString()));
        if (!options.location.isEmpty())
            sig.replaceKey("/Location", QPDFObjectHandle::newUnicodeString(options.location.toStdString()));
        sig = pdf.makeIndirectObject(sig);

        QPDFObjectHandle widget = QPDFObjectHandle::newDictionary();
        widget.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        widget.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        widget.replaceKey("/FT", QPDFObjectHandle::newName("/Sig"));
        widget.replaceKey("/T", QPDFObjectHandle::newUnicodeString("Signature1"));
        widget.replaceKey("/V", sig);
        widget.replaceKey("/F", QPDFObjectHandle::newInteger(132));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        for (int i = 0; i < 4; ++i)
            rect.appendItem(QPDFObjectHandle::newInteger(0));
        widget.replaceKey("/Rect", rect);
        widget = pdf.makeIndirectObject(widget);
        QPDFObjectHandle page = pages.front().getObjectHandle();
        widget.replaceKey("/P", page);
        QPDFObjectHandle annots = page.getKey("/Annots");
        if (!annots.isArray()) {
            annots = QPDFObjectHandle::newArray();
            page.replaceKey("/Annots", annots);
        }
        annots.appendItem(widget);
        QPDFAcroFormDocumentHelper afdh(pdf);
        afdh.addFormField(QPDFFormFieldObjectHelper(widget));
        pdf.getRoot().getKey("/AcroForm").replaceKey("/SigFlags", QPDFObjectHandle::newInteger(3));

        QFile::remove(tmp);
        QPDFWriter writer(pdf, tmp.toUtf8().constData());
        writer.setObjectStreamMode(qpdf_o_disable);          // the signature dictionary must be plain bytes in the file
        writer.setStreamDataMode(qpdf_s_compress);
        writer.write();
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
    {
        QFile f(tmp);
        if (!f.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("write");
            return result;
        }
        data = f.readAll();
    }
    QFile::remove(tmp);

    // ---- the byte range and the signature ----------------------------------------------------------------------------------------
    const QByteArray placeholder = "<" + QByteArray(kSignatureBytes * 2, '0') + ">";
    const qint64 open = data.indexOf(placeholder);
    qint64 rangePos = data.indexOf("/ByteRange");
    if (open < 0 || rangePos < 0 || data.indexOf(placeholder, open + 1) >= 0) {
        result.error = QStringLiteral("unknown");
        result.detail = QStringLiteral("signature placeholder not found");
        return result;
    }
    const qint64 close = open + placeholder.size();                      // first byte after '>'
    const qint64 values[3] = {open, close, data.size() - close};
    for (qint64 v : values) {
        rangePos = data.indexOf("9999999999", rangePos);
        if (rangePos < 0) {
            result.error = QStringLiteral("unknown");
            return result;
        }
        data.replace(int(rangePos), 10, zeroPad(v).toLatin1());
        rangePos += 10;
    }
    QByteArray signedBytes = data.left(int(open)) + data.mid(int(close));
    CRYPT_SIGN_MESSAGE_PARA para{};
    para.cbSize = sizeof(para);
    para.dwMsgEncodingType = kEncoding;
    para.pSigningCert = signerCert.get();
    para.HashAlgorithm.pszObjId = const_cast<LPSTR>(szOID_NIST_sha256);
    const CERT_CONTEXT* chain[1] = {signerCert.get()};
    para.cMsgCert = 1;
    para.rgpMsgCert = const_cast<PCCERT_CONTEXT*>(chain);
    const BYTE* content[1] = {reinterpret_cast<const BYTE*>(signedBytes.constData())};
    DWORD contentSize[1] = {DWORD(signedBytes.size())};
    DWORD cmsSize = 0;
    if (!CryptSignMessage(&para, TRUE, 1, content, contentSize, nullptr, &cmsSize)) {
        result.error = QStringLiteral("noKey");
        result.detail = QStringLiteral("CryptSignMessage failed (%1)").arg(GetLastError());
        return result;
    }
    QByteArray cms(int(cmsSize), '\0');
    if (!CryptSignMessage(&para, TRUE, 1, content, contentSize, reinterpret_cast<BYTE*>(cms.data()), &cmsSize)) {
        result.error = QStringLiteral("noKey");
        result.detail = QStringLiteral("CryptSignMessage failed (%1)").arg(GetLastError());
        return result;
    }
    cms.resize(int(cmsSize));
    if (cms.size() > kSignatureBytes) {
        result.error = QStringLiteral("tooLarge");
        return result;
    }
    const QByteArray hex = cms.toHex().toUpper();
    data.replace(int(open) + 1, hex.size(), hex);                         // the rest of the placeholder stays zeros

    QSaveFile out(outFile);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        result.error = QStringLiteral("write");
        return result;
    }
    result.ok = true;
    return result;
}

QList<SignatureInfo> verifySignatures(const QString& file, const QString& password, QString* error)
{
    QList<SignatureInfo> out;
    if (error)
        error->clear();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("file");
        return out;
    }
    const QByteArray data = f.readAll();
    f.close();
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(file.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        std::map<QPDFObjGen, QString> fieldNames;
        for (QPDFObjectHandle obj : pdf.getAllObjects())
            if (obj.isDictionary() && obj.getKey("/FT").isName() && obj.getKey("/FT").getName() == "/Sig" && obj.getKey("/V").isDictionary() && obj.getKey("/T").isString())
                fieldNames[obj.getKey("/V").getObjGen()] = QString::fromUtf8(obj.getKey("/T").getUTF8Value().c_str());
        for (QPDFObjectHandle obj : pdf.getAllObjects()) {
            if (!obj.isDictionary() || !obj.getKey("/ByteRange").isArray() || !obj.getKey("/Contents").isString())
                continue;
            SignatureInfo info;
            if (auto it = fieldNames.find(obj.getObjGen()); it != fieldNames.end())
                info.fieldName = it->second;
            auto text = [&](const char* key) { return obj.getKey(key).isString() ? QString::fromUtf8(obj.getKey(key).getUTF8Value().c_str()) : QString(); };
            info.reason = text("/Reason");
            info.location = text("/Location");
            info.signingTime = text("/M");
            QPDFObjectHandle br = obj.getKey("/ByteRange");
            qint64 r[4] = {0, 0, 0, 0};
            bool good = br.getArrayNItems() == 4;
            for (int i = 0; good && i < 4; ++i) {
                QPDFObjectHandle v = br.getArrayItem(i);
                good = v.isInteger() && v.getIntValue() >= 0;
                if (good)
                    r[i] = v.getIntValue();
            }
            if (!good || r[0] != 0 || r[1] + r[3] > data.size() || r[2] + r[3] != data.size() - 0 && r[2] + r[3] > data.size() || r[1] > r[2]) {
                info.error = QStringLiteral("unreadable");
                out.append(info);
                continue;
            }
            info.coversWholeFile = r[2] + r[3] == data.size();
            const std::string raw = obj.getKey("/Contents").getStringValue();
            QByteArray cms(raw.data(), int(raw.size()));
            cms.resize(derLength(cms));
            const QByteArray content = data.mid(int(r[0]), int(r[1])) + data.mid(int(r[2]), int(r[3]));
            CRYPT_VERIFY_MESSAGE_PARA vp{};
            vp.cbSize = sizeof(vp);
            vp.dwMsgAndCertEncodingType = kEncoding;
            const BYTE* contents[1] = {reinterpret_cast<const BYTE*>(content.constData())};
            DWORD sizes[1] = {DWORD(content.size())};
            PCCERT_CONTEXT signer = nullptr;
            const BOOL ok = CryptVerifyDetachedMessageSignature(&vp, 0, reinterpret_cast<const BYTE*>(cms.constData()), DWORD(cms.size()), 1, contents, sizes, &signer);
            std::unique_ptr<const CERT_CONTEXT, CertFree> signerCert(signer);
            if (!signer) {
                info.error = ok ? QStringLiteral("noSigner") : QStringLiteral("badSignature");
                out.append(info);
                continue;
            }
            info.signer = nameOf(signer);
            info.intact = ok == TRUE;
            if (!info.intact)
                info.error = QStringLiteral("badSignature");
            // the chain: certificates in the signature are used to build it; no online revocation check is made
            CRYPT_DATA_BLOB sigBlob{DWORD(cms.size()), reinterpret_cast<BYTE*>(cms.data())};
            std::unique_ptr<void, StoreCloser> extra(CertOpenStore(CERT_STORE_PROV_PKCS7, kEncoding, 0, 0, &sigBlob));
            CERT_CHAIN_PARA cp{};
            cp.cbSize = sizeof(cp);
            PCCERT_CHAIN_CONTEXT chain = nullptr;
            if (CertGetCertificateChain(nullptr, signer, nullptr, static_cast<HCERTSTORE>(extra.get()), &cp, 0, nullptr, &chain)) {
                const DWORD status = chain->TrustStatus.dwErrorStatus;
                if (status == CERT_TRUST_NO_ERROR)
                    info.trust = QStringLiteral("trusted");
                else if (status & CERT_TRUST_IS_REVOKED)
                    info.trust = QStringLiteral("revoked");
                else if (status & CERT_TRUST_IS_NOT_TIME_VALID)
                    info.trust = QStringLiteral("expired");
                else if (status & (CERT_TRUST_IS_UNTRUSTED_ROOT | CERT_TRUST_IS_PARTIAL_CHAIN))
                    info.trust = QStringLiteral("untrusted");
                else
                    info.trust = QStringLiteral("unknown");
                CertFreeCertificateChain(chain);
            } else {
                info.trust = QStringLiteral("unknown");
            }
            out.append(info);
        }
    } catch (const QPDFExc& e) {
        out.clear();
        if (error)
            *error = e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format");
    } catch (const std::exception&) {
        out.clear();
        if (error)
            *error = QStringLiteral("format");
    }
    return out;
}

} // namespace dp::tools
