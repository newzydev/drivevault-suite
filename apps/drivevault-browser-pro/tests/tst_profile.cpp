#include "ProfileSession.h"
#include "GoogleDesktopOAuth.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDesktopServices>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPasswordDigestor>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QWindow>
#include <qt_windows.h>
#include <wincrypt.h>

class TstProfile : public QObject {
    Q_OBJECT
    QUrl authorization;
    Q_INVOKABLE void captureAuthorization(const QUrl& url) { authorization = url; }
private slots:
    void unconfiguredLoginReportsInlineError()
    {
        if (dv::br::desktopOAuth::ciphertext[0])
            QSKIP("This build has Desktop credentials.");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        dv::br::ProfileSession session(root.path(), true);
        QVERIFY(session.start());
        session.signIn();
        QCOMPARE(session.signInError(), QStringLiteral("unavailable"));
        QVERIFY(!session.signingIn());
        QVERIFY(!QApplication::activeModalWidget());
    }

    void bundledLoginOpensAuthorizationWithoutImport()
    {
        if (!dv::br::desktopOAuth::ciphertext[0])
            QSKIP("No credentials were supplied to the build.");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        dv::br::ProfileSession session(root.path(), true);
        QVERIFY(session.start());          // no welcome dialog: the browser opens as a guest
        QVERIFY(!session.signedIn());
        QDesktopServices::setUrlHandler("https", this, "captureAuthorization");
        bool waitedInline = false;
        QTimer::singleShot(100, &session, [&] {
            waitedInline = session.signingIn() && !QApplication::activeModalWidget();
            session.cancelSignIn();
        });
        session.signIn();                  // the Home page button
        QDesktopServices::unsetUrlHandler("https");
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(waitedInline);
        QVERIFY(authorization.host() == "accounts.google.com");
        const QUrlQuery query(authorization);
        QVERIFY(query.queryItemValue("client_id").endsWith(".apps.googleusercontent.com"));
        QVERIFY(query.queryItemValue("code_challenge_method") == "S256");
        QVERIFY(!query.hasQueryItem("client_secret"));
    }

    void startupRequiresPinAndUsesSeparateDataDirectory()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray salt(32, 'x');
        const auto digest = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, "123456", salt, 600000, 32);
        const QJsonObject profile{{"sub", "fake-google-account"}, {"name", "Test Profile"}, {"email", "fixture@example.invalid"},
                                  {"salt", QString::fromLatin1(salt.toHex())}, {"pin", QString::fromLatin1(digest.toHex())}};
        auto data = QJsonDocument(profile).toJson(QJsonDocument::Compact);
        DATA_BLOB input{DWORD(data.size()), reinterpret_cast<BYTE*>(data.data())};
        DATA_BLOB output{};
        QVERIFY(CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output));
        QByteArray protectedData(reinterpret_cast<const char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        QFile file(root.filePath("account.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(protectedData), protectedData.size());
        file.close();

        dv::br::ProfileSession session(root.path(), true);
        int pinPrompts = 0;
        bool hiddenAtLock = false;
        QWindow browserWindow;
        QTimer inputTimer;
        inputTimer.setInterval(50);
        connect(&inputTimer, &QTimer::timeout, this, [&] {
            auto* dialog = QApplication::activeModalWidget();
            if (auto* pin = qobject_cast<QInputDialog*>(dialog)) {
                if (session.locked()) hiddenAtLock = !browserWindow.isVisible();
                pin->setTextValue(++pinPrompts == 1 ? "654321" : "123456");
                pin->accept();
            } else if (auto* message = qobject_cast<QMessageBox*>(dialog)) {
                message->accept();
            }
        });
        inputTimer.start();
        QVERIFY(session.start());
        QCOMPARE(pinPrompts, 2);
        QVERIFY(session.signedIn());
        QCOMPARE(session.email(), QStringLiteral("fixture@example.invalid"));
        QVERIFY(session.directory().startsWith(root.path() + "/accounts/"));
        QVERIFY(session.directory() != root.path());

        browserWindow.show();
        session.enableLock();
        auto* idle = session.findChild<QTimer*>();
        QVERIFY(idle);
        QCOMPARE(idle->interval(), 10 * 60000);
        QVERIFY(QMetaObject::invokeMethod(idle, "timeout"));
        QCOMPARE(pinPrompts, 3);
        QVERIFY(hiddenAtLock);
        QVERIFY(browserWindow.isVisible());
    }
};

QTEST_MAIN(TstProfile)
#include "tst_profile.moc"
