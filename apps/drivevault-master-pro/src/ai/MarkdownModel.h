#pragma once

#include "MarkdownHtml.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

namespace dm::ai {

// One answer of the AI as a list MODEL of blocks (see markdownBlocks) for the page: QML type `MarkdownModel`, one per answer on the screen.
// While an answer is written its text grows many times a second; the model compares the new blocks with the old ones and reports only the rows that really
// changed (usually the last one), so the blocks that are finished are not rebuilt (no flicker, little work). One role, "block": the block as a map.
class MarkdownModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged FINAL)
    Q_PROPERTY(QVariantMap style READ style WRITE setStyle NOTIFY styleChanged FINAL)         // {codeBg, text, link: "#rrggbb", fontPx}
public:
    explicit MarkdownModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString markdown() const { return m_markdown; }
    void setMarkdown(const QString& text);
    QVariantMap style() const { return m_style; }
    void setStyle(const QVariantMap& style);
    int lastTouched() const { return m_touched; }                  // rows reported as changed / inserted / removed by the last update (tested)

signals:
    void markdownChanged();
    void styleChanged();

private:
    void rebuild();

    QString m_markdown;
    QVariantMap m_style;
    QVariantList m_rows;
    int m_touched = 0;
};

} // namespace dm::ai
