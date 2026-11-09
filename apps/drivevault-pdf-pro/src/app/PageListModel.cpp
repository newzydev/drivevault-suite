#include "PageListModel.h"

#include "DocumentController.h"
#include "pdftools/PdfTools.h"

#include <QDir>
#include <QFileInfo>
#include <QRunnable>
#include <QSet>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>

namespace dp {

namespace {
constexpr size_t kMaxUndo = 200;

QString errorCode(pdf::OpenError e)
{
    switch (e) {
    case pdf::OpenError::Password: return QStringLiteral("password");
    case pdf::OpenError::Format: return QStringLiteral("format");
    case pdf::OpenError::File: return QStringLiteral("file");
    case pdf::OpenError::TooLarge: return QStringLiteral("tooLarge");
    default: return QStringLiteral("format");
    }
}
} // namespace

PageListModel::PageListModel(DocumentController* controller, QObject* parent)
    : QAbstractListModel(parent)
    , m_controller(controller)
{
}

PageListModel::~PageListModel()
{
    if (m_cancel)
        m_cancel->store(true);
    QThreadPool::globalInstance()->waitForDone(15000);
    for (const Source& s : m_sources)
        if (s.owned)
            m_controller->unregisterExtra(s.id);
}

QHash<int, QByteArray> PageListModel::roleNames() const
{
    return {{SourceIdRole, "sourceId"}, {PageRole, "page"}, {RotationRole, "rotation"}, {WidthRole, "widthPt"}, {HeightRole, "heightPt"},
            {LabelRole, "label"}, {BlankRole, "blank"}, {CropRole, "crop"}, {FileRole, "file"}};
}

QSizeF PageListModel::shownSize(const Item& item) const
{
    const int turns = ((item.rotation % 4) + 4) % 4;
    if (item.blank)
        return (turns % 2) ? QSizeF(item.blankSize.height(), item.blankSize.width()) : item.blankSize;
    if (item.source < 0 || item.source >= int(m_sources.size()))
        return QSizeF(595, 842);
    return m_sources[size_t(item.source)].doc->pageSizePt(item.page, item.rotation);
}

QVariant PageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_items.size()))
        return {};
    const Item& it = m_items[size_t(index.row())];
    const Source* src = (!it.blank && it.source >= 0 && it.source < int(m_sources.size())) ? &m_sources[size_t(it.source)] : nullptr;
    switch (role) {
    case SourceIdRole: return src ? src->id : 0;
    case PageRole: return it.page;
    case RotationRole: return ((it.rotation % 4) + 4) % 4;
    case WidthRole: return shownSize(it).width();
    case HeightRole: return shownSize(it).height();
    case LabelRole: return src ? QStringLiteral("%1 · %2").arg(src->name).arg(it.page + 1) : QString();
    case BlankRole: return it.blank;
    case CropRole:
        return QVariantMap{{QStringLiteral("left"), it.crop.left()}, {QStringLiteral("top"), it.crop.top()},
                           {QStringLiteral("right"), it.crop.right()}, {QStringLiteral("bottom"), it.crop.bottom()}};
    case FileRole: return src ? src->name : QString();
    default: return {};
    }
}

bool PageListModel::modified() const { return m_items != m_initial; }

QString PageListModel::baseName() const
{
    if (m_sources.empty())
        return QStringLiteral("merged");
    const QString first = QFileInfo(m_sources.front().path).completeBaseName();
    return m_sources.size() > 1 ? first + QStringLiteral("-merged") : first + QStringLiteral("-edited");
}

double PageListModel::pageWidthAt(int row) const
{
    return (row >= 0 && row < count()) ? shownSize(m_items[size_t(row)]).width() : 0.0;
}
double PageListModel::pageHeightAt(int row) const
{
    return (row >= 0 && row < count()) ? shownSize(m_items[size_t(row)]).height() : 0.0;
}

// ---- helpers ---------------------------------------------------------------------------------------------------------------

int PageListModel::addSource(const std::shared_ptr<pdf::PdfDocument>& doc, int id, const QString& path, const QString& password, bool owned)
{
    Source s;
    s.id = id;
    s.doc = doc;
    s.path = path;
    s.password = password;
    s.owned = owned;
    s.name = QFileInfo(path).fileName();
    m_sources.push_back(std::move(s));
    return int(m_sources.size()) - 1;
}

void PageListModel::historyTouched() { emit historyChanged(); }

