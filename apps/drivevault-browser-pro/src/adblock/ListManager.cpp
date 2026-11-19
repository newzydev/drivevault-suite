#include "ListManager.h"

#include "FilterParser.h"
#include "PageScript.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QVariantMap>

namespace dv::br::ab {

namespace {
constexpr qint64 kMaxListBytes = 60 * 1024 * 1024;
}

QVector<ListDef> ListManager::defaultCatalog()
{
    // Official addresses of the lists, read ONLY when the user presses "Update filter lists". Licences: see docs/THIRD-PARTY-NOTICES (checked before every release).
    return {
        {QStringLiteral("easylist"), QStringLiteral("EasyList"), QStringLiteral("ads"), QStringLiteral("https://easylist.to/easylist/easylist.txt"), QStringLiteral("GPLv3+ / CC BY-SA 3.0")},
        {QStringLiteral("easyprivacy"), QStringLiteral("EasyPrivacy"), QStringLiteral("tracking"), QStringLiteral("https://easylist.to/easylist/easyprivacy.txt"), QStringLiteral("GPLv3+ / CC BY-SA 3.0")},
        {QStringLiteral("fanboy-social"), QStringLiteral("Fanboy's Social Blocking List"), QStringLiteral("social"), QStringLiteral("https://easylist.to/easylist/fanboy-social.txt"), QStringLiteral("GPLv3+ / CC BY-SA 3.0")},
        {QStringLiteral("fanboy-annoyance"), QStringLiteral("Fanboy's Annoyance List"), QStringLiteral("annoyances"), QStringLiteral("https://easylist.to/easylist/fanboy-annoyance.txt"), QStringLiteral("GPLv3+ / CC BY-SA 3.0")},
    };
}

ListManager::ListManager(const QString& dataDir, const QString& shippedDir, QObject* parent)
    : QObject(parent), m_dataDir(dataDir), m_shippedDir(shippedDir), m_catalog(defaultCatalog())
{
    m_set.store(std::make_shared<const RuleSet>());
    m_cos.store(std::make_shared<const CosmeticStore>());
    m_noGeneric.store(std::make_shared<const RuleSet>());
    m_noCosmetic.store(std::make_shared<const RuleSet>());
    m_generic.store(std::make_shared<const Generic>());
    m_stats.resize(m_catalog.size());
}

quint32 ListManager::maskOf(const QStringList& ids) const
{
    quint32 mask = 0;
    for (int i = 0; i < m_catalog.size() && i < 32; ++i)
        if (ids.contains(m_catalog.at(i).id))
            mask |= 1u << i;
    return mask;
}

void ListManager::setEnabled(const QStringList& ids)
{
    m_enabled = ids;
    m_mask.store(maskOf(ids));
    refreshGeneric();
    emit statusChanged();
}

QStringList ListManager::enabled() const { return m_enabled; }

int ListManager::totalRules() const
{
    int n = 0;
    for (int i = 0; i < m_stats.size() && i < 32; ++i)
        if (m_mask.load() & (1u << i))
            n += m_stats.at(i).rules;
    return n;
}

ListManager::Built ListManager::build(const QVector<ListDef>& catalog, const QString& dataDir, const QString& shippedDir)
{
    Built out;
    out.stats.resize(catalog.size());
    auto set = std::make_shared<RuleSet>();
    auto cos = std::make_shared<CosmeticStore>();
    auto noGeneric = std::make_shared<RuleSet>();
    auto noCosmetic = std::make_shared<RuleSet>();
    for (int i = 0; i < catalog.size() && i < 32; ++i) {
        const ListDef& def = catalog.at(i);
        QString path = dataDir + QStringLiteral("/filters/") + def.id + QStringLiteral(".txt");
        if (!QFileInfo::exists(path) && !shippedDir.isEmpty())
            path = shippedDir + QLatin1Char('/') + def.id + QStringLiteral(".txt");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        Stats& st = out.stats[i];
        st.present = true;
        st.bytes = f.size();
        st.date = QFileInfo(f).lastModified().toString(Qt::ISODate);
        QFile meta(dataDir + QStringLiteral("/filters/") + def.id + QStringLiteral(".meta.json"));
        if (meta.open(QIODevice::ReadOnly) && path.startsWith(dataDir))
            st.date = QJsonDocument::fromJson(meta.readAll()).object().value(QStringLiteral("date")).toString(st.date);
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine());
            NetRule rule;
            switch (parseLine(line, rule)) {
            case ParseKind::Network:
                rule.list = quint8(i);
                set->add(rule);
                ++st.rules;
                break;
            case ParseKind::Cosmetic: {
                CosmeticRule cr;
                if (parseCosmetic(line, cr) == ParseKind::Cosmetic) {
                    cr.list = quint8(i);
                    cos->add(cr);
                    if (cr.kind == CosmeticKind::Scriptlet && !cr.exception)
                        ++st.scriptlets;
                    else
                        ++st.cosmetic;
                } else {
                    ++st.unsupported;
                }
                break;
            }
            case ParseKind::CosmeticSite:
                rule.list = quint8(i);
                (rule.cosmeticOff == 2 ? noCosmetic : noGeneric)->add(rule);
                if (rule.cosmeticOff == 2)
                    noGeneric->add(rule);                                // $elemhide also means no generic hiding
                break;
            case ParseKind::Unsupported: ++st.unsupported; break;
            case ParseKind::Comment: break;
            }
        }
    }
    set->finalize();
    cos->finalize();
    noGeneric->finalize();
    noCosmetic->finalize();
    out.set = set;
    out.cosmetics = cos;
    out.noGeneric = noGeneric;
    out.noCosmetic = noCosmetic;
    return out;
}

