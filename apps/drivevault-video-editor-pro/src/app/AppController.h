#pragma once

#include <QObject>
#include <QString>

namespace dv {

// UI-level navigation state shared by all QML screens.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString screen READ screen WRITE setScreen NOTIFY screenChanged FINAL)           // "home" | "editor"
    Q_PROPERTY(QString workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL) // media|edit|color|audio|effects|export
    Q_PROPERTY(bool drawAllClips READ drawAllClips CONSTANT FINAL)       // diagnostics: DV_TIMELINE_ALL=1 builds a delegate for every clip (the old behaviour)
    Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged FINAL)
    Q_PROPERTY(QString settingsPage READ settingsPage WRITE setSettingsPage NOTIFY settingsPageChanged FINAL)
    Q_PROPERTY(bool newProjectOpen READ newProjectOpen WRITE setNewProjectOpen NOTIFY newProjectOpenChanged FINAL)
    Q_PROPERTY(QString selectedMediaId READ selectedMediaId WRITE setSelectedMediaId NOTIFY selectedMediaIdChanged FINAL)
    Q_PROPERTY(bool aboutOpen READ aboutOpen WRITE setAboutOpen NOTIFY aboutOpenChanged FINAL)

public:
    using QObject::QObject;

    QString screen() const { return m_screen; }
    QString workspace() const { return m_workspace; }
    bool settingsOpen() const { return m_settingsOpen; }
    bool drawAllClips() const { return qEnvironmentVariableIntValue("DV_TIMELINE_ALL") != 0; }
    QString settingsPage() const { return m_settingsPage; }
    bool newProjectOpen() const { return m_newProjectOpen; }
    bool aboutOpen() const { return m_aboutOpen; }
    QString selectedMediaId() const { return m_selectedMediaId; }
    void setSelectedMediaId(const QString& id) { if (id != m_selectedMediaId) { m_selectedMediaId = id; emit selectedMediaIdChanged(); } }

    void setScreen(const QString& value);
    void setWorkspace(const QString& value);
    void setSettingsOpen(bool value);
    void setSettingsPage(const QString& value);
    void setNewProjectOpen(bool value);
    void setAboutOpen(bool value);

    // Closes the application and starts a fresh instance (used for settings that need a restart).
    Q_INVOKABLE void restart();
    Q_INVOKABLE void quit();

signals:
    void screenChanged();
    void workspaceChanged();
    void settingsOpenChanged();
    void settingsPageChanged();
    void newProjectOpenChanged();
    void aboutOpenChanged();
    void selectedMediaIdChanged();
    // kind: "info" | "success" | "warning" | "error"
    void toast(const QString& message, const QString& kind);

private:
    QString m_screen = QStringLiteral("home");
    QString m_workspace = QStringLiteral("edit");
    QString m_settingsPage = QStringLiteral("general");
    QString m_selectedMediaId;
    bool m_settingsOpen = false;
    bool m_newProjectOpen = false;
    bool m_aboutOpen = false;
};

} // namespace dv
