#include "CallbackPage.h"
#include "GoogleAccount.h"
#include "OAuthConfig.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QTest>
#include <qt_windows.h>
#include <bcrypt.h>

using dv::account::GoogleAccount;

namespace {
void writeAccount(const QString& dir, const QJsonObject& object)
{
    QFile f(dir + QStringLiteral("/account.json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write the test account");
    f.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// Plays the browser: the account object listens in THIS thread, so the client side must not block (events keep running while it waits).
struct BrowserTrip {
    int port = 0;
    QString state;
    bool start(GoogleAccount& account)
    {
        QUrl authorize;
        QObject::connect(&account, &GoogleAccount::authorizeUrl, &account, [&](const QUrl& url) { authorize = url; });
        account.signIn();                                       // DV_ACCOUNT_DIR is set: no browser is opened, no Google call is made
        if (account.error() == QLatin1String("unavailable")) return false;
        const QUrlQuery query(authorize);
        const QUrl redirect(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
        state = query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
        port = redirect.port();
        return account.busy() && port > 0 && redirect.host() == QLatin1String("127.0.0.1") && !state.isEmpty();
    }
    QByteArray get(const QString& target) const
    {
        QTcpSocket socket;
        QByteArray all;
        QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&] { all += socket.readAll(); });
        QObject::connect(&socket, &QTcpSocket::connected, &socket, [&] {
            socket.write("GET " + target.toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
        });
        socket.connectToHost(QStringLiteral("127.0.0.1"), quint16(port));
        QElapsedTimer waited;
        waited.start();
        while (socket.state() != QAbstractSocket::UnconnectedState && waited.elapsed() < 5000)
            QTest::qWait(20);
        all += socket.readAll();
        return all;
    }
};
} // namespace

// The shared account: one file per user, every program watches it. DV_ACCOUNT_DIR keeps these tests away from the real account.
class TstAccount : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("DV_ACCOUNT_DIR", QDir::toNativeSeparators(m_dir.path()).toUtf8());
    }
    void init() { QFile::remove(m_dir.path() + QStringLiteral("/account.json")); }

    void startsSignedOutWithoutAFile()
    {
        GoogleAccount account;
        QVERIFY(!account.signedIn());
        QVERIFY(account.name().isEmpty());
        QVERIFY(!account.busy());
    }

    void readsTheSharedFile()
    {
        writeAccount(m_dir.path(), {{"sub", "42"}, {"name", "Somchai Jaidee"}, {"email", "somchai@example.com"}});
        GoogleAccount account;
        QVERIFY(account.signedIn());
        QCOMPARE(account.name(), QStringLiteral("Somchai Jaidee"));
        QCOMPARE(account.email(), QStringLiteral("somchai@example.com"));
        QVERIFY(account.picture().isEmpty());                 // no picture saved: the programs show the first letter
    }

    // The key under which a program keeps the data of this account: empty for nobody, stable for the same Google id, different for another one.
    void theScopeKeyNamesTheAccountWithoutNamingThePerson()
    {
        {
            GoogleAccount nobody;
            QVERIFY(nobody.scopeKey().isEmpty());
        }
        writeAccount(m_dir.path(), {{"sub", "1001"}, {"name", "Somchai Jaidee"}, {"email", "somchai@example.com"}});
        GoogleAccount first;
        const QString key = first.scopeKey();
        QCOMPARE(key.size(), 24);
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{24}$")).match(key).hasMatch());
        QVERIFY(!key.contains(QStringLiteral("1001")));
        QCOMPARE(GoogleAccount().scopeKey(), key);                       // the same account, the same key (also in another program)
        writeAccount(m_dir.path(), {{"sub", "1002"}, {"email", "napat@example.com"}});
        GoogleAccount other;
        QVERIFY(!other.scopeKey().isEmpty());
        QVERIFY(other.scopeKey() != key);
        first.signOut();
        QVERIFY(first.scopeKey().isEmpty());
    }

    void aNameThatIsMissingFallsBackToTheEmail()
    {
        writeAccount(m_dir.path(), {{"sub", "42"}, {"email", "somchai@example.com"}});
        GoogleAccount account;
        QCOMPARE(account.name(), QStringLiteral("somchai@example.com"));
    }

