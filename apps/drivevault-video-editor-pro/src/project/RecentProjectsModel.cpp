#include "RecentProjectsModel.h"

#include "core/AppPaths.h"
#include "core/FileUtils.h"
#include "core/Logger.h"
#include "settings/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace dv {

namespace {
QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
} // namespace

RecentProjectsModel::RecentProjectsModel(SettingsManager* settings, QObject* parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
}

int RecentProjectsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant RecentProjectsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Item& it = m_items.at(index.row());
    switch (role) {
    case NameRole:
    case Qt::DisplayRole: return it.name;
    case PathRole: return QDir::toNativeSeparators(it.path);
    case LastOpenedRole: return it.lastOpened;
    case LastOpenedTextRole: return QLocale().toString(it.lastOpened.toLocalTime(), QLocale::ShortFormat);
    case ExistsRole: return QFileInfo::exists(it.path);
    default: return {};
    }
}

QHash<int, QByteArray> RecentProjectsModel::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {LastOpenedRole, "lastOpened"},
            {LastOpenedTextRole, "lastOpenedText"}, {ExistsRole, "exists"}};
}

bool RecentProjectsModel::load(const QString& file)
{
    m_file = file.isEmpty() ? paths::recentFile() : file;
    beginResetModel();
    m_items.clear();

    QFile f(m_file);
    bool ok = true;
    if (f.exists()) {
        ok = false;
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                ok = true;
                for (const QJsonValue& v : doc.object().value(QStringLiteral("items")).toArray()) {
                    const QJsonObject o = v.toObject();
                    Item it;
                    it.path = o.value(QStringLiteral("path")).toString();
                    it.name = o.value(QStringLiteral("name")).toString();
                    it.lastOpened = QDateTime::fromString(o.value(QStringLiteral("lastOpened")).toString(), Qt::ISODate);
                    if (it.path.isEmpty() || it.name.isEmpty())
                        continue;
                    if (!it.lastOpened.isValid())
                        it.lastOpened = QDateTime::currentDateTimeUtc();
                    m_items.append(it);
                }
            }
        }
        if (!ok)
            qCWarning(lcProject) << "Recent projects file is unreadable, starting with an empty list";
    }
    trimToLimit();
    endResetModel();
    emit countChanged();
    return ok;
}

bool RecentProjectsModel::save()
{
    QJsonArray items;
    for (const Item& it : m_items) {
        items.append(QJsonObject{{"name", it.name}, {"path", it.path},
                                 {"lastOpened", it.lastOpened.toUTC().toString(Qt::ISODate)}});
    }
    const QJsonObject root{{"schemaVersion", 1}, {"items", items}};
    QString error;
    if (!fileutils::writeAtomic(m_file.isEmpty() ? paths::recentFile() : m_file,
                                QJsonDocument(root).toJson(QJsonDocument::Indented), &error)) {
        qCWarning(lcProject) << "Cannot save recent projects:" << error;
        return false;
    }
    return true;
}

void RecentProjectsModel::trimToLimit()
{
    const int limit = m_settings ? m_settings->get(QStringLiteral("recentProjectsLimit")).toInt() : 10;
    while (m_items.size() > limit)
        m_items.removeLast();
}

void RecentProjectsModel::add(const QString& path, const QString& name)
{
    const QString normalized = normalizedPath(path);
    beginResetModel();
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).path.compare(normalized, Qt::CaseInsensitive) == 0) {
            m_items.removeAt(i);
            break;
        }
    }
    m_items.prepend({name, normalized, QDateTime::currentDateTimeUtc()});
    trimToLimit();
    endResetModel();
    emit countChanged();
    save();
}

void RecentProjectsModel::removeAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    save();
}

void RecentProjectsModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
    save();
}

void RecentProjectsModel::refresh()
{
    if (m_items.isEmpty())
        return;
    emit dataChanged(index(0), index(int(m_items.size()) - 1), {LastOpenedTextRole, ExistsRole});
}

bool RecentProjectsModel::existsAt(int row) const
{
    return row >= 0 && row < m_items.size() && QFileInfo::exists(m_items.at(row).path);
}

QString RecentProjectsModel::pathAt(int row) const
{
    return (row >= 0 && row < m_items.size()) ? m_items.at(row).path : QString();
}

} // namespace dv
