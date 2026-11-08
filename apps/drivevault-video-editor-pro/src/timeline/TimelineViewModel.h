#pragma once

#include <QSortFilterProxyModel>

namespace dv {

// The clips that the timeline panel has to draw: only those that touch the visible part of the timeline (plus a margin).
// The panel creates one QML delegate per row of this model, so a project with thousands of clips costs as much as the few that are on screen.
class TimelineViewModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(double firstFrame READ firstFrame WRITE setFirstFrame NOTIFY windowChanged FINAL)
    Q_PROPERTY(double lastFrame READ lastFrame WRITE setLastFrame NOTIFY windowChanged FINAL)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)
public:
    explicit TimelineViewModel(QObject* parent = nullptr);

    QObject* source() const { return sourceModel(); }
    void setSource(QObject* model);
    double firstFrame() const { return m_first; }
    void setFirstFrame(double f);
    double lastFrame() const { return m_last; }
    void setLastFrame(double f);

signals:
    void sourceChanged();
    void windowChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& parent) const override;

private:
    double m_first = 0;
    double m_last = 1e12;
};

} // namespace dv
