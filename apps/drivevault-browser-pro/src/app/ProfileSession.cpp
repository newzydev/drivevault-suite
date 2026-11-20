#include "ProfileSession.h"

#include "GoogleDesktopOAuth.h"
#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPasswordDigestor>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWindow>
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

QJsonObject googleProfile(const QString& client, const QString& secret)
{
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
    QDialog waiting;
    waiting.setWindowTitle(QStringLiteral("DriveVault Browser Pro"));
    auto* layout = new QVBoxLayout(&waiting);
    layout->addWidget(new QLabel(QStringLiteral("Complete Google sign-in in your browser.\nเข้าสู่ระบบ Google ในเบราว์เซอร์ของคุณ")));
    auto* cancel = new QPushButton(QStringLiteral("Cancel / ยกเลิก"));
    layout->addWidget(cancel);
    QObject::connect(cancel, &QPushButton::clicked, &waiting, &QDialog::reject);
    QTimer::singleShot(180000, &waiting, &QDialog::reject);
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
                ? "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nReturn to DriveVault Browser Pro."
                : "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
            if (expected) {
                received = true;
                server.close();
                waiting.accept();
            }
        };
        QObject::connect(socket, &QTcpSocket::readyRead, &waiting, consume);
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, &QTcpSocket::abort);
        if (socket->bytesAvailable()) consume();
    });
    if (!QDesktopServices::openUrl(authorize) || waiting.exec() != QDialog::Accepted || code.isEmpty()) return {};

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

ProfileSession::ProfileSession(QString root, bool english)
    : m_root(std::move(root)), m_english(english)
{
}

QString ProfileSession::trText(const char* en, const char* th) const
{
    return QString::fromUtf8(m_english ? en : th);
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
    QString client = qEnvironmentVariable("GOOGLE_LOGIN_CLIENT_ID").trimmed();
    QString secret = qEnvironmentVariable("GOOGLE_LOGIN_CLIENT_SECRET").trimmed();
    if (client.isEmpty() && secret.isEmpty()) {
        const auto builtIn = embeddedOAuth();
        client = builtIn.value("GOOGLE_LOGIN_CLIENT_ID").toString();
        secret = builtIn.value("GOOGLE_LOGIN_CLIENT_SECRET").toString();
    }
    const QString configPath = m_root + QStringLiteral("/google-oauth.bin");
    if (client.isEmpty() && secret.isEmpty()) {
        QFile stored(configPath);
        if (stored.open(QIODevice::ReadOnly) && stored.size() <= 16384) {
            const auto config = QJsonDocument::fromJson(protect(stored.readAll(), false)).object();
            client = config.value("client").toString();
            secret = config.value("secret").toString();
        }
    }
    if (!client.endsWith(".apps.googleusercontent.com") || secret.isEmpty()) {
        const auto path = QFileDialog::getOpenFileName(nullptr,
            trText("Choose Google Desktop OAuth credentials", "เลือกไฟล์ OAuth ของ Google สำหรับแอปเดสก์ท็อป"),
            QString(), trText("Credential text files (*.txt *.env);;All files (*)", "ไฟล์ข้อมูลรับรอง (*.txt *.env);;ทุกไฟล์ (*)"));
        if (path.isEmpty()) return false;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 16384) {
            const auto lines = QString::fromUtf8(file.readAll()).split('\n');
            for (const auto& line : lines) {
                const auto separator = line.indexOf('=');
                if (separator < 0) continue;
                const auto key = line.left(separator).trimmed();
                auto value = line.mid(separator + 1).trimmed();
                if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\'')) && value.back() == value.front())
                    value = value.mid(1, value.size() - 2);
                if (key == "GOOGLE_LOGIN_CLIENT_ID") client = value;
                if (key == "GOOGLE_LOGIN_CLIENT_SECRET") secret = value;
            }
        }
        if (!client.endsWith(".apps.googleusercontent.com") || secret.isEmpty()
            || !writeProtected(configPath, QJsonDocument(QJsonObject{{"client", client}, {"secret", secret}}).toJson(QJsonDocument::Compact))) {
            QMessageBox::warning(nullptr, "DriveVault Browser Pro",
                trText("Could not import the OAuth credentials.", "นำเข้าข้อมูล OAuth ไม่สำเร็จ"));
            return false;
        }
    }
    auto profile = googleProfile(client, secret);
    if (profile.isEmpty()) {
        QMessageBox::information(nullptr, "DriveVault Browser Pro",
            trText("Google sign-in did not complete.", "เข้าสู่ระบบ Google ไม่สำเร็จหรือถูกยกเลิก"));
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
    for (;;) {
        bool ok = false;
        const auto title = trText("Create a six-digit PIN", "ตั้ง PIN 6 หลัก");
        const auto pin = QInputDialog::getText(nullptr, title, trText("PIN", "รหัส PIN"), QLineEdit::Password, {}, &ok);
        if (!ok) return false;
        if (validPin(pin)) {
            const auto confirmation = QInputDialog::getText(nullptr, title, trText("Confirm PIN", "ยืนยันรหัส PIN"),
                                                            QLineEdit::Password, {}, &ok);
            if (!ok) return false;
            if (confirmation == pin) {
                const auto salt = randomBytes(32);
                if (salt.isEmpty()) return false;
                profile.insert("salt", QString::fromLatin1(salt.toHex()));
                profile.insert("pin", QString::fromLatin1(pinDigest(pin, salt).toHex()));
                profile.insert("idle", 10);
                m_account = profile;
                if (save()) return true;
                m_account = {};
                return false;
            }
        }
        QMessageBox::warning(nullptr, title, trText("Enter two matching six-digit PINs.", "กรอก PIN ตัวเลข 6 หลักให้ตรงกัน"));
    }
}

