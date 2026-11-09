#include "SettingsManager.h"

#include "core/AppPaths.h"
#include "core/FileUtils.h"
#include "core/Logger.h"
#include "dv/core/Version.h"

#include <QColor>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QtMath>

namespace dv {

namespace {
using Type = SettingsManager::Type;

SettingsManager::Definition def(const QString& key, Type type, const QVariant& value,
                                const QVariant& min = {}, const QVariant& max = {},
                                const QStringList& choices = {}, bool restart = false)
{
    SettingsManager::Definition d;
    d.key = key;
    d.type = type;
    d.defaultValue = value;
    d.minimum = min;
    d.maximum = max;
    d.choices = choices;
    d.requiresRestart = restart;
    return d;
}
} // namespace

const QList<SettingsManager::Definition>& SettingsManager::definitions()
{
    static const QList<Definition> kDefinitions = {
        // General
        def("language", Type::Choice, "auto", {}, {}, {"auto", "th-TH", "en-US"}),
        def("theme", Type::Choice, "dark", {}, {}, {"dark", "light"}),
        def("accentColor", Type::Color, "#8B5CF6"),
        def("startupBehavior", Type::Choice, "home", {}, {}, {"home", "lastProject"}),
        def("recentProjectsLimit", Type::Int, 10, 1, 30),
        // Appearance
        def("uiScale", Type::Double, 1.0, 0.75, 2.0, {}, true),
        def("fontSizeOffset", Type::Int, 0, -2, 4),
        def("animations", Type::Bool, true),
        // Performance
        def("hardwareAcceleration", Type::Bool, false),
        def("useProxies", Type::Bool, true),
        def("previewQuality", Type::Choice, "full", {}, {}, {"full", "half", "quarter"}),
        def("cacheLocation", Type::Path, ""),
        def("cacheLimitMb", Type::Int, 4096, 256, 262144),
        def("memoryLimitMb", Type::Int, 2048, 512, 65536),
        // Editing
        def("defaultFps", Type::String, "30"),
        def("snapping", Type::Bool, true),
        def("copyMediaToProject", Type::Bool, true),            // imported files are copied into <project>/medias
        def("autoSaveEnabled", Type::Bool, true),
        def("autoSaveMinutes", Type::Int, 5, 1, 60),
        def("defaultProjectLocation", Type::Path, ""),
        // Audio
        def("audioOutputDevice", Type::String, "default"),
        def("previewVolume", Type::Int, 80, 0, 100),
        def("audioLatencyMs", Type::Int, 100, 10, 500),
        // AI
        def("aiEnabled", Type::Bool, false),
        def("aiDevice", Type::Choice, "auto", {}, {}, {"auto", "cpu", "gpu"}),
        def("aiModelsLocation", Type::Path, ""),
        def("aiOfflineOnly", Type::Bool, true),
        def("aiModel", Type::String, ""),
        // Export
        def("exportFormat", Type::Choice, "mp4", {}, {}, {"mp4", "mov", "mkv", "m4a"}),
        def("exportRange", Type::Choice, "all", {}, {}, {"all", "inout"}),
        def("exportPreset", Type::Choice, "1080p", {}, {}, {"720p", "1080p", "1440p", "4k", "custom"}),
        def("exportQuality", Type::Int, 75, 0, 100),
        def("exportFolder", Type::Path, ""),
        def("exportAudioBitrate", Type::Int, 192, 96, 320),
        def("exportEncoder", Type::String, "auto"),
        def("exportCustomWidth", Type::Int, 1920, 64, 8192),
        def("exportCustomHeight", Type::Int, 1080, 64, 8192),
    };
    return kDefinitions;
}

SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent)
{
    const auto& defs = definitions();
    for (int i = 0; i < defs.size(); ++i)
        m_index.insert(defs.at(i).key, i);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &SettingsManager::save);

    applyDefaults();
}

SettingsManager::~SettingsManager()
{
    if (m_dirty)
        save();
}

void SettingsManager::applyDefaults()
{
    m_values.clear();
    for (const Definition& d : definitions())
        m_values.insert(d.key, d.defaultValue);
}

QVariant SettingsManager::normalize(const Definition& d, const QVariant& input, bool* ok)
{
    *ok = true;
    switch (d.type) {
    case Type::Bool:
        if (input.typeId() == QMetaType::Bool)
            return input.toBool();
        break;
    case Type::Int: {
        bool conv = false;
        const double v = input.toDouble(&conv);
        if (conv && qIsFinite(v) && input.typeId() != QMetaType::Bool && input.typeId() != QMetaType::QString)
            return qBound(d.minimum.toInt(), int(qRound(v)), d.maximum.toInt());
        break;
    }
    case Type::Double: {
        bool conv = false;
        const double v = input.toDouble(&conv);
        if (conv && qIsFinite(v) && input.typeId() != QMetaType::Bool && input.typeId() != QMetaType::QString)
            return qBound(d.minimum.toDouble(), v, d.maximum.toDouble());
        break;
    }
    case Type::Choice:
        if (input.typeId() == QMetaType::QString && d.choices.contains(input.toString()))
            return input.toString();
        break;
    case Type::Color:
        if (input.typeId() == QMetaType::QString) {
            const QColor c(input.toString());
            if (c.isValid())
                return c.name(QColor::HexRgb).toUpper();
        }
        break;
    case Type::Path:
    case Type::String:
        if (input.typeId() == QMetaType::QString) {
            const QString s = input.toString().trimmed();
            if (s.size() <= 1024 && !s.contains(QChar(0)))
                return s;
        }
        break;
    }
    *ok = false;
    return d.defaultValue;
}

