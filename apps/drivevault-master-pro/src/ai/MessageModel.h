#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>

namespace dm::ai {

// The messages of the open conversation as a list MODEL for the page. The page used to get a new JavaScript array on every change, so the list rebuilt all its
// boxes (a flicker, and the scroll position jumped); this model only tells about the rows that really changed, so the boxes stay and only their contents change.
// One role, "modelData": the row as a map (the same map that AiChannel::messages() has).
class MessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit MessageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Makes the model equal to `rows` of the conversation `conversationId`: another conversation is a reset, the same one gives dataChanged for the rows that
    // differ and inserted / removed rows at the end. Returns how many rows were reported as changed or inserted (tested).
    int sync(const QString& conversationId, const QVariantList& rows);

private:
    QVariantList m_rows;
    QString m_id;
};

} // namespace dm::ai