bool ProfileSession::start()
{
    if (!load()) {
        QMessageBox::warning(nullptr, "DriveVault Browser Pro",
            trText("The saved profile could not be read.", "อ่านโปรไฟล์ที่บันทึกไว้ไม่ได้"));
        return false;
    }
    if (signedIn()) return unlock();
    return true;   // not signed in: the browser opens at once as a guest; "Sign in" is on the home page and in the menu
}

void ProfileSession::signIn()
{
    if (signedIn() || m_locked || m_signingIn) return;
    const QScopedValueRollback signingIn(m_signingIn, true);
    if (!authenticate()) return;
    m_restartRequested = true;
    qApp->quit();
}


bool ProfileSession::unlock()
{
    const auto title = trText("Unlock DriveVault", "ปลดล็อก DriveVault");
    for (;;) {
        const auto remaining = m_account.value("retryAfter").toInteger() - QDateTime::currentSecsSinceEpoch();
        if (remaining > 0) {
            QMessageBox::information(nullptr, title,
                trText("Try again in %1 seconds.", "ลองใหม่ในอีก %1 วินาที").arg(remaining));
            return false;
        }
        bool ok = false;
        const auto pin = QInputDialog::getText(nullptr, title,
            trText("Enter your six-digit PIN for %1", "กรอก PIN 6 หลักสำหรับ %1").arg(email()),
            QLineEdit::Password, {}, &ok);
        if (!ok) return false;
        if (validPin(pin) && pinMatches(pin, m_account)) {
            m_account.remove("failures");
            m_account.remove("retryAfter");
            return save();
        }
        const int failures = m_account.value("failures").toInt() + 1;
        m_account.insert("failures", failures);
        if (failures >= 5)
            m_account.insert("retryAfter", QDateTime::currentSecsSinceEpoch() + qMin(900, 30 * (failures - 4)));
        if (!save()) return false;
        QMessageBox::warning(nullptr, title, trText("Incorrect PIN.", "รหัส PIN ไม่ถูกต้อง"));
    }
}

