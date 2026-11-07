#pragma once

#include "project/ProjectData.h"

#include <QAbstractListModel>

namespace dv {

// Timeline tracks: name, kind, hide / mute / lock / solo, audio gain and pan. Persisted in the project.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role {
        IdRole = Qt::UserRole + 1, NameRole, KindRole, MutedRole, LockedRole, VisibleRole, GainRole, PanRole, SoloRole
    };

    explicit TrackListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    QList<TrackInfo> tracks() const { return m_tracks; }
    int count() const { return int(m_tracks.size()); }
    void setTracks(const QList<TrackInfo>& tracks);

    // property is one of "muted", "locked", "visible", "solo".
    Q_INVOKABLE void toggle(int row, const QString& property);
    Q_INVOKABLE void setName(int row, const QString& name);
    Q_INVOKABLE void setGain(int row, double db);
    Q_INVOKABLE void setPan(int row, double pan);

    // Adds a track of `kind` ("video" | "audio" | "subtitle"). New video tracks go on top of the video
    // group, audio tracks below the audio group, the subtitle track on top of everything. Returns the new
    // row, or -1 when a track of that kind cannot be added (only one subtitle track).
    int addTrack(const QString& kind);
    // Removes a row. The caller checks that no clip lives on it and that it is not the last of its kind.
    bool removeTrack(int row);

signals:
    void countChanged();
    void userChanged(); // emitted only for edits made through setData / toggle / add / remove

private:
    QString nextId(const QString& kind) const;
    QList<TrackInfo> m_tracks;
};

} // namespace dv
