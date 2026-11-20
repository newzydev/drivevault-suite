#include "GoogleAccount.h"

#include "OAuthConfig.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <functional>
#include <qt_windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

namespace dv::account {
namespace {

constexpr qint64 kMaxFile = 1024 * 1024;

QByteArray randomBytes(int length)
{
    QByteArray bytes(length, Qt::Uninitialized);
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), ULONG(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
    return bytes;
}

QByteArray base64Url(const QByteArray& bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

// Windows DPAPI, this user only: another Windows user (or a copy of the file on another PC) cannot read it.
QByteArray protect(QByteArray input, bool encrypt)
{
    if (input.isEmpty()) return {};
    static char entropyText[] = "DriveVault.Account.v1";
    DATA_BLOB entropy{DWORD(sizeof(entropyText) - 1), reinterpret_cast<BYTE*>(entropyText)};
    DATA_BLOB source{DWORD(input.size()), reinterpret_cast<BYTE*>(input.data())};
    DATA_BLOB output{};
    const BOOL ok = encrypt
        ? CryptProtectData(&source, L"DriveVault account", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&source, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char*>(output.pbData), qsizetype(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

QByteArray formBody(const QList<QPair<QString, QString>>& fields)
{
    QByteArray result;
    for (const auto& field : fields) {
        if (!result.isEmpty()) result += '&';
        result += QUrl::toPercentEncoding(field.first) + '=' + QUrl::toPercentEncoding(field.second);
    }
    return result;
}

bool plausibleEmail(const QString& text)
{
    return text.size() <= 254 && text.count(QLatin1Char('@')) == 1 && !text.startsWith(QLatin1Char('@')) && !text.endsWith(QLatin1Char('@'))
        && !text.contains(QLatin1Char(' ')) && !text.contains(QLatin1Char('<')) && !text.contains(QLatin1Char('>'));
}

qint64 stampOf(const QFileInfo& info)
{
    return info.exists() ? (info.lastModified().toMSecsSinceEpoch() ^ (qint64(info.size()) << 40)) : -1;
}

bool testMode() { return qEnvironmentVariableIsSet("DV_ACCOUNT_DIR"); }

const QByteArray kDonePage = QByteArrayLiteral(
    "<!doctype html><html lang=\"th\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>DriveVault</title><style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#101014;color:#eef0f4;font:16px system-ui}"
    "main{margin:24px;padding:32px;border:1px solid #2c2f38;border-radius:16px;background:#17181d;max-width:520px}h1{font-size:24px}p{line-height:1.6}</style>"
    "<main><h1>DriveVault</h1><p>เข้าสู่ระบบด้วย Google เรียบร้อยแล้ว กลับไปที่โปรแกรม DriveVault ได้เลย ปิดหน้านี้ได้</p>"
    "<p lang=\"en\">You are signed in with Google. Go back to your DriveVault program; you can close this page.</p></main>"
    "<script>history.replaceState(null,'',location.pathname)</script></html>");

// One answer of Google or of the picture server: at most 1 MB, status 200 only.
void onAnswer(QNetworkReply* reply, QObject* context, const std::function<void(const QByteArray&)>& done)
{
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 bytes, qint64) { if (bytes > kMaxFile) reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done] {
        const bool ok = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QByteArray body = ok ? reply->readAll() : QByteArray();
        reply->deleteLater();
        done(body);
    });
}

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return request;
}

} // namespace

GoogleAccount::GoogleAccount(QObject* parent)
    : QObject(parent)
{
    QDir().mkpath(storeDir());
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(storeDir());
    auto* settle = new QTimer(this);          // a write makes several events: look once, shortly after the last one
    settle->setSingleShot(true);
    settle->setInterval(200);
    connect(settle, &QTimer::timeout, this, &GoogleAccount::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, settle, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::fileChanged, settle, qOverload<>(&QTimer::start));
    m_poll = new QTimer(this);                // the watcher can miss a change: a cheap look every 2 s as well
    m_poll->setInterval(2000);
    connect(m_poll, &QTimer::timeout, this, &GoogleAccount::reload);
    m_poll->start();
    reload();
}

GoogleAccount::~GoogleAccount() = default;

QString GoogleAccount::storeDir()
{
    const QString override = qEnvironmentVariable("DV_ACCOUNT_DIR");
    if (!override.isEmpty()) return QDir::cleanPath(override);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/Account"));
}

QString GoogleAccount::filePath() const
{
    return storeDir() + (testMode() ? QStringLiteral("/account.json") : QStringLiteral("/account.bin"));
}

QString GoogleAccount::name() const
{
    const QString value = m_identity.value(QLatin1String("name")).toString();
    return value.isEmpty() ? email() : value;
}

QUrl GoogleAccount::picture() const
{
    const QString avatar = m_identity.value(QLatin1String("avatar")).toString();
    return avatar.isEmpty() ? QUrl() : QUrl(QStringLiteral("data:image/png;base64,") + avatar);
}

bool GoogleAccount::readFile(QJsonObject* out) const
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxFile) return false;
    QByteArray bytes = file.readAll();
    if (!testMode()) bytes = protect(bytes, false);
    QJsonParseError error;
    const QJsonObject object = QJsonDocument::fromJson(bytes, &error).object();
    if (error.error != QJsonParseError::NoError || object.value(QLatin1String("sub")).toString().isEmpty()
        || !plausibleEmail(object.value(QLatin1String("email")).toString()))
        return false;
    *out = object;
    return true;
}

