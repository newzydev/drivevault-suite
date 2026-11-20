#include "ProfileSession.h"

#include "GoogleDesktopOAuth.h"
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>

#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPasswordDigestor>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <qt_windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

namespace dv::br {
namespace {

QJsonObject embeddedOAuth()
{
    auto keyBytes = QByteArray::fromBase64(desktopOAuth::key);
    auto iv = QByteArray::fromBase64(desktopOAuth::iv);
    auto encrypted = QByteArray::fromBase64(desktopOAuth::ciphertext);
    if (keyBytes.size() != 32 || iv.size() != 16 || encrypted.isEmpty()) return {};

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return {};
    BCRYPT_KEY_HANDLE key = nullptr;
    auto bytes = [](QByteArray& value) { return reinterpret_cast<PUCHAR>(value.data()); };
    QByteArray plain(encrypted.size(), Qt::Uninitialized);
    ULONG size = 0;
    const bool ok = BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0, bytes(keyBytes), ULONG(keyBytes.size()), 0) == 0
        && BCryptDecrypt(key, bytes(encrypted), ULONG(encrypted.size()), nullptr,
                         bytes(iv), ULONG(iv.size()), bytes(plain), ULONG(plain.size()), &size, BCRYPT_BLOCK_PADDING) == 0;
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    const auto config = ok ? QJsonDocument::fromJson(QByteArrayView(plain).first(size).toByteArray()).object() : QJsonObject();
    SecureZeroMemory(plain.data(), size_t(plain.size()));
    SecureZeroMemory(keyBytes.data(), size_t(keyBytes.size()));
    return config;
}

QByteArray randomBytes(int length)
{
    QByteArray bytes(length, Qt::Uninitialized);
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), ULONG(length),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return {};
    return bytes;
}

QByteArray protect(QByteArray input, bool encrypt)
{
    if (input.isEmpty()) return {};
    DATA_BLOB source{DWORD(input.size()), reinterpret_cast<BYTE*>(input.data())};
    DATA_BLOB output{};
    const BOOL ok = encrypt
        ? CryptProtectData(&source, L"DriveVault Browser Pro", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

bool writeProtected(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    const auto encrypted = protect(data, true);
    return !encrypted.isEmpty() && file.open(QIODevice::WriteOnly)
        && file.write(encrypted) == encrypted.size() && file.commit();
}

QByteArray pinDigest(const QString& pin, const QByteArray& salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, pin.toUtf8(), salt, 600000, 32);
}

bool pinMatches(const QString& pin, const QJsonObject& account)
{
    const auto salt = QByteArray::fromHex(account.value("salt").toString().toLatin1());
    const auto expected = QByteArray::fromHex(account.value("pin").toString().toLatin1());
    if (salt.size() != 32 || expected.size() != 32) return false;
    const auto actual = pinDigest(pin, salt);
    if (actual.size() != expected.size()) return false;
    unsigned difference = 0;
    for (qsizetype i = 0; i < actual.size(); ++i)
        difference |= unsigned(uchar(actual[i]) ^ uchar(expected[i]));
    return difference == 0;
}

bool validPin(const QString& pin)
{
    static const QRegularExpression sixDigits(QStringLiteral("\\A[0-9]{6}\\z"));
    return sixDigits.match(pin).hasMatch();
}

QByteArray formBody(const QList<QPair<QString, QString>>& fields)
{
    QByteArray result;
    for (const auto& [key, value] : fields) {
        if (!result.isEmpty()) result += '&';
        result += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return result;
}

QByteArray request(QNetworkAccessManager& network, QNetworkRequest req, const QByteArray& body = {})
{
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = body.isEmpty() ? network.get(req) : network.post(req, body);
    QEventLoop wait;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 bytes, qint64) {
        if (bytes > 1024 * 1024) reply->abort();
    });
    deadline.start(20000);
    if (!reply->isFinished()) wait.exec();
    const auto result = reply->error() == QNetworkReply::NoError
        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
        && reply->bytesAvailable() <= 1024 * 1024 ? reply->readAll() : QByteArray();
    reply->deleteLater();
    return result;
}

