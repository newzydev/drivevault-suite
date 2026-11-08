#include "TrackListModel.h"

#include <algorithm>

namespace dv {

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_tracks.size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return {};
    const TrackInfo& t = m_tracks.at(index.row());
    switch (role) {
    case IdRole: return t.id;
    case NameRole:
    case Qt::DisplayRole: return t.name;
    case KindRole: return t.kind;
    case MutedRole: return t.muted;
    case LockedRole: return t.locked;
    case VisibleRole: return t.visible;
    case GainRole: return t.gainDb;
    case PanRole: return t.pan;
    case SoloRole: return t.solo;
    case LaneRole: return t.lane;
    case EqLowRole: return t.eqLow;
    case EqMidRole: return t.eqMid;
    case EqHighRole: return t.eqHigh;
    default: return {};
    }
}

bool TrackListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return false;
    TrackInfo& t = m_tracks[index.row()];
    switch (role) {
    case NameRole: {
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || name == t.name)
            return false;
        t.name = name.left(40);
        break;
    }
    case MutedRole:
        if (t.muted == value.toBool())
            return false;
        t.muted = value.toBool();
        break;
    case LockedRole:
        if (t.locked == value.toBool())
            return false;
        t.locked = value.toBool();
        break;
    case VisibleRole:
        if (t.visible == value.toBool())
            return false;
        t.visible = value.toBool();
        break;
    case SoloRole:
        if (t.solo == value.toBool())
            return false;
        t.solo = value.toBool();
        break;
    case LaneRole: {
        const int lane = std::clamp(value.toInt(), 0, 2);
        if (t.lane == lane)
            return false;
        t.lane = lane;
        break;
    }
    case GainRole: {
        const double db = std::clamp(value.toDouble(), -60.0, 12.0);
        if (qFuzzyCompare(t.gainDb + 100, db + 100))
            return false;
        t.gainDb = db;
        break;
    }
    case PanRole: {
        const double pan = std::clamp(value.toDouble(), -1.0, 1.0);
        if (qFuzzyCompare(t.pan + 100, pan + 100))
            return false;
        t.pan = pan;
        break;
    }
    case EqLowRole:
    case EqMidRole:
    case EqHighRole: {
        double& target = role == EqLowRole ? t.eqLow : role == EqMidRole ? t.eqMid : t.eqHigh;
        const double db = std::clamp(value.toDouble(), -18.0, 18.0);
        if (qFuzzyCompare(target + 100, db + 100))
            return false;
        target = db;
        break;
    }
    default:
        return false;
    }
    emit dataChanged(index, index, {role});
    emit userChanged();
    return true;
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {{IdRole, "trackId"}, {NameRole, "name"}, {KindRole, "kind"}, {MutedRole, "muted"}, {LockedRole, "locked"},
            {VisibleRole, "trackVisible"}, {GainRole, "gainDb"}, {PanRole, "pan"}, {SoloRole, "solo"}, {LaneRole, "lane"},
            {EqLowRole, "eqLow"}, {EqMidRole, "eqMid"}, {EqHighRole, "eqHigh"}};
}

void TrackListModel::setTracks(const QList<TrackInfo>& tracks)
{
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
    emit countChanged();
}

void TrackListModel::toggle(int row, const QString& property)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    const QModelIndex idx = index(row);
    if (property == QLatin1String("muted"))
        setData(idx, !m_tracks.at(row).muted, MutedRole);
    else if (property == QLatin1String("locked"))
        setData(idx, !m_tracks.at(row).locked, LockedRole);
    else if (property == QLatin1String("visible"))
        setData(idx, !m_tracks.at(row).visible, VisibleRole);
    else if (property == QLatin1String("solo"))
        setData(idx, !m_tracks.at(row).solo, SoloRole);
}

void TrackListModel::setName(int row, const QString& name) { setData(index(row), name, NameRole); }
void TrackListModel::setGain(int row, double db) { setData(index(row), db, GainRole); }
void TrackListModel::setPan(int row, double pan) { setData(index(row), pan, PanRole); }

void TrackListModel::setEq(int row, const QString& band, double db)
{
    const int role = band == QLatin1String("low") ? EqLowRole : band == QLatin1String("mid") ? EqMidRole : band == QLatin1String("high") ? EqHighRole : -1;
    if (role >= 0)
        setData(index(row), db, role);
}

QString TrackListModel::nextId(const QString& kind) const
{
    const QString prefix = kind == QLatin1String("video") ? QStringLiteral("V")
                           : kind == QLatin1String("audio") ? QStringLiteral("A") : QStringLiteral("S");
    for (int n = 1;; ++n) {
        const QString id = prefix + QString::number(n);
        const bool taken = std::any_of(m_tracks.begin(), m_tracks.end(), [&](const TrackInfo& t) { return t.id == id; });
        if (!taken)
            return id;
    }
}

int TrackListModel::addTrack(const QString& kind)
{
    if (kind != QLatin1String("video") && kind != QLatin1String("audio") && kind != QLatin1String("subtitle"))
        return -1;
    if (kind == QLatin1String("subtitle")
        && std::any_of(m_tracks.begin(), m_tracks.end(), [](const TrackInfo& t) { return t.kind == QLatin1String("subtitle"); }))
        return -1;
    if (m_tracks.size() >= 64)
        return -1;

    TrackInfo t;
    t.id = nextId(kind);
    t.name = t.id;
    t.kind = kind;

    int row = 0;                                          // subtitle: top
    if (kind == QLatin1String("video")) {
        row = 0;
        while (row < m_tracks.size() && m_tracks.at(row).kind == QLatin1String("subtitle"))
            ++row;                                        // above the other video tracks, below subtitles
    } else if (kind == QLatin1String("audio")) {
        row = int(m_tracks.size());                       // bottom
    }
    beginInsertRows({}, row, row);
    m_tracks.insert(row, t);
    endInsertRows();
    emit countChanged();
    emit userChanged();
    return row;
}

bool TrackListModel::canRemove(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return false;
    const QString kind = m_tracks.at(row).kind;
    const auto sameKind = std::count_if(m_tracks.begin(), m_tracks.end(), [&](const TrackInfo& t) { return t.kind == kind; });
    return sameKind > 1 || kind == QLatin1String("subtitle");     // a project always keeps at least one video and one audio track
}

bool TrackListModel::removeTrack(int row)
{
    if (!canRemove(row))
        return false;
    beginRemoveRows({}, row, row);
    m_tracks.removeAt(row);
    endRemoveRows();
    emit countChanged();
    emit userChanged();
    return true;
}

} // namespace dv