void GoogleAccount::reload()
{
    const qint64 stamp = stampOf(QFileInfo(filePath()));
    if (stamp == m_stamp) return;
    m_stamp = stamp;
    QJsonObject now;
    if (stamp != -1 && !readFile(&now)) now = QJsonObject();
    if (now == m_identity) return;
    m_identity = now;
    emit changed();
}

void GoogleAccount::clearError()
{
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit busyChanged();
}

void GoogleAccount::finish(const QString& error)
{
    if (m_deadline) { m_deadline->stop(); m_deadline->deleteLater(); m_deadline = nullptr; }
    if (m_server) { m_server->close(); m_server->deleteLater(); m_server = nullptr; }
    m_working = false;
    m_state.clear();
    m_verifier.clear();
    m_error = error;
    emit busyChanged();
}

void GoogleAccount::cancelSignIn()
{
    if (busy()) finish(QString());
}

void GoogleAccount::signIn()
{
    if (busy()) return;
    m_error.clear();
    const OAuthClient client = embeddedClient();
    const QByteArray stateBytes = randomBytes(32);
    const QByteArray verifierBytes = randomBytes(48);
    if (!client.valid()) { finish(QStringLiteral("unavailable")); return; }
    if (stateBytes.isEmpty() || verifierBytes.isEmpty()) { finish(QStringLiteral("failed")); return; }

    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost)) { finish(QStringLiteral("failed")); return; }
    m_state = base64Url(stateBytes);
    m_verifier = base64Url(verifierBytes);
    m_redirect = QStringLiteral("http://127.0.0.1:%1/oauth/callback").arg(m_server->serverPort());

    QUrl authorize(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), client.id);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirect);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), QStringLiteral("openid email profile"));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(m_state));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("select_account"));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(base64Url(QCryptographicHash::hash(m_verifier, QCryptographicHash::Sha256))));
    authorize.setQuery(query);

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] { finish(QStringLiteral("timeout")); });
    m_deadline->start(180000);

    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (m_server && m_server->hasPendingConnections()) {
            QTcpSocket* socket = m_server->nextPendingConnection();
            socket->setParent(this);          // the listener is deleted as soon as the answer arrives; the page for the browser must still be sent (a child socket died with it: "connection refused" in the browser)
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(5000, socket, &QTcpSocket::abort);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                QByteArray data = socket->property("request").toByteArray() + socket->readAll();
                if (data.size() > 8192) { socket->abort(); return; }
                if (!data.contains("\r\n\r\n")) { socket->setProperty("request", data); return; }
                const QList<QByteArray> line = data.left(data.indexOf("\r\n")).split(' ');
                const QUrl url = line.size() == 3 && line[0] == "GET" ? QUrl::fromEncoded(line[1], QUrl::StrictMode) : QUrl();
                const QUrlQuery response(url);
                const bool expected = m_server && url.isValid() && url.isRelative() && url.path() == QLatin1String("/oauth/callback")
                    && response.allQueryItemValues(QStringLiteral("state")).size() == 1
                    && response.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded) == QString::fromLatin1(m_state);
                socket->write(expected
                    ? "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\n"
                      "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'\r\n"
                      "Content-Type: text/html; charset=utf-8\r\nContent-Length: " + QByteArray::number(kDonePage.size()) + "\r\n\r\n" + kDonePage
                    : QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
                socket->disconnectFromHost();
                if (!expected) return;
                const QString code = response.allQueryItemValues(QStringLiteral("code")).size() == 1
                    ? response.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded) : QString();
                const QString problem = response.queryItemValue(QStringLiteral("error"));
                m_server->close();
                m_server->deleteLater();
                m_server = nullptr;
                if (code.isEmpty()) finish(problem == QLatin1String("access_denied") ? QStringLiteral("denied") : QStringLiteral("failed"));
                else if (testMode()) finish(QStringLiteral("test-callback"));   // tests: the callback page is checked, Google is never called
                else exchange(code);
            });
            if (socket->bytesAvailable()) emit socket->readyRead();
        }
    });
    emit busyChanged();
    emit authorizeUrl(authorize);
    if (testMode()) return;                  // tests play the browser themselves
    if (!QDesktopServices::openUrl(authorize)) finish(QStringLiteral("failed"));
}