QJsonObject googleProfile(const QString& client, const QString& secret, ProfileSession* session)
{
    const QByteArray callbackPage = QByteArrayLiteral(
        "<!doctype html><html lang=\"th\"><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>DriveVault Browser Pro</title>"
        "<style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#101820;"
        "color:#edf7fa;font:16px system-ui}main{margin:24px;padding:32px;border:1px solid #315160;"
        "border-radius:16px;background:#172632;max-width:520px}h1{font-size:24px}p{line-height:1.6}</style>"
        "<main><h1>DriveVault Browser Pro</h1><p>กลับไปที่ Browser Pro เพื่อดำเนินการต่อ "
        "หากเป็นครั้งแรก ให้ตั้ง PIN 6 หลักในโปรแกรม</p>"
        "<p lang=\"en\">Return to Browser Pro. If this is your first sign-in, set your six-digit PIN in the app.</p></main>"
        "<script>history.replaceState(null,'',location.pathname)</script></html>");
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return {};
    const auto encoding = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
    const QByteArray stateBytes = randomBytes(32);
    const QByteArray verifierBytes = randomBytes(48);
    if (stateBytes.isEmpty() || verifierBytes.isEmpty()) return {};
    const QString state = QString::fromLatin1(stateBytes.toBase64(encoding));
    const QByteArray verifier = verifierBytes.toBase64(encoding);
    const QString redirect = QStringLiteral("http://127.0.0.1:%1/oauth/callback").arg(server.serverPort());
    QUrl authorize(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery query;
    query.addQueryItem("client_id", client);
    query.addQueryItem("redirect_uri", redirect);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", "openid email profile");
    query.addQueryItem("state", state);
    query.addQueryItem("prompt", "select_account");
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256).toBase64(encoding)));
    authorize.setQuery(query);
    QEventLoop waiting;
    QTimer::singleShot(180000, &waiting, &QEventLoop::quit);
    QObject::connect(session, &ProfileSession::signInCancelled, &waiting, &QEventLoop::quit);
    QString code;
    bool received = false;
    QObject::connect(&server, &QTcpServer::newConnection, &waiting, [&] {
        auto* socket = server.nextPendingConnection();
        if (!socket) return;
        auto consume = [&, socket] {
            QByteArray data = socket->property("request").toByteArray() + socket->readAll();
            if (data.size() > 8192) { socket->abort(); return; }
            if (!data.contains("\r\n\r\n")) { socket->setProperty("request", data); return; }
            const auto line = data.left(data.indexOf("\r\n")).split(' ');
            const QUrl url = line.size() == 3 && line[0] == "GET" ? QUrl::fromEncoded(line[1], QUrl::StrictMode) : QUrl();
            const QUrlQuery response(url);
            const bool expected = !received && url.isValid() && url.isRelative() && url.path() == "/oauth/callback"
                && response.allQueryItemValues("state").size() == 1
                && response.queryItemValue("state", QUrl::FullyDecoded) == state;
            if (expected && response.allQueryItemValues("code").size() == 1)
                code = response.queryItemValue("code", QUrl::FullyDecoded);
            socket->write(expected
                ? "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\n"
                  "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'\r\n"
                  "Content-Type: text/html; charset=utf-8\r\nContent-Length: " + QByteArray::number(callbackPage.size())
                  + "\r\n\r\n" + callbackPage
                : QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
            socket->disconnectFromHost();
            if (expected) {
                received = true;
                server.close();
                waiting.quit();
            }
        };
        QObject::connect(socket, &QTcpSocket::readyRead, &waiting, consume);
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, &QTcpSocket::abort);
        if (socket->bytesAvailable()) consume();
    });
    if (!QDesktopServices::openUrl(authorize)) return {};
    if (!received) waiting.exec();
    if (code.isEmpty()) return {};

    QNetworkAccessManager network;
    QNetworkRequest token{QUrl(QStringLiteral("https://oauth2.googleapis.com/token"))};
    token.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    const auto tokenJson = QJsonDocument::fromJson(request(network, token, formBody({
        {"client_id", client}, {"client_secret", secret}, {"code", code}, {"code_verifier", QString::fromLatin1(verifier)},
        {"redirect_uri", redirect}, {"grant_type", "authorization_code"}
    }))).object();
    const auto access = tokenJson.value("access_token").toString().toLatin1();
    if (access.isEmpty() || access.contains('\r') || access.contains('\n')) return {};
    QNetworkRequest info{QUrl(QStringLiteral("https://openidconnect.googleapis.com/v1/userinfo"))};
    info.setRawHeader("Authorization", "Bearer " + access);
    const auto profile = QJsonDocument::fromJson(request(network, info)).object();
    if (profile.value("sub").toString().isEmpty() || !profile.value("email_verified").toBool()
        || profile.value("email").toString().isEmpty()) return {};

    // Google avatars are fetched without bearer credentials and stored locally.
    QJsonObject identity{{"sub", profile.value("sub")}, {"name", profile.value("name")}, {"email", profile.value("email")}};
    const QUrl avatar(profile.value("picture").toString());
    if (avatar.scheme() == "https" && avatar.userInfo().isEmpty()
        && (avatar.host() == "googleusercontent.com" || avatar.host().endsWith(".googleusercontent.com"))) {
        auto bytes = request(network, QNetworkRequest(avatar));
        QBuffer source(&bytes);
        source.open(QIODevice::ReadOnly);
        QImageReader reader(&source);
        const QSize size = reader.size();
        const auto image = size.isValid() && size.width() <= 1024 && size.height() <= 1024 ? reader.read() : QImage();
        if (!image.isNull()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "PNG");
            identity.insert("avatar", QString::fromLatin1(png.toBase64()));
        }
    }
    return identity;
}

} // namespace

