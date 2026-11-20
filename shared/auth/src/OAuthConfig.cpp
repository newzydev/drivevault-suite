#include "OAuthConfig.h"

#include "GoogleDesktopOAuth.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <qt_windows.h>
#include <bcrypt.h>

namespace dv::account {

OAuthClient decryptClient(const char* keyBase64, const char* ivBase64, const char* cipherBase64)
{
    QByteArray key = QByteArray::fromBase64(keyBase64);
    QByteArray iv = QByteArray::fromBase64(ivBase64);
    QByteArray encrypted = QByteArray::fromBase64(cipherBase64);
    if (key.size() != 32 || iv.size() != 16 || encrypted.isEmpty()) return {};

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return {};
    BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                      ULONG((wcslen(BCRYPT_CHAIN_MODE_CBC) + 1) * sizeof(wchar_t)), 0);
    BCRYPT_KEY_HANDLE handle = nullptr;
    auto bytes = [](QByteArray& value) { return reinterpret_cast<PUCHAR>(value.data()); };
    QByteArray plain(encrypted.size(), Qt::Uninitialized);
    ULONG size = 0;
    const bool ok = BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, bytes(key), ULONG(key.size()), 0) == 0
        && BCryptDecrypt(handle, bytes(encrypted), ULONG(encrypted.size()), nullptr, bytes(iv), ULONG(iv.size()),
                         bytes(plain), ULONG(plain.size()), &size, BCRYPT_BLOCK_PADDING) == 0;
    if (handle) BCryptDestroyKey(handle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    OAuthClient client;
    if (ok) {
        const QJsonObject object = QJsonDocument::fromJson(QByteArray(plain.constData(), int(size))).object();
        client.id = object.value(QStringLiteral("GOOGLE_LOGIN_CLIENT_ID")).toString();
        client.secret = object.value(QStringLiteral("GOOGLE_LOGIN_CLIENT_SECRET")).toString();
    }
    SecureZeroMemory(plain.data(), size_t(plain.size()));
    SecureZeroMemory(key.data(), size_t(key.size()));
    return client;
}

OAuthClient embeddedClient()
{
    return decryptClient(desktopOAuth::key, desktopOAuth::iv, desktopOAuth::ciphertext);
}

} // namespace dv::account
