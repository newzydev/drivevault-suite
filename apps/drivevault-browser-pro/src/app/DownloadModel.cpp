#include "DownloadModel.h"

#include "UrlUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QtWebEngineQuick/QQuickWebEngineDownloadRequest>

namespace dv::br {

DownloadModel::DownloadModel(QObject* parent) : QAbstractListModel(parent) {}

QString DownloadModel::folder() const
{
    return m_folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : m_folder;
}

int DownloadModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(m_entries.size()); }

QVariant DownloadModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry& e = m_entries.at(index.row());
    switch (role) {
    case IdRole: return e.id;
    case NameRole: return e.name;
    case PathRole: return e.path;
    case ReceivedRole: return e.received;
    case TotalRole: return e.total;
    case StateRole: return e.state;
    case RiskyRole: return e.risky;
    case UnitRole: return e.unit;
    case PausableRole: return !e.external;
    case ErrorRole: return e.error;
    }
    return {};
}

QHash<int, QByteArray> DownloadModel::roleNames() const
{
    return {{IdRole, "id"}, {NameRole, "name"}, {PathRole, "path"}, {ReceivedRole, "received"}, {TotalRole, "total"}, {StateRole, "state"}, {RiskyRole, "risky"}, {UnitRole, "unit"}, {PausableRole, "pausable"}, {ErrorRole, "error"}};
}

int DownloadModel::activeCount() const
{
    int n = 0;
    for (const Entry& e : m_entries)
        if (e.state == QLatin1String("running") || e.state == QLatin1String("paused"))
            ++n;
    return n;
}

int DownloadModel::rowOfId(int id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == id)
            return i;
    return -1;
}

void DownloadModel::handle(QQuickWebEngineDownloadRequest* request)
{
    if (!request)
        return;
    const QString dir = folder();
    QDir().mkpath(dir);
    const QString suggested = request->suggestedFileName().isEmpty() ? request->downloadFileName() : request->suggestedFileName();
    const QString name = uniqueFileName(dir, suggested);
    request->setDownloadDirectory(dir);
    request->setDownloadFileName(name);

    Entry e;
    e.id = m_nextId++;
    e.name = name;
    e.path = QDir(dir).filePath(name);
    e.state = QStringLiteral("running");
    e.total = request->totalBytes();
    e.risky = riskyFileName(name);
    e.request = request;
    beginInsertRows({}, 0, 0);
    m_entries.prepend(e);
    endInsertRows();
    emit activeCountChanged();

    const int id = e.id;
    auto update = [this, id] { refresh(id); };
    connect(request, &QQuickWebEngineDownloadRequest::stateChanged, this, update);
    connect(request, &QQuickWebEngineDownloadRequest::receivedBytesChanged, this, update);
    connect(request, &QQuickWebEngineDownloadRequest::totalBytesChanged, this, update);
    connect(request, &QQuickWebEngineDownloadRequest::isPausedChanged, this, update);
    request->accept();
}

void DownloadModel::refresh(int id)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    Entry& e = m_entries[row];
    if (!e.request)
        return;
    e.received = e.request->receivedBytes();
    e.total = e.request->totalBytes();
    switch (e.request->state()) {
    case QQuickWebEngineDownloadRequest::DownloadCompleted: e.state = QStringLiteral("done"); break;
    case QQuickWebEngineDownloadRequest::DownloadCancelled: e.state = QStringLiteral("cancelled"); break;
    case QQuickWebEngineDownloadRequest::DownloadInterrupted: e.state = QStringLiteral("failed"); break;
    default: e.state = e.request->isPaused() ? QStringLiteral("paused") : QStringLiteral("running"); break;
    }
    emit dataChanged(index(row), index(row));
    emit activeCountChanged();
}

void DownloadModel::cancel(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    if (m_entries.at(row).request)
        m_entries.at(row).request->cancel();
    else if (m_entries.at(row).cancelFn)
        m_entries.at(row).cancelFn();
}

QVariantMap DownloadModel::get(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    const Entry& e = m_entries.at(row);
    return {{QStringLiteral("name"), e.name}, {QStringLiteral("path"), e.path}, {QStringLiteral("state"), e.state}, {QStringLiteral("received"), e.received},
            {QStringLiteral("total"), e.total}, {QStringLiteral("unit"), e.unit}, {QStringLiteral("error"), e.error}};
}

int DownloadModel::addExternal(const QString& name, const QString& path, std::function<void()> cancel)
{
    Entry e;
    e.id = m_nextId++;
    e.name = name;
    e.path = path;
    e.state = QStringLiteral("running");
    e.external = true;
    e.cancelFn = std::move(cancel);
    beginInsertRows({}, 0, 0);
    m_entries.prepend(e);
    endInsertRows();
    emit activeCountChanged();
    return e.id;
}

void DownloadModel::updateExternal(int id, qint64 received, qint64 total, const QString& unit)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    Entry& e = m_entries[row];
    e.received = received;
    e.total = total;
    e.unit = unit;
    emit dataChanged(index(row), index(row));
}

void DownloadModel::finishExternal(int id, bool ok, const QString& path, const QString& error)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    Entry& e = m_entries[row];
    e.state = ok ? QStringLiteral("done") : (error == QLatin1String("cancelled") ? QStringLiteral("cancelled") : QStringLiteral("failed"));
    e.error = ok ? QString() : error;
    if (ok && !path.isEmpty()) {
        e.path = path;
        e.name = QFileInfo(path).fileName();
    }
    e.cancelFn = nullptr;
    emit dataChanged(index(row), index(row));
    emit activeCountChanged();
}

void DownloadModel::pause(int row)
{
    if (row >= 0 && row < m_entries.size() && m_entries.at(row).request)
        m_entries.at(row).request->pause();
}

void DownloadModel::resume(int row)
{
    if (row >= 0 && row < m_entries.size() && m_entries.at(row).request)
        m_entries.at(row).request->resume();
}

void DownloadModel::showInFolder(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const QString path = QDir::toNativeSeparators(m_entries.at(row).path);
    if (QFile::exists(path))
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + path});
    else
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(folder())});
}

void DownloadModel::remove(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit activeCountChanged();
}

void DownloadModel::clearFinished()
{
    for (int i = int(m_entries.size()) - 1; i >= 0; --i) {
        const QString& s = m_entries.at(i).state;
        if (s != QLatin1String("running") && s != QLatin1String("paused")) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
        }
    }
    emit activeCountChanged();
}

} // namespace dv::br