void ListManager::reload()
{
    if (m_loading) {
        m_reloadAgain = true;
        return;
    }
    m_loading = true;
    m_phase = QStringLiteral("loading");
    emit statusChanged();
    const QVector<ListDef> catalog = m_catalog;
    const QString data = m_dataDir, shipped = m_shippedDir;
    QPointer<ListManager> self(this);
    QThreadPool::globalInstance()->start([self, catalog, data, shipped] {
        Built b = build(catalog, data, shipped);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, b]() mutable {
            if (!self)
                return;
            self->m_set.store(b.set);
            self->m_cos.store(b.cosmetics);
            self->m_noGeneric.store(b.noGeneric);
            self->m_noCosmetic.store(b.noCosmetic);
            self->m_stats = b.stats;
            self->refreshGeneric();
            self->m_loading = false;
            self->m_phase = QStringLiteral("idle");
            emit self->statusChanged();
            emit self->reloaded();
            if (self->m_reloadAgain) {
                self->m_reloadAgain = false;
                self->reload();
            }
        }, Qt::QueuedConnection);
    });
}

ListManager::Decision ListManager::match(const QString& url, const QString& sourceUrl, quint16 type) const
{
    Decision out;
    const std::shared_ptr<const RuleSet> set = m_set.load();
    const quint32 mask = m_mask.load();
    if (!set || mask == 0)
        return out;
    if (set->siteExempt(sourceUrl, mask))
        return out;
    const RuleSet::Decision d = set->match(url, sourceUrl, type, mask);
    if (d.blocked && d.rule) {
        out.blocked = true;
        out.rule = d.rule->raw;
        out.redirect = d.rule->redirect;
        out.listId = d.rule->list < m_catalog.size() ? m_catalog.at(d.rule->list).id : QString();
    } else if (d.exception) {
        out.allowedByException = true;
        out.rule = d.exception->raw;
    }
    return out;
}

bool ListManager::siteExempt(const QString& sourceUrl) const
{
    const std::shared_ptr<const RuleSet> set = m_set.load();
    return set && set->siteExempt(sourceUrl, m_mask.load());
}

void ListManager::refreshGeneric()
{
    const std::shared_ptr<const CosmeticStore> cos = m_cos.load();
    auto g = std::make_shared<Generic>();
    if (cos) {
        g->classes = cos->genericClasses(m_mask.load());
        g->ids = cos->genericIds(m_mask.load());
    }
    m_generic.store(g);
}

int ListManager::cosmeticRuleCount() const
{
    int n = 0;
    for (int i = 0; i < m_stats.size() && i < 32; ++i)
        if (m_mask.load() & (1u << i))
            n += m_stats.at(i).cosmetic + m_stats.at(i).scriptlets;
    return n;
}

PageCosmetics ListManager::cosmeticsFor(const QString& pageUrl) const
{
    PageCosmetics out;
    const QString lower = pageUrl.toLower();
    if (!lower.startsWith(QLatin1String("http://")) && !lower.startsWith(QLatin1String("https://")))
        return out;
    const QString host = RuleSet::hostOf(lower);
    const std::shared_ptr<const CosmeticStore> cos = m_cos.load();
    const quint32 mask = m_mask.load();
    if (!cos || host.isEmpty() || mask == 0)
        return out;
    const std::shared_ptr<const RuleSet> noCos = m_noCosmetic.load(), noGen = m_noGeneric.load();
    const bool noCosmetic = noCos && noCos->siteExempt(pageUrl, mask);
    const bool noGeneric = noCosmetic || (noGen && noGen->siteExempt(pageUrl, mask));
    out = cos->forPage(host, mask, noGeneric);
    out.noCosmetic = noCosmetic;
    out.noGeneric = noGeneric;
    return out;
}

