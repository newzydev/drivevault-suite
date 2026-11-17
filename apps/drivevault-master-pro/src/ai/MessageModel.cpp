#include "MessageModel.h"

namespace dm::ai {

MessageModel::MessageModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

QVariant MessageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || role != Qt::UserRole + 1)
        return {};
    return m_rows.at(index.row());
}

QHash<int, QByteArray> MessageModel::roleNames() const { return {{Qt::UserRole + 1, "modelData"}}; }

int MessageModel::sync(const QString& conversationId, const QVariantList& rows)
{
    if (conversationId != m_id) {                                      // another conversation: everything is new
        beginResetModel();
        m_rows = rows;
        m_id = conversationId;
        endResetModel();
        return int(rows.size());
    }
    int touched = 0;
    const int common = int(qMin(m_rows.size(), rows.size()));
    for (int i = 0; i < common; ++i) {
        if (m_rows.at(i) == rows.at(i))
            continue;
        m_rows[i] = rows.at(i);
        emit dataChanged(index(i), index(i));
        ++touched;
    }
    if (rows.size() < m_rows.size()) {
        beginRemoveRows({}, int(rows.size()), int(m_rows.size()) - 1);
        m_rows = m_rows.mid(0, int(rows.size()));
        endRemoveRows();
    } else if (rows.size() > m_rows.size()) {
        beginInsertRows({}, int(m_rows.size()), int(rows.size()) - 1);
        touched += int(rows.size() - m_rows.size());
        m_rows = rows;
        endInsertRows();
    }
    return touched;
}

} // namespace dm::ai
