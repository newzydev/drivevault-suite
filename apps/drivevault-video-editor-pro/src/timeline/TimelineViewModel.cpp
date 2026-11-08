#include "TimelineViewModel.h"

#include "TimelineModel.h"

namespace dv {

TimelineViewModel::TimelineViewModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &TimelineViewModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TimelineViewModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TimelineViewModel::countChanged);
}

void TimelineViewModel::setSource(QObject* model)
{
    auto* m = qobject_cast<QAbstractItemModel*>(model);
    if (m == sourceModel())
        return;
    setSourceModel(m);
    emit sourceChanged();
}

void TimelineViewModel::setFirstFrame(double f)
{
    if (f == m_first)
        return;
    m_first = f;
    invalidateFilter();
    emit windowChanged();
}

void TimelineViewModel::setLastFrame(double f)
{
    if (f == m_last)
        return;
    m_last = f;
    invalidateFilter();
    emit windowChanged();
}

bool TimelineViewModel::filterAcceptsRow(int sourceRow, const QModelIndex& parent) const
{
    const QAbstractItemModel* src = sourceModel();
    if (!src)
        return false;
    const QModelIndex i = src->index(sourceRow, 0, parent);
    const double start = src->data(i, TimelineModel::StartRole).toDouble();
    const double end = start + src->data(i, TimelineModel::DurationRole).toDouble();
    return end >= m_first && start <= m_last;
}

} // namespace dv
