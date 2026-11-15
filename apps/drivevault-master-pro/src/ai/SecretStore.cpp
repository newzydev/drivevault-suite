#include "SecretStore.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace dm::ai {
namespace SecretStore {

namespace {
#ifdef Q_OS_WIN
const wchar_t kEntropy[] = L"DriveVault Master Pro AI key";
DATA_BLOB entropyBlob()
{
    DATA_BLOB b;
    b.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kEntropy));
    b.cbData = DWORD(sizeof(kEntropy));
    return b;
}
#endif
} // namespace

QString protect(const QString& plain)
{
#ifdef Q_OS_WIN
    if (plain.isEmpty())
        return {};
    QByteArray in = plain.toUtf8();
    DATA_BLOB input{DWORD(in.size()), reinterpret_cast<BYTE*>(in.data())};
    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"DriveVault Master Pro", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    const QByteArray out(reinterpret_cast<const char*>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return QString::fromLatin1(out.toBase64());
#else
    Q_UNUSED(plain)
    return {};
#endif
}

QString unprotect(const QString& stored)
{
#ifdef Q_OS_WIN
    if (stored.isEmpty())
        return {};
    QByteArray in = QByteArray::fromBase64(stored.toLatin1());
    if (in.isEmpty())
        return {};
    DATA_BLOB input{DWORD(in.size()), reinterpret_cast<BYTE*>(in.data())};
    DATA_BLOB entropy = entropyBlob();
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    const QString plain = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData), int(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return plain;
#else
    Q_UNUSED(stored)
    return {};
#endif
}

} // namespace SecretStore
} // namespace dm::ai