void PageListModel::pushUndo()
{
    if (m_inGesture) {
        if (m_gestureSnapshotTaken)
            return;
        m_gestureSnapshotTaken = true;
    }
    m_undo.push_back(m_items);
    if (m_undo.size() > kMaxUndo)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void PageListModel::applyItems(std::vector<Item> items)
{
    const int before = count();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (before != count())
        emit countChanged();
    historyTouched();
}

QList<int> PageListModel::sortedRows(const QVariantList& rows) const
{
    QSet<int> seen;
    QList<int> out;
    for (const QVariant& v : rows) {
        const int r = v.toInt();
        if (r >= 0 && r < count() && !seen.contains(r)) {
            seen.insert(r);
            out.append(r);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---- filling ---------------------------------------------------------------------------------------------------------------

void PageListModel::clear()
{
    for (const Source& s : m_sources)
        if (s.owned)
            m_controller->unregisterExtra(s.id);
    m_sources.clear();
    m_undo.clear();
    m_redo.clear();
    m_initial.clear();
    m_inGesture = false;
    applyItems({});
    emit countChanged();
}

bool PageListModel::loadCurrent()
{
    const DocumentController::SourceInfo cur = m_controller->currentSource();
    if (!cur.doc)
        return false;
    clear();
    const int s = addSource(cur.doc, cur.id, cur.path, cur.password, false);
    std::vector<Item> items;
    for (int p = 0; p < cur.doc->pageCount(); ++p) {
        Item it;
        it.source = s;
        it.page = p;
        items.push_back(it);
    }
    applyItems(std::move(items));
    m_initial = m_items;
    historyTouched();
    return true;
}

QString PageListModel::addFile(const QString& pathOrUrl, const QString& password, int insertAt)
{
    const QString path = QDir::toNativeSeparators(QFileInfo(m_controller->localPath(pathOrUrl)).absoluteFilePath());
    pdf::OpenError error = pdf::OpenError::Unknown;
    // a file that is already a source is not opened again
    int sourceIndex = -1;
    for (size_t i = 0; i < m_sources.size(); ++i)
        if (m_sources[i].path.compare(path, Qt::CaseInsensitive) == 0 && m_sources[i].password == password)
            sourceIndex = int(i);
    if (sourceIndex < 0) {
        auto doc = pdf::PdfDocument::open(path, password, &error);
        if (!doc)
            return errorCode(error);
        sourceIndex = addSource(doc, m_controller->registerExtra(doc), path, password, true);
    }
    pushUndo();
    std::vector<Item> items = m_items;
    const int at = (insertAt < 0 || insertAt > int(items.size())) ? int(items.size()) : insertAt;
    std::vector<Item> added;
    for (int p = 0; p < m_sources[size_t(sourceIndex)].doc->pageCount(); ++p) {
        Item it;
        it.source = sourceIndex;
        it.page = p;
        added.push_back(it);
    }
    items.insert(items.begin() + at, added.begin(), added.end());
    applyItems(std::move(items));
    return {};
}

void PageListModel::addBlank(int insertAt)
{
    pushUndo();
    std::vector<Item> items = m_items;
    const int at = (insertAt < 0 || insertAt > int(items.size())) ? int(items.size()) : insertAt;
    Item blank;
    blank.blank = true;
    const int neighbour = at > 0 ? at - 1 : (at < int(items.size()) ? at : -1);
    if (neighbour >= 0)
        blank.blankSize = shownSize(items[size_t(neighbour)]);
    items.insert(items.begin() + at, blank);
    applyItems(std::move(items));
}

// ---- editing ---------------------------------------------------------------------------------------------------------------

void PageListModel::removeRows(const QVariantList& rows)
{
    const QList<int> sel = sortedRows(rows);
    if (sel.isEmpty())
        return;
    pushUndo();
    std::vector<Item> items;
    QSet<int> gone(sel.begin(), sel.end());
    for (int i = 0; i < count(); ++i)
        if (!gone.contains(i))
            items.push_back(m_items[size_t(i)]);
    applyItems(std::move(items));
}

void PageListModel::rotate(const QVariantList& rows, int turns)
{
    const QList<int> sel = sortedRows(rows);
    if (sel.isEmpty() || (turns % 4) == 0)
        return;
    pushUndo();
    for (int r : sel) {
        Item& it = m_items[size_t(r)];
        it.rotation = (((it.rotation + turns) % 4) + 4) % 4;
        // a crop is given as the page is shown: it turns with the page, so it is cleared by a rotation (the user sets it again)
        it.crop = QMarginsF();
        emit dataChanged(index(r), index(r), {RotationRole, WidthRole, HeightRole, CropRole});
    }
    historyTouched();
}

void PageListModel::duplicate(const QVariantList& rows)
{
    const QList<int> sel = sortedRows(rows);
    if (sel.isEmpty())
        return;
    pushUndo();
    QSet<int> dup(sel.begin(), sel.end());
    std::vector<Item> items;
    for (int i = 0; i < count(); ++i) {
        items.push_back(m_items[size_t(i)]);
        if (dup.contains(i))
            items.push_back(m_items[size_t(i)]);
    }
    applyItems(std::move(items));
}

void PageListModel::moveRows(const QVariantList& rows, int to)
{
    const QList<int> sel = sortedRows(rows);
    if (sel.isEmpty())
        return;
    QSet<int> moving(sel.begin(), sel.end());
    std::vector<Item> rest, block;
    for (int i = 0; i < count(); ++i)
        (moving.contains(i) ? block : rest).push_back(m_items[size_t(i)]);
    const int at = std::clamp(to, 0, int(rest.size()));
    std::vector<Item> items = rest;
    items.insert(items.begin() + at, block.begin(), block.end());
    if (items == m_items)
        return;
    pushUndo();
    applyItems(std::move(items));
}

void PageListModel::crop(const QVariantList& rows, double left, double top, double right, double bottom)
{
    const QList<int> sel = sortedRows(rows);
    if (sel.isEmpty())
        return;
    const QMarginsF m(std::max(0.0, left), std::max(0.0, top), std::max(0.0, right), std::max(0.0, bottom));
    pushUndo();
    for (int r : sel) {
        m_items[size_t(r)].crop = m;
        emit dataChanged(index(r), index(r), {CropRole});
    }
    historyTouched();
}

void PageListModel::beginGesture()
{
    m_inGesture = true;
    m_gestureSnapshotTaken = false;
}

void PageListModel::endGesture()
{
    m_inGesture = false;
    m_gestureSnapshotTaken = false;
}

void PageListModel::moveOne(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= count() || to >= count())
        return;
    pushUndo();
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    Item item = m_items[size_t(from)];
    m_items.erase(m_items.begin() + from);
    m_items.insert(m_items.begin() + to, item);
    endMoveRows();
    historyTouched();
}

void PageListModel::undo()
{
    if (m_undo.empty())
        return;
    m_redo.push_back(m_items);
    std::vector<Item> items = std::move(m_undo.back());
    m_undo.pop_back();
    applyItems(std::move(items));
}

void PageListModel::redo()
{
    if (m_redo.empty())
        return;
    m_undo.push_back(m_items);
    std::vector<Item> items = std::move(m_redo.back());
    m_redo.pop_back();
    applyItems(std::move(items));
}

void PageListModel::reset()
{
    if (m_items == m_initial)
        return;
    pushUndo();
    applyItems(m_initial);
}

// ---- result ----------------------------------------------------------------------------------------------------------------

QString PageListModel::save(const QString& outPath, const QVariantList& rows, bool overwrite)
{
    if (m_busy)
        return QStringLiteral("busy");
    QList<int> sel = rows.isEmpty() ? QList<int>() : sortedRows(rows);
    if (rows.isEmpty())
        for (int i = 0; i < count(); ++i)
            sel.append(i);
    if (sel.isEmpty())
        return QStringLiteral("empty");
    const QString path = QDir::toNativeSeparators(QFileInfo(outPath).absoluteFilePath());
    if (!overwrite && QFileInfo::exists(path))
        return QStringLiteral("exists");

    QList<tools::PageSpec> specs;
    for (int r : sel) {
        const Item& it = m_items[size_t(r)];
        tools::PageSpec s;
        s.rotation = it.rotation;
        s.crop = it.crop;
        s.blank = it.blank;
        if (it.blank) {
            s.blankSize = it.blankSize;
        } else {
            const Source& src = m_sources[size_t(it.source)];
            s.file = src.path;
            s.password = src.password;
            s.page = it.page;
        }
        specs.append(s);
    }

    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    m_busy = true;
    m_progress = 0.0;
    emit savingChanged();
    QThreadPool::globalInstance()->start(QRunnable::create([this, specs, path, overwrite, cancel] {
        const tools::WriteResult r = tools::writePages(specs, path, [this, cancel](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] { m_progress = total > 0 ? double(done) / total : 0.0; emit savingChanged(); },
                                      Qt::QueuedConnection);
            return !cancel->load();
        }, overwrite);
        QMetaObject::invokeMethod(this, [this, r, path] {
            m_busy = false;
            m_progress = r.ok ? 1.0 : m_progress;
            emit savingChanged();
            emit saveFinished(r.ok, r.error, r.errorFile, r.warnings, r.pages, double(r.bytes), path);
        }, Qt::QueuedConnection);
    }));
    return {};
}

void PageListModel::cancelSave()
{
    if (m_cancel)
        m_cancel->store(true);
}

} // namespace dp
