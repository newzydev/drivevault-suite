#include "GoogleAccount.h"
#include "OAuthConfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
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
