#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

namespace dv {

// Schema-driven, persisted application settings.
//
// Every setting has a key, a default and a validator, so a hand-edited or corrupted
// settings.json can never put the application into an invalid state. QML reads values via
// `AppSettings.values.<key>` (re-evaluated whenever anything changes) and writes via `set()`.
class SettingsManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged FINAL)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged FINAL)
    Q_PROPERTY(QString filePath READ filePath CONSTANT FINAL)

public:
    enum class Type { Bool, Int, Double, String, Choice, Color, Path };

    struct Definition {
        QString key;
        Type type = Type::String;
        QVariant defaultValue;
        QVariant minimum;      // Int / Double
        QVariant maximum;      // Int / Double
        QStringList choices;   // Choice
        bool requiresRestart = false;
    };

    explicit SettingsManager(QObject* parent = nullptr);
    ~SettingsManager() override;

    // Loads <dataDir>/settings.json (or `file` when given). Missing / corrupt files fall back to defaults.
    bool load(const QString& file = {});
    // Writes immediately (atomic). Normally called through the debounce timer.
    bool save();

    QVariantMap values() const { return m_values; }
    bool restartRequired() const { return m_restartRequired; }
    QString filePath() const { return m_file; }

    Q_INVOKABLE QVariant get(const QString& key) const;
    Q_INVOKABLE bool set(const QString& key, const QVariant& value);
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE QVariant defaultValue(const QString& key) const;

    // Free-form UI state (window geometry, splitter sizes ...). Not validated, stored under "state".
    Q_INVOKABLE QVariant state(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void setState(const QString& key, const QVariant& value);

    static const QList<Definition>& definitions();

signals:
    void valuesChanged();
    void valueChanged(const QString& key, const QVariant& value);
    void restartRequiredChanged();
    void saveFailed(const QString& message);

private:
    static QVariant normalize(const Definition& def, const QVariant& input, bool* ok);
    void scheduleSave();
    void applyDefaults();

    QVariantMap m_values;
    QVariantMap m_state;
    QHash<QString, int> m_index;
    QString m_file;
    QTimer m_saveTimer;
    bool m_restartRequired = false;
    bool m_dirty = false;
};

} // namespace dv
