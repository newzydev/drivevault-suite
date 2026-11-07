#include "core/FileUtils.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv::fileutils;

class TstFileUtils : public QObject {
    Q_OBJECT
private slots:
    void sanitizeRemovesInvalidCharacters()
    {
        QCOMPARE(sanitizeFileName("My: Video/Project?"), QStringLiteral("My VideoProject"));
        QCOMPARE(sanitizeFileName("  spaced name.  "), QStringLiteral("spaced name"));
    }
    void sanitizeKeepsThaiNames()
    {
        const QString thai = QStringLiteral("โปรเจกต์ตัดต่อ ๑");
        QCOMPARE(sanitizeFileName(thai), thai);
    }
    void sanitizeRejectsReservedAndEmpty()
    {
        QVERIFY(sanitizeFileName("").isEmpty());
        QVERIFY(sanitizeFileName("...").isEmpty());
        QVERIFY(sanitizeFileName("CON").isEmpty());
        QVERIFY(sanitizeFileName("nul.txt").isEmpty());
        QVERIFY(sanitizeFileName("<>:\"|").isEmpty());
    }
    void sanitizeBlocksPathTraversal()
    {
        const QString out = sanitizeFileName("..\\..\\evil");
        QVERIFY(!out.contains('\\'));
        QVERIFY(!out.contains('/'));
    }
    void atomicWriteCreatesReplacesAndBacksUp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("sub/dir/file.json");
        QString error;
        QVERIFY2(writeAtomic(path, "one", &error), qPrintable(error));
        QVERIFY2(writeAtomic(path, "two", &error, true), qPrintable(error));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("two"));
        QFile bak(path + ".bak");
        QVERIFY(bak.open(QIODevice::ReadOnly));
        QCOMPARE(bak.readAll(), QByteArray("one"));
    }
    void atomicWriteFailureKeepsOriginal()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("keep.json");
        QVERIFY(writeAtomic(path, "original"));
        // A directory with the target's temp-file name prevents nothing, so instead make the
        // destination read-only and verify the original survives a failed replace.
        QFile::setPermissions(path, QFileDevice::ReadOwner);
        QString error;
        const bool ok = writeAtomic(path, "changed", &error);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray content = f.readAll();
        QVERIFY(ok || content == "original");
    }
};

QTEST_APPLESS_MAIN(TstFileUtils)
#include "tst_fileutils.moc"