ProfileSession::ProfileSession(QString root, bool guestSession)
    : m_root(std::move(root)), m_guestSession(guestSession)
{
}

bool ProfileSession::signedIn() const { return m_account.contains("sub"); }
QString ProfileSession::name() const { return m_account.value("name").toString(email()); }
QString ProfileSession::email() const { return m_account.value("email").toString(); }

QUrl ProfileSession::picture() const
{
    const auto avatar = m_account.value("avatar").toString();
    return avatar.isEmpty() ? QUrl() : QUrl(QStringLiteral("data:image/png;base64,") + avatar);
}

QString ProfileSession::directory() const
{
    if (!signedIn()) return m_root;
    const auto key = QCryptographicHash::hash(m_account.value("sub").toString().toUtf8(), QCryptographicHash::Sha256).toHex();
    return m_root + QStringLiteral("/accounts/") + QString::fromLatin1(key);
}

bool ProfileSession::save() const
{
    return writeProtected(m_root + QStringLiteral("/account.bin"), QJsonDocument(m_account).toJson(QJsonDocument::Compact));
}

bool ProfileSession::load()
{
    QFile file(m_root + QStringLiteral("/account.bin"));
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    QJsonParseError error;
    m_account = QJsonDocument::fromJson(protect(file.readAll(), false), &error).object();
    return error.error == QJsonParseError::NoError && !m_account.isEmpty();
}

bool ProfileSession::authenticate()
{
    const auto builtIn = embeddedOAuth();
    const auto client = builtIn.value("GOOGLE_LOGIN_CLIENT_ID").toString();
    const auto secret = builtIn.value("GOOGLE_LOGIN_CLIENT_SECRET").toString();
    if (builtIn.value("client_type").toString() != "installed" || client.isEmpty() || secret.isEmpty()) {
        m_signInError = QStringLiteral("unavailable");
        return false;
    }
    auto profile = googleProfile(client, secret, this);
    if (m_cancelled) return false;
    if (profile.isEmpty()) {
        m_signInError = QStringLiteral("failed");
        return false;
    }
    const auto previous = m_account;
    m_account = profile;
    QFile preferences(directory() + QStringLiteral("/pin.bin"));
    if (preferences.open(QIODevice::ReadOnly) && preferences.size() <= 16384) {
        const auto saved = QJsonDocument::fromJson(protect(preferences.readAll(), false)).object();
        const auto salt = QByteArray::fromHex(saved.value("salt").toString().toLatin1());
        const auto digest = QByteArray::fromHex(saved.value("pin").toString().toLatin1());
        if (salt.size() == 32 && digest.size() == 32) {
            m_account.insert("salt", saved.value("salt"));
            m_account.insert("pin", saved.value("pin"));
            m_account.insert("idle", saved.value("idle"));
            if (save()) return true;
            m_account = previous;
            return false;
        }
    }
        m_account = previous;
    m_pendingProfile = profile;
    return false;
}

void ProfileSession::setInitialPin(const QString& pin, const QString& confirmation)
{
    if (!settingPin()) return;
    if (!validPin(pin) || pin != confirmation) {
        m_signInError = QStringLiteral("pin_mismatch");
        emit signInChanged();
        return;
    }
    const auto salt = randomBytes(32);
    const auto digest = salt.isEmpty() ? QByteArray() : pinDigest(pin, salt);
    if (digest.size() == 32) {
        m_account = m_pendingProfile;
        m_account.insert("salt", QString::fromLatin1(salt.toHex()));
        m_account.insert("pin", QString::fromLatin1(digest.toHex()));
        m_account.insert("idle", 10);
        if (save()) {
            m_pendingProfile = {};
            m_restartRequested = true;
            qApp->quit();
            return;
        }
        m_account = {};
    }
    m_signInError = QStringLiteral("pin_save_failed");
    emit signInChanged();
}

