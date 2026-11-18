#include "MarkdownModel.h"

namespace dm::ai {

MarkdownModel::MarkdownModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

QVariant MarkdownModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || role != Qt::UserRole + 1)
        return {};
    return m_rows.at(index.row());
}

QHash<int, QByteArray> MarkdownModel::roleNames() const { return {{Qt::UserRole + 1, "block"}}; }

void MarkdownModel::setMarkdown(const QString& text)
{
    if (text == m_markdown)
        return;
    m_markdown = text;
    rebuild();
    emit markdownChanged();
}

void MarkdownModel::setStyle(const QVariantMap& style)
{
    if (style == m_style)
        return;
    m_style = style;
    rebuild();
    emit styleChanged();
}

void MarkdownModel::rebuild()
{
    MarkdownStyle st;
    if (const QString v = m_style.value(QStringLiteral("codeBg")).toString(); !v.isEmpty())
        st.codeBg = v;
    if (const QString v = m_style.value(QStringLiteral("text")).toString(); !v.isEmpty())
        st.text = v;
    if (const QString v = m_style.value(QStringLiteral("link")).toString(); !v.isEmpty())
        st.link = v;
    st.fontPx = m_style.value(QStringLiteral("fontPx"), st.fontPx).toInt();
    const QVariantList rows = markdownBlocks(m_markdown, st);

    m_touched = 0;
    const int common = int(qMin(m_rows.size(), rows.size()));
    for (int i = 0; i < common; ++i) {
        if (m_rows.at(i) == rows.at(i))
            continue;
        m_rows[i] = rows.at(i);
        emit dataChanged(index(i), index(i));
        ++m_touched;
    }
    if (rows.size() < m_rows.size()) {
        beginRemoveRows({}, int(rows.size()), int(m_rows.size()) - 1);
        m_touched += int(m_rows.size() - rows.size());
        m_rows = rows;
        endRemoveRows();
    } else if (rows.size() > m_rows.size()) {
        beginInsertRows({}, int(m_rows.size()), int(rows.size()) - 1);
        m_touched += int(rows.size() - m_rows.size());
        m_rows = rows;
        endInsertRows();
    }
}

} // namespace dm::ai