    void aDamagedOrOddFileIsNotAnAccount()
    {
        writeAccount(m_dir.path(), {{"sub", "42"}, {"email", "no-at-sign"}});
        QVERIFY(!GoogleAccount().signedIn());
        writeAccount(m_dir.path(), {{"email", "a@b.c"}});      // no id
        QVERIFY(!GoogleAccount().signedIn());
        QFile f(m_dir.path() + QStringLiteral("/account.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{ not json");
        f.close();
        QVERIFY(!GoogleAccount().signedIn());
    }

    void anotherRunningProgramFollowsSignInAndSignOut()
    {
        GoogleAccount first;                                   // "Master Pro"
        GoogleAccount second;                                  // "PDF Pro", already running
        QVERIFY(!first.signedIn() && !second.signedIn());
        QSignalSpy changed(&second, &GoogleAccount::changed);

        writeAccount(m_dir.path(), {{"sub", "7"}, {"name", "Napat"}, {"email", "napat@example.com"}});
        QTRY_VERIFY_WITH_TIMEOUT(second.signedIn(), 6000);     // the folder watcher or the 2 s look
        QCOMPARE(second.name(), QStringLiteral("Napat"));
        QVERIFY(changed.count() >= 1);

        first.reload();
        QVERIFY(first.signedIn());
        first.signOut();                                       // signing out in one program ...
        QVERIFY(!first.signedIn());
        QVERIFY(!QFile::exists(m_dir.path() + QStringLiteral("/account.json")));
        QTRY_VERIFY_WITH_TIMEOUT(!second.signedIn(), 6000);    // ... signs out the other
    }

    void signInWithoutBrowserDoesNotHang()
    {
        GoogleAccount account;
        QVERIFY(!account.busy());
        account.cancelSignIn();                                // nothing to cancel: harmless
        QVERIFY(!account.busy());
        QVERIFY(account.error().isEmpty());
    }

    // The browser comes back to http://127.0.0.1:<port>/oauth/callback: the page "you can go back" must arrive COMPLETE (it was lost once:
    // the listener was deleted with the socket that still held the page, and the browser showed "connection refused").
    void theBrowserGetsTheCallbackPageInFull()
    {
        GoogleAccount account;
        BrowserTrip trip;
        if (!trip.start(account)) QSKIP("this build has no Google client embedded");

        // a request with the wrong state is refused and the listener keeps waiting
        const QByteArray refused = trip.get(QStringLiteral("/oauth/callback?state=wrong&code=x"));
        QVERIFY2(refused.startsWith("HTTP/1.1 400"), refused.left(60).constData());
        QVERIFY(account.busy());

        // the real answer: 200 and the whole page (Content-Length bytes, ending with </html>), saying that the person is signed in
        const QByteArray answer = trip.get(QStringLiteral("/oauth/callback?state=") + trip.state + QStringLiteral("&code=test"));
        QVERIFY2(answer.startsWith("HTTP/1.1 200"), answer.left(60).constData());
        const int split = int(answer.indexOf("\r\n\r\n"));
        QVERIFY(split > 0);
        const QByteArray head = answer.left(split), body = answer.mid(split + 4);
        const int at = int(head.indexOf("Content-Length: "));
        QVERIFY(at >= 0);
        QCOMPARE(qsizetype(head.mid(at + 16).split('\r').first().toInt()), body.size());
        QVERIFY(body.trimmed().endsWith("</html>"));
        QVERIFY(body.contains("DriveVault"));
        QVERIFY(body.contains(QStringLiteral("เข้าสู่ระบบสำเร็จ").toUtf8()));
        QVERIFY(head.contains("Content-Security-Policy: default-src 'none'"));
        QVERIFY(!body.contains("http://") && !body.contains("https://"));      // nothing is loaded from anywhere
        QTRY_VERIFY_WITH_TIMEOUT(!account.busy(), 3000);        // the listener is closed, the sign-in is over (test mode: no Google call)
        QCOMPARE(account.error(), QStringLiteral("test-callback"));
        QVERIFY(!account.signedIn());
    }

    // Cancelled at Google: the page must NOT say "signed in", and the program learns "denied".
    void aCancelledSignInGetsAnHonestPage()
    {
        GoogleAccount account;
        BrowserTrip trip;
        if (!trip.start(account)) QSKIP("this build has no Google client embedded");
        const QByteArray answer = trip.get(QStringLiteral("/oauth/callback?state=") + trip.state + QStringLiteral("&error=access_denied"));
        QVERIFY2(answer.startsWith("HTTP/1.1 200"), answer.left(60).constData());
        QVERIFY(answer.trimmed().endsWith("</html>"));
        QVERIFY(answer.contains(QStringLiteral("ยังไม่ได้เข้าสู่ระบบ").toUtf8()));
        QVERIFY(!answer.contains(QStringLiteral("เข้าสู่ระบบสำเร็จ").toUtf8()));
        QTRY_VERIFY_WITH_TIMEOUT(!account.busy(), 3000);
        QCOMPARE(account.error(), QStringLiteral("denied"));
        QVERIFY(!account.signedIn());
    }

    // Not a check: with DV_DUMP_PAGE_DIR set it writes both callback pages there, to look at them in a browser.
    void dumpTheCallbackPagesWhenAsked()
    {
        const QString dir = qEnvironmentVariable("DV_DUMP_PAGE_DIR");
        if (dir.isEmpty()) QSKIP("set DV_DUMP_PAGE_DIR to write the pages");
        QDir().mkpath(dir);
        for (bool ok : {true, false}) {
            QFile f(dir + (ok ? QStringLiteral("/callback-ok.html") : QStringLiteral("/callback-no.html")));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(dv::account::callbackPage(ok));
        }
    }

    void theEmbeddedPairDecryptsAndAWrongKeyDoesNot()
    {
        // encrypt a pair the way shared\scripts\make-oauth-header.ps1 does (AES-256-CBC, PKCS#7) and read it back
        QByteArray key(32, Qt::Uninitialized), iv(16, Qt::Uninitialized);
        QVERIFY(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(key.data()), ULONG(key.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
        QVERIFY(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(iv.data()), ULONG(iv.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0);
        const QByteArray plain = QJsonDocument(QJsonObject{{"GOOGLE_LOGIN_CLIENT_ID", "test.apps.googleusercontent.com"},
                                                           {"GOOGLE_LOGIN_CLIENT_SECRET", "test-secret"}}).toJson(QJsonDocument::Compact);
        BCRYPT_ALG_HANDLE alg = nullptr;
        QVERIFY(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0);
        BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                          ULONG((wcslen(BCRYPT_CHAIN_MODE_CBC) + 1) * sizeof(wchar_t)), 0);
        BCRYPT_KEY_HANDLE handle = nullptr;
        QByteArray keyCopy = key, ivCopy = iv, input = plain;
        QVERIFY(BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, reinterpret_cast<PUCHAR>(keyCopy.data()), ULONG(keyCopy.size()), 0) == 0);
        QByteArray encrypted(plain.size() + 32, Qt::Uninitialized);
        ULONG written = 0;
        QVERIFY(BCryptEncrypt(handle, reinterpret_cast<PUCHAR>(input.data()), ULONG(input.size()), nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()),
                              ULONG(ivCopy.size()), reinterpret_cast<PUCHAR>(encrypted.data()), ULONG(encrypted.size()), &written, BCRYPT_BLOCK_PADDING) == 0);
        encrypted.resize(int(written));
        BCryptDestroyKey(handle);
        BCryptCloseAlgorithmProvider(alg, 0);

        const QByteArray k = key.toBase64(), v = iv.toBase64(), c = encrypted.toBase64();
        const auto client = dv::account::decryptClient(k.constData(), v.constData(), c.constData());
        QVERIFY(client.valid());
        QCOMPARE(client.id, QStringLiteral("test.apps.googleusercontent.com"));
        QCOMPARE(client.secret, QStringLiteral("test-secret"));

        QByteArray wrong = key;
        wrong[0] = char(wrong[0] ^ 0x55);
        const QByteArray w = wrong.toBase64();
        QVERIFY(!dv::account::decryptClient(w.constData(), v.constData(), c.constData()).valid());
        QVERIFY(!dv::account::decryptClient("", "", "").valid());             // a build without the credentials
    }
};

QTEST_MAIN(TstAccount)
#include "tst_account.moc"
