#include "AppController.h"

#include "core/Logger.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

namespace dv {

namespace {
const QStringList kWorkspaces = {"media", "edit", "color", "audio", "effects", "ai", "export"};
const QStringList kSettingsPages = {"general", "appearance", "performance", "editing", "audio", "ai", "export", "about"};
} // namespace

void AppController::setScreen(const QString& value)
{
    if ((value != QLatin1String("home") && value != QLatin1String("editor")) || value == m_screen)
        return;
    m_screen = value;
    emit screenChanged();
}

void AppController::setWorkspace(const QString& value)
{
    if (!kWorkspaces.contains(value) || value == m_workspace)
        return;
    m_workspace = value;
    emit workspaceChanged();
}

void AppController::setSettingsOpen(bool value)
{
    if (value == m_settingsOpen)
        return;
    m_settingsOpen = value;
    emit settingsOpenChanged();
}

void AppController::setSettingsPage(const QString& value)
{
    if (!kSettingsPages.contains(value) || value == m_settingsPage)
        return;
    m_settingsPage = value;
    emit settingsPageChanged();
}

void AppController::setNewProjectOpen(bool value)
{
    if (value == m_newProjectOpen)
        return;
    m_newProjectOpen = value;
    emit newProjectOpenChanged();
}

void AppController::setAboutOpen(bool value)
{
    if (value == m_aboutOpen)
        return;
    m_aboutOpen = value;
    emit aboutOpenChanged();
}

void AppController::restart()
{
    qCInfo(lcApp) << "Restart requested";
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
    QCoreApplication::quit();
}

void AppController::quit()
{
    QCoreApplication::quit();
}

} // namespace dv
