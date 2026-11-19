#include "AdBlockController.h"

namespace dv::br {

AdBlockController::AdBlockController(BrowserSettings* settings, AdBlockInterceptor* interceptor, StandInHandler* standIns, const QString& dataDir, const QString& shippedDir, QObject* parent)
    : QObject(parent), m_settings(settings), m_interceptor(interceptor), m_standIns(standIns), m_lists(dataDir, shippedDir)
{
    m_interceptor->setManager(&m_lists);
    connect(&m_lists, &ab::ListManager::statusChanged, this, &AdBlockController::changed);
    connect(&m_lists, &ab::ListManager::statusChanged, this, [this] { ++m_tick; emit tickChanged(); });
    connect(m_settings, &BrowserSettings::changed, this, [this] { applySettings(); });
    m_poll.setInterval(700);                                      // the network thread only counts; this tells the pages
    connect(&m_poll, &QTimer::timeout, this, [this] {
        const int total = m_interceptor->blockedTotal();
        if (total != m_lastTotal) {
            m_lastTotal = total;
            ++m_tick;
            emit tickChanged();
        }
    });
    m_poll.start();
}

AdBlockController::~AdBlockController()
{
    m_interceptor->setManager(nullptr);                           // the network thread must not reach a deleted manager
}

QStringList AdBlockController::enabledIds() const
{
    QStringList ids;
    for (const ab::ListDef& d : m_lists.catalog())
        if (m_settings->listOn(d.id))
            ids << d.id;
    return ids;
}

void AdBlockController::applySettings()
{
    m_interceptor->setEnabled(m_settings->adblockEnabled());
    const QStringList paused = m_settings->pausedHosts();
    m_interceptor->setPausedHosts(QSet<QString>(paused.begin(), paused.end()));
    m_lists.setEnabled(enabledIds());
}

void AdBlockController::start()
{
    applySettings();
    m_lists.reload();
}

void AdBlockController::attach(QQuickWebEngineProfile* profile)
{
    if (!profile)
        return;
    profile->setUrlRequestInterceptor(m_interceptor);
    if (m_standIns)
        profile->installUrlSchemeHandler(QByteArrayLiteral("dvres"), m_standIns);
}

QString AdBlockController::scriptFor(const QString& pageUrl) const
{
    if (!m_settings->adblockEnabled() || m_library.isEmpty())
        return {};
    if (m_settings->adblockPaused(m_lists.hostOfUrl(pageUrl)))
        return {};
    return m_lists.pageScriptFor(m_library, pageUrl, m_settings->cosmeticEnabled(), m_settings->antiDetectEnabled());
}

void AdBlockController::setEnabled(bool on) { m_settings->setAdblockEnabled(on); }

void AdBlockController::setPaused(const QString& host, bool paused)
{
    m_settings->setAdblockPaused(host.toLower(), paused);
    ++m_tick;
    emit tickChanged();
}

void AdBlockController::setListEnabled(const QString& id, bool on) { m_settings->setListOn(id, on); }

} // namespace dv::br