QString ListManager::pageScriptFor(const QString& libraryJs, const QString& pageUrl, bool cosmetic, bool defend) const
{
    const QString lower = pageUrl.toLower();
    const bool http = lower.startsWith(QLatin1String("http://")) || lower.startsWith(QLatin1String("https://"));
    if (!http)
        return {};
    PageCosmetics page;
    QString classes, ids;
    if (cosmetic && m_mask.load() != 0) {
        page = cosmeticsFor(pageUrl);
        if (const std::shared_ptr<const Generic> g = m_generic.load()) {
            classes = g->classes;
            ids = g->ids;
        }
    }
    return buildPageScript(libraryJs, page, classes, ids, defend);
}
QVariantList ListManager::status() const
{
    QVariantList out;
    for (int i = 0; i < m_catalog.size(); ++i) {
        const ListDef& d = m_catalog.at(i);
        const Stats st = i < m_stats.size() ? m_stats.at(i) : Stats();
        out.append(QVariantMap{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name}, {QStringLiteral("category"), d.category}, {QStringLiteral("licence"), d.licence},
                               {QStringLiteral("present"), st.present}, {QStringLiteral("enabled"), m_enabled.contains(d.id)}, {QStringLiteral("rules"), st.rules},
                               {QStringLiteral("cosmetic"), st.cosmetic}, {QStringLiteral("scriptlets"), st.scriptlets}, {QStringLiteral("unsupported"), st.unsupported}, {QStringLiteral("date"), st.date}, {QStringLiteral("bytes"), st.bytes}});
    }
    return out;
}

// ---- update (only when the user pressed the button) -------------------------------------------------------------------------

void ListManager::update(const QStringList& ids)
{
    if (m_updating)
        return;
    m_queue.clear();
    for (const ListDef& d : m_catalog)
        if (ids.isEmpty() ? m_enabled.contains(d.id) : ids.contains(d.id))
            m_queue << d.id;
    if (m_queue.isEmpty()) {
        m_phase = QStringLiteral("nothingOn");
        emit statusChanged();
        return;
    }
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
        m_net->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
    m_updating = true;
    m_updated = m_failed = 0;
    m_failures.clear();
    QDir().mkpath(filtersDir());
    startNextDownload();
}

void ListManager::startNextDownload()
{
    if (m_queue.isEmpty()) {
        finishDownloads();
        return;
    }
    const QString id = m_queue.takeFirst();
    ListDef def;
    for (const ListDef& d : m_catalog)
        if (d.id == id)
            def = d;
    m_phase = QStringLiteral("updating"); m_current = def.name;
    emit statusChanged();
    const QUrl url(def.url);
    if (url.scheme() != QLatin1String("https") && !(m_allowHttp && url.scheme() == QLatin1String("http"))) {
        ++m_failed;
        m_failures << QStringLiteral("%1: the address is not https").arg(def.name);
        startNextDownload();
        return;
    }
    QNetworkRequest req(url);
    req.setTransferTimeout(60000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DriveVault-Browser-Pro"));
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 got, qint64) {
        if (got > kMaxListBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, def] {
        reply->deleteLater();
        const QByteArray body = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString problem;
        if (code != 0 && code != 200)
            problem = QStringLiteral("HTTP %1").arg(code);
        else if (reply->error() != QNetworkReply::NoError)
            problem = reply->errorString();
        else if (body.isEmpty() || body.size() > kMaxListBytes)
            problem = QStringLiteral("empty or too big");
        else if (!body.left(4000).contains("[Adblock") && !body.left(4000).contains("! "))
            problem = QStringLiteral("this is not a filter list");
        if (!problem.isEmpty()) {
            ++m_failed;
            m_failures << QStringLiteral("%1: %2").arg(def.name, problem);
        } else {
            QSaveFile out(filtersDir() + QLatin1Char('/') + def.id + QStringLiteral(".txt"));
            bool ok = out.open(QIODevice::WriteOnly) && out.write(body) == body.size() && out.commit();
            if (ok) {
                QSaveFile meta(filtersDir() + QLatin1Char('/') + def.id + QStringLiteral(".meta.json"));
                if (meta.open(QIODevice::WriteOnly)) {
                    meta.write(QJsonDocument(QJsonObject{{QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate)}, {QStringLiteral("bytes"), double(body.size())},
                                                         {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex())},
                                                         {QStringLiteral("url"), def.url}}).toJson());
                    meta.commit();
                }
                ++m_updated;
            } else {
                ++m_failed;
                m_failures << QStringLiteral("%1: the file could not be saved").arg(def.name);
            }
        }
        startNextDownload();
    });
}

void ListManager::finishDownloads()
{
    m_updating = false;
    m_phase = QStringLiteral("idle"); const QString summary = QStringLiteral("%1 updated, %2 failed").arg(m_updated).arg(m_failed);
    emit statusChanged();
    emit updateFinished(summary);
    if (m_updated > 0)
        reload();
}

} // namespace dv::br::ab