bool ProfileSession::start()
{
    if (!load()) {
        m_locked = true;
        m_profileError = QStringLiteral("profile_unreadable");
    } else {
        m_locked = signedIn();
    }
    return true;
}


void ProfileSession::signIn()
{
    if (m_guestSession || signedIn() || m_locked || m_signingIn || settingPin()) return;
    m_signingIn = true;
    m_cancelled = false;
    m_signInError.clear();
    emit signInChanged();
    const bool authenticated = authenticate();
    m_signingIn = false;
    emit signInChanged();
    if (!authenticated) return;
    m_restartRequested = true;
    qApp->quit();
}


void ProfileSession::cancelSignIn()
{
    if (settingPin()) {
        m_pendingProfile = {};
        m_signInError.clear();
        emit signInChanged();
        return;
    }
    if (!m_signingIn) return;
    m_cancelled = true;
    emit signInCancelled();
}

void ProfileSession::openGuest(const QString& language)
{
    if (!m_locked) return;
    const QStringList arguments{QStringLiteral("--guest-session"), QStringLiteral("--lang"),
        language == QStringLiteral("en") ? QStringLiteral("en") : QStringLiteral("th")};
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments)) {
        m_profileError = QStringLiteral("profile_guest_failed");
        emit profileChanged();
    }
}

void ProfileSession::unlock(const QString& pin)
{
    if (!m_locked || !signedIn()) return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (m_account.value("retryAfter").toInteger() > now) {
        m_profileError = QStringLiteral("profile_retry");
    } else {
        const auto previous = m_account;
        if (validPin(pin) && pinMatches(pin, m_account)) {
            m_account.remove("failures");
            m_account.remove("retryAfter");
            if (save()) {
                m_profileError.clear();
                m_locked = false;
                resetTimer();
                emit lockedChanged();
                return;
            }
        } else {
            const int failures = qBound(0, m_account.value("failures").toInt(), 34) + 1;
            m_account.insert("failures", failures);
            if (failures >= 5)
                m_account.insert("retryAfter", now + qMin(900, 30 * (failures - 4)));
            if (save()) {
                m_profileError = failures >= 5 ? QStringLiteral("profile_retry") : QStringLiteral("profile_bad_pin");
                emit profileChanged();
                return;
            }
        }
        m_account = previous;
        m_profileError = QStringLiteral("profile_save_failed");
    }
    emit profileChanged();
}

int ProfileSession::idleMinutes() const
{
    return qBound(1, m_account.value("idle").toInt(10), 120);
}

void ProfileSession::resetTimer()
{
    if (m_timer && !m_locked) m_timer->start(idleMinutes() * 60000);
}

void ProfileSession::enableLock()
{
    if (!signedIn() && !m_locked) return;
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ProfileSession::lock);
    qApp->installEventFilter(this);
    resetTimer();
}

bool ProfileSession::eventFilter(QObject*, QEvent* event)
{
    if (m_locked) return event->type() == QEvent::Shortcut;
    switch (event->type()) {
    case QEvent::KeyPress: case QEvent::MouseButtonPress: case QEvent::MouseMove:
    case QEvent::Wheel: case QEvent::TouchBegin: case QEvent::TabletPress:
        if (event->spontaneous()) resetTimer();
        break;
    default: break;
    }
    return false;
}

void ProfileSession::lock()
{
    m_profileError.clear();
    m_locked = true;
    emit lockedChanged();
}


bool ProfileSession::signOut()
{
    const QJsonObject pin{{"salt", m_account.value("salt")}, {"pin", m_account.value("pin")}, {"idle", m_account.value("idle")}};
    if (!writeProtected(directory() + QStringLiteral("/pin.bin"), QJsonDocument(pin).toJson(QJsonDocument::Compact))) return false;
    return writeProtected(m_root + QStringLiteral("/account.bin"), QByteArrayLiteral("{\"signedOut\":true}"));
}

void ProfileSession::setIdleMinutes(int minutes)
{
    if (!signedIn() || m_locked || minutes < 1 || minutes > 120) return;
    const auto previous = m_account;
    m_account.insert("idle", minutes);
    m_profileError.clear();
    if (save()) resetTimer();
    else {
        m_account = previous;
        m_profileError = QStringLiteral("profile_save_failed");
    }
    emit profileChanged();
}

void ProfileSession::requestSignOut()
{
    if (!signedIn() || m_locked) return;
    if (signOut()) { qApp->quit(); return; }
    m_profileError = QStringLiteral("profile_signout_failed");
    emit profileChanged();
}


} // namespace dv::br
