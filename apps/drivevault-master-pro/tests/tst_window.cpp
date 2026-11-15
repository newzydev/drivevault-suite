// Renders the real window (offscreen) against a fake registry and checks that every program shows its own colour.
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kRoot = QStringLiteral("Software\\DriveVaultMasterWindowTest");

int countNear(const QImage& img, QRgb c, int tol)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (std::abs(qRed(line[x]) - qRed(c)) <= tol && std::abs(qGreen(line[x]) - qGreen(c)) <= tol && std::abs(qBlue(line[x]) - qBlue(c)) <= tol)
                ++n;
    }
    return n;
}
} // namespace

class TstWindow : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QImage render(const QString& lang, const QString& name, const QString& tab = QStringLiteral("programs"))
    {
        const QString png = m_dir.filePath(name);
        QProcess p;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("QT_QUICK_BACKEND", "software");
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(MASTER_EXE_PATH), {"--registry-root", kRoot, "--data-dir", m_dir.filePath("data"), "--lang", lang,
                                                   "--screenshot", png, "--size", "1120x740", "--tab", tab});
        if (!p.waitForFinished(60000))
            return {};
        if (p.exitCode() != 0)
            qWarning() << "exit code" << p.exitCode() << p.readAllStandardError();
        return QImage(png).convertToFormat(QImage::Format_RGB32);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString exe = QStringLiteral(MASTER_EXE_PATH);          // any existing file stands in for the installed program
        QSettings s(QStringLiteral("HKEY_CURRENT_USER\\%1\\Products\\VideoEditorPro").arg(kRoot), QSettings::NativeFormat);
        s.setValue("DisplayName", "DriveVault Video Editor Pro");
        s.setValue("Version", "1.0.0");
        s.setValue("InstallDir", QFileInfo(exe).absolutePath());
        s.setValue("Executable", exe);
        s.setValue("Uninstaller", exe);
        s.sync();
    }
    void cleanupTestCase() { QSettings(QStringLiteral("HKEY_CURRENT_USER\\%1").arg(kRoot), QSettings::NativeFormat).remove(QString()); }

    void everyProgramShowsItsOwnColour()
    {
        const QImage img = render("en", "en.png");
        QVERIFY2(!img.isNull() && img.width() >= 1000, "the window could not be rendered");
        QVERIFY2(countNear(img, qRgb(0x8B, 0x5C, 0xF6), 14) > 1500, "Video Editor card (violet)");
        QVERIFY2(countNear(img, qRgb(0x2F, 0x6E, 0xCF), 34) > 1500, "Image Editor card (blue, dimmed: coming soon)");
        QVERIFY2(countNear(img, qRgb(0x14, 0x9A, 0x6E), 34) > 1500, "Audio Editor card (emerald, dimmed: coming soon)");
        QVERIFY2(countNear(img, qRgb(0xF5, 0x9E, 0x0B), 14) > 100, "the hub's own amber accent");
    }

    // The tabs at the top: Programs shows the cards; AI Q&A and Agentic AI are "coming soon" pages (no program cards, so no violet card colour).
    void tabsSwitchThePage()
    {
        const QImage programs = render("en", "tab-programs.png", "programs");
        const QImage ask = render("en", "tab-ask.png", "ask");
        const QImage agent = render("en", "tab-agent.png", "agent");
        QVERIFY(!programs.isNull() && !ask.isNull() && !agent.isNull());
        QVERIFY2(ask != programs && agent != programs && ask != agent, "each tab shows its own page");
        QVERIFY(countNear(programs, qRgb(0x8B, 0x5C, 0xF6), 14) > 1500);
        QVERIFY2(countNear(ask, qRgb(0x8B, 0x5C, 0xF6), 14) < 300, "no program cards on the AI Q&A tab");
        QVERIFY2(countNear(agent, qRgb(0x8B, 0x5C, 0xF6), 14) < 300, "no program cards on the Agentic AI tab");
    }

    void thaiRendersToo()
    {
        const QImage th = render("th", "th.png");
        const QImage en = render("en", "en2.png");
        QVERIFY(!th.isNull() && !en.isNull());
        QVERIFY2(th != en, "the Thai window must differ from the English one (texts are translated)");
        QVERIFY(countNear(th, qRgb(0x8B, 0x5C, 0xF6), 14) > 1500);
    }
};

QTEST_MAIN(TstWindow)
#include "tst_window.moc"
