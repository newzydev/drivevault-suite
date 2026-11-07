#pragma once

#include <QObject>
#include <QString>

namespace dv {

// Read-only product / system facts for the About page.
class SystemInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString productName READ productName CONSTANT FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT FINAL)
    Q_PROPERTY(QString operatingSystem READ operatingSystem CONSTANT FINAL)
    Q_PROPERTY(QString architecture READ architecture CONSTANT FINAL)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT FINAL)
    Q_PROPERTY(QString compiler READ compiler CONSTANT FINAL)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT FINAL)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT FINAL)
    Q_PROPERTY(QString defaultCacheDirectory READ defaultCacheDirectory CONSTANT FINAL)
    Q_PROPERTY(QString defaultExportDirectory READ defaultExportDirectory CONSTANT FINAL)

public:
    using QObject::QObject;

    QString productName() const;
    QString version() const;
    QString buildNumber() const;
    QString operatingSystem() const;
    QString architecture() const;
    QString qtVersion() const;
    QString compiler() const;
    QString dataDirectory() const;
    QString logDirectory() const;
    QString defaultCacheDirectory() const;
    QString defaultExportDirectory() const;

    Q_INVOKABLE bool openFolder(const QString& path) const;
    // Opens Explorer with the file selected.
    Q_INVOKABLE bool showInFolder(const QString& path) const;
    Q_INVOKABLE bool openFile(const QString& path) const;
    // A document that ships with the application (user guide, release notes, license notices): looked up in <app>docs,
    // <app>icenses and, in a development tree, the repository's docs folder. Empty when it is not there.
    Q_INVOKABLE QString documentPath(const QString& fileName) const;
};

} // namespace dv