void GoogleAccount::exchange(const QString& code)
{
    const OAuthClient client = embeddedClient();
    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_working = true;
    emit busyChanged();
    QNetworkRequest request = makeRequest(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    const QByteArray body = formBody({{QStringLiteral("client_id"), client.id}, {QStringLiteral("client_secret"), client.secret},
                                      {QStringLiteral("code"), code}, {QStringLiteral("code_verifier"), QString::fromLatin1(m_verifier)},
                                      {QStringLiteral("redirect_uri"), m_redirect}, {QStringLiteral("grant_type"), QStringLiteral("authorization_code")}});
    onAnswer(m_net->post(request, body), this, [this](const QByteArray& answer) {
        const QByteArray token = QJsonDocument::fromJson(answer).object().value(QLatin1String("access_token")).toString().toLatin1();
        if (token.isEmpty() || token.contains('\r') || token.contains('\n')) { finish(QStringLiteral("failed")); return; }
        fetchProfile(token);
    });
}

void GoogleAccount::fetchProfile(const QByteArray& accessToken)
{
    QNetworkRequest request = makeRequest(QUrl(QStringLiteral("https://openidconnect.googleapis.com/v1/userinfo")));
    request.setRawHeader("Authorization", "Bearer " + accessToken);
    onAnswer(m_net->get(request), this, [this](const QByteArray& answer) {
        const QJsonObject profile = QJsonDocument::fromJson(answer).object();
        const QString email = profile.value(QLatin1String("email")).toString();
        if (profile.value(QLatin1String("sub")).toString().isEmpty() || !profile.value(QLatin1String("email_verified")).toBool() || !plausibleEmail(email)) {
            finish(QStringLiteral("failed"));
            return;
        }
        const QJsonObject identity{{QStringLiteral("sub"), profile.value(QLatin1String("sub"))}, {QStringLiteral("name"), profile.value(QLatin1String("name"))},
                                   {QStringLiteral("email"), email}};
        // the picture comes from Google's picture host only and is fetched without any credentials
        const QUrl avatar(profile.value(QLatin1String("picture")).toString());
        const bool trusted = avatar.scheme() == QLatin1String("https") && avatar.userInfo().isEmpty()
            && (avatar.host() == QLatin1String("googleusercontent.com") || avatar.host().endsWith(QLatin1String(".googleusercontent.com")));
        if (trusted) fetchPicture(identity, avatar);
        else store(identity);
    });
}

void GoogleAccount::fetchPicture(const QJsonObject& identity, const QUrl& address)
{
    onAnswer(m_net->get(makeRequest(address)), this, [this, identity](const QByteArray& bytes) {
        QJsonObject result = identity;
        QByteArray copy = bytes;
        QBuffer source(&copy);
        source.open(QIODevice::ReadOnly);
        QImageReader reader(&source);
        const QSize size = reader.size();
        const QImage image = size.isValid() && size.width() <= 1024 && size.height() <= 1024 ? reader.read() : QImage();
        if (!image.isNull()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "PNG");
            result.insert(QStringLiteral("avatar"), QString::fromLatin1(png.toBase64()));
        }
        store(result);
    });
}

void GoogleAccount::store(const QJsonObject& identity)
{
    QDir().mkpath(storeDir());
    QByteArray bytes = QJsonDocument(identity).toJson(QJsonDocument::Compact);
    if (!testMode()) bytes = protect(bytes, true);
    QSaveFile file(filePath());
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        finish(QStringLiteral("failed"));
        return;
    }
    m_identity = identity;
    m_stamp = stampOf(QFileInfo(filePath()));
    emit changed();
    finish(QString());
}

void GoogleAccount::signOut()
{
    QFile::remove(filePath());
    m_stamp = -1;
    if (!m_identity.isEmpty()) {
        m_identity = QJsonObject();
        emit changed();
    }
}

} // namespace dv::account