void ProfileSession::resetTimer()
{
    m_timer->start(qBound(1, m_account.value("idle").toInt(10), 120) * 60000);
}

void ProfileSession::enableLock()
{
    if (!signedIn()) return;
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ProfileSession::lock);
    qApp->installEventFilter(this);
    resetTimer();
}

bool ProfileSession::eventFilter(QObject*, QEvent* event)
{
    if (m_locked || !m_timer) return false;
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
    if (m_profileDialog) {
        m_profileDialog->reject();
        QTimer::singleShot(0, this, &ProfileSession::lock);
        return;
    }
    m_locked = true;
    emit lockedChanged();
    const bool quitOnClose = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    QList<QPointer<QWindow>> visible;
    for (auto* window : QGuiApplication::topLevelWindows()) {
        if (window->isVisible()) { visible.append(window); window->hide(); }
    }
    const bool unlocked = unlock();
    if (unlocked) {
        for (const auto& window : visible)
            if (window) window->show();
        m_locked = false;
        emit lockedChanged();
        resetTimer();
    } else {
        qApp->quit();
    }
    qApp->setQuitOnLastWindowClosed(quitOnClose);
}

bool ProfileSession::signOut()
{
    const QJsonObject pin{{"salt", m_account.value("salt")}, {"pin", m_account.value("pin")}, {"idle", m_account.value("idle")}};
    if (!writeProtected(directory() + QStringLiteral("/pin.bin"), QJsonDocument(pin).toJson(QJsonDocument::Compact))) return false;
    return writeProtected(m_root + QStringLiteral("/account.bin"), QByteArrayLiteral("{\"signedOut\":true}"));
}

int ProfileSession::execProfileDialog(QDialog& dialog)
{
    m_profileDialog = &dialog;
    const int result = dialog.exec();
    m_profileDialog = nullptr;
    return result;
}

void ProfileSession::showOptions()
{
    if (!signedIn() || m_locked) return;
    QMessageBox options;
    options.setWindowTitle(QStringLiteral("DriveVault Browser Pro"));
    options.setTextFormat(Qt::PlainText);
    options.setText(name() + '\n' + email());
    auto* timeout = options.addButton(trText("Auto-lock time", "เวลาล็อกอัตโนมัติ"), QMessageBox::ActionRole);
    auto* logout = options.addButton(trText("Sign out", "ออกจากระบบ"), QMessageBox::DestructiveRole);
    options.addButton(QMessageBox::Close);
    execProfileDialog(options);
    if (options.clickedButton() == timeout) {
        const int oldMinutes = m_account.value("idle").toInt(10);
        QInputDialog setting;
        setting.setWindowTitle(trText("Auto-lock", "ล็อกอัตโนมัติ"));
        setting.setLabelText(trText("Idle minutes", "จำนวนนาทีที่ไม่ได้ใช้งาน"));
        setting.setInputMode(QInputDialog::IntInput);
        setting.setIntRange(1, 120);
        setting.setIntValue(oldMinutes);
        if (execProfileDialog(setting) == QDialog::Accepted) {
            m_account.insert("idle", setting.intValue());
            if (!save()) m_account.insert("idle", oldMinutes);
            resetTimer();
        }
    } else if (options.clickedButton() == logout) {
        QMessageBox confirm(QMessageBox::Question, trText("Sign out", "ออกจากระบบ"),
            trText("Sign out and close Browser Pro?", "ออกจากระบบและปิด Browser Pro หรือไม่"), QMessageBox::Yes | QMessageBox::No);
        if (execProfileDialog(confirm) == QMessageBox::Yes) {
            if (signOut()) { qApp->quit(); return; }
            QMessageBox warning(QMessageBox::Warning, "DriveVault Browser Pro",
                trText("Could not sign out.", "ออกจากระบบไม่ได้"), QMessageBox::Ok);
            execProfileDialog(warning);
        }
    }
}

} // namespace dv::br