bool SettingsManager::load(const QString& file)
{
    m_file = file.isEmpty() ? paths::settingsFile() : file;
    applyDefaults();
    m_state.clear();

    QFile f(m_file);
    if (!f.exists()) {
        qCInfo(lcSettings) << "No settings file yet, using defaults:" << m_file;
        emit valuesChanged();
        return true;
    }

    bool success = false;
    QJsonParseError parseError;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        f.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            const QJsonObject stored = root.value(QStringLiteral("values")).toObject();
            for (auto it = stored.begin(); it != stored.end(); ++it) {
                const auto found = m_index.constFind(it.key());
                if (found == m_index.constEnd()) {
                    qCWarning(lcSettings) << "Ignoring unknown setting" << it.key();
                    continue;
                }
                bool ok = false;
                const QVariant v = normalize(definitions().at(*found), it.value().toVariant(), &ok);
                if (!ok)
                    qCWarning(lcSettings) << "Invalid value for" << it.key() << "- using default";
                m_values.insert(it.key(), v);
            }
            m_state = root.value(QStringLiteral("state")).toObject().toVariantMap();
            success = true;
        }
    }

    if (!success) {
        qCWarning(lcSettings) << "Settings file is unreadable (" << parseError.errorString()
                              << "), keeping a copy and using defaults";
        QFile::remove(m_file + QStringLiteral(".corrupt"));
        QFile::rename(m_file, m_file + QStringLiteral(".corrupt"));
        applyDefaults();
    }
    m_restartRequired = false;
    emit valuesChanged();
    return success;
}

bool SettingsManager::save()
{
    m_saveTimer.stop();
    if (m_file.isEmpty())
        m_file = paths::settingsFile();

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), version::kSettingsSchemaVersion);
    root.insert(QStringLiteral("appVersion"), QString::fromLatin1(version::kVersion));
    root.insert(QStringLiteral("values"), QJsonObject::fromVariantMap(m_values));
    root.insert(QStringLiteral("state"), QJsonObject::fromVariantMap(m_state));

    QString error;
    if (!fileutils::writeAtomic(m_file, QJsonDocument(root).toJson(QJsonDocument::Indented), &error)) {
        qCWarning(lcSettings) << "Cannot save settings:" << error;
        emit saveFailed(error);
        return false;
    }
    m_dirty = false;
    return true;
}

QVariant SettingsManager::get(const QString& key) const
{
    return m_values.value(key);
}

QVariant SettingsManager::defaultValue(const QString& key) const
{
    const auto it = m_index.constFind(key);
    return it == m_index.constEnd() ? QVariant() : definitions().at(*it).defaultValue;
}

bool SettingsManager::set(const QString& key, const QVariant& value)
{
    const auto it = m_index.constFind(key);
    if (it == m_index.constEnd()) {
        qCWarning(lcSettings) << "Rejected unknown setting" << key;
        return false;
    }
    const Definition& d = definitions().at(*it);
    bool ok = false;
    const QVariant v = normalize(d, value, &ok);
    if (!ok) {
        qCWarning(lcSettings) << "Rejected invalid value for" << key << ":" << value;
        return false;
    }
    if (m_values.value(key) == v)
        return true;

    m_values.insert(key, v);
    if (d.requiresRestart && !m_restartRequired) {
        m_restartRequired = true;
        emit restartRequiredChanged();
    }
    scheduleSave();
    emit valueChanged(key, v);
    emit valuesChanged();
    return true;
}

void SettingsManager::resetToDefaults()
{
    applyDefaults();
    scheduleSave();
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        emit valueChanged(it.key(), it.value());
    emit valuesChanged();
}

QVariant SettingsManager::state(const QString& key, const QVariant& fallback) const
{
    return m_state.value(key, fallback);
}

void SettingsManager::setState(const QString& key, const QVariant& valueIn)
{
    // QML hands a JS object over as a QJSValue wrapped in the QVariant; JSON cannot store that (it was saved as null, so the window state and the
    // panel layouts were never remembered). Convert it to a plain QVariantMap / QVariantList first.
    QVariant value = valueIn;
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        value = value.value<QJSValue>().toVariant();
    if (m_state.value(key) == value)
        return;
    m_state.insert(key, value);
    scheduleSave();
}

void SettingsManager::scheduleSave()
{
    m_dirty = true;
    m_saveTimer.start();
}

} // namespace dv
