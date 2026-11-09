#define _USE_MATH_DEFINES
#include "EditorModel.h"

#include "DocumentController.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QFontDatabase>
#include <QImageReader>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace dp {

using tools::EditItem;
using tools::Kind;
using tools::MarkDef;

namespace {

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Text: return QStringLiteral("text");
    case Kind::Image: return QStringLiteral("image");
    case Kind::Rect: return QStringLiteral("rect");
    case Kind::Ellipse: return QStringLiteral("ellipse");
    case Kind::Line: return QStringLiteral("line");
    case Kind::Arrow: return QStringLiteral("arrow");
    case Kind::Pen: return QStringLiteral("pen");
    case Kind::Highlight: return QStringLiteral("highlight");
    case Kind::Underline: return QStringLiteral("underline");
    case Kind::Strike: return QStringLiteral("strike");
    case Kind::Stamp: return QStringLiteral("stamp");
    }
    return {};
}

bool kindFromTool(const QString& tool, Kind& out)
{
    static const QList<Kind> all = {Kind::Text, Kind::Image, Kind::Rect, Kind::Ellipse, Kind::Line, Kind::Arrow,
                                    Kind::Pen, Kind::Highlight, Kind::Underline, Kind::Strike, Kind::Stamp};
    for (Kind k : all)
        if (kindName(k) == tool) {
            out = k;
            return true;
        }
    return false;
}

bool isLine(Kind k) { return k == Kind::Line || k == Kind::Arrow; }

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF d = b - a;
    const double len2 = d.x() * d.x() + d.y() * d.y();
    double t = len2 > 0 ? ((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF c = a + d * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

QPointF unrotate(const QPointF& p, const QPointF& center, double angle)
{
    if (angle == 0.0)
        return p;
    const double a = -angle * M_PI / 180.0;
    const QPointF d = p - center;
    return center + QPointF(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
}

QString localOf(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

MarkDef::Type markType(const QString& t, bool* ok = nullptr)
{
    if (ok)
        *ok = true;
    if (t == QLatin1String("watermark")) return MarkDef::Watermark;
    if (t == QLatin1String("pageNumber")) return MarkDef::PageNumber;
    if (t == QLatin1String("header")) return MarkDef::Header;
    if (t == QLatin1String("footer")) return MarkDef::Footer;
    if (ok)
        *ok = false;
    return MarkDef::Watermark;
}

} // namespace

EditorModel::EditorModel(DocumentController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    m_style.color = QColor(0, 0, 0);
    m_style.width = 2;
    m_style.fontSize = 16;
    m_style.opacity = 1.0;
    // the fonts of the program first, then the others installed
    m_fonts = {QStringLiteral("Noto Sans Thai"), QStringLiteral("Inter")};
    for (const QString& f : QFontDatabase::families())
        if (!m_fonts.contains(f))
            m_fonts.append(f);
    connect(controller, &DocumentController::documentChanged, this, [this] {
        m_selected = 0;
        m_editing = 0;
        ++m_rev;
        emit selectionChanged();
        emit editingChanged();
        emit changed();
    });
    connect(controller, &DocumentController::tabsChanged, this, [this] { pruneClosedTabs(); });
}

EditorModel::~EditorModel()
{
    if (m_cancel)
        m_cancel->store(true);
    QThreadPool::globalInstance()->waitForDone(15000);
}

void EditorModel::pruneClosedTabs()
{
    QMutexLocker lock(&m_mutex);
    QSet<int> open;
    for (const QVariant& v : m_controller->tabs())
        open.insert(v.toMap().value(QStringLiteral("id")).toInt());
    bool removed = false;
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (!open.contains(it->first)) {
            it = m_states.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed)
        emit changed();
}

EditorModel::State* EditorModel::state()
{
    const int id = m_controller->docId();
    if (id <= 0 || !m_controller->isOpen())
        return nullptr;
    return &m_states[id];
}

const EditorModel::State* EditorModel::state() const
{
    const int id = m_controller->docId();
    const auto it = m_states.find(id);
    return it == m_states.end() ? nullptr : &it->second;
}

EditItem* EditorModel::find(int id)
{
    if (State* s = state())
        for (EditItem& it : s->now.items)
            if (it.id == id)
                return &it;
    return nullptr;
}

const EditItem* EditorModel::find(int id) const
{
    if (const State* s = state())
        for (const EditItem& it : s->now.items)
            if (it.id == id)
                return &it;
    return nullptr;
}

int EditorModel::count() const
{
    QMutexLocker lock(&m_mutex);
    const State* s = state();
    return s ? int(s->now.items.size() + s->now.marks.size()) : 0;
}
bool EditorModel::canUndo() const
{
    QMutexLocker lock(&m_mutex);
    const State* s = state();
    return s && !s->undo.empty();
}
bool EditorModel::canRedo() const
{
    QMutexLocker lock(&m_mutex);
    const State* s = state();
    return s && !s->redo.empty();
}
bool EditorModel::modified() const
{
    QMutexLocker lock(&m_mutex);
    const State* s = state();
    return s && s->dirty && (!s->now.items.isEmpty() || !s->now.marks.isEmpty());
}
bool EditorModel::anyModified() const
{
    QMutexLocker lock(&m_mutex);
    for (const auto& [id, s] : m_states)
        if (s.dirty && (!s.now.items.isEmpty() || !s.now.marks.isEmpty()))
            return true;
    return false;
}

void EditorModel::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    if (!on) {
        endEdit();
        m_selected = 0;
        emit selectionChanged();
    }
    emit activeChanged();
}

void EditorModel::setTool(const QString& tool)
{
    if (m_tool == tool)
        return;
    endEdit();
    m_tool = tool;
    if (tool != QLatin1String("select")) {
        m_selected = 0;
        emit selectionChanged();
    }
    emit toolChanged();
}

void EditorModel::setPendingImage(const QString& pathOrUrl)
{
    m_pendingImage = pathOrUrl.isEmpty() ? QString() : localOf(pathOrUrl);
    emit toolChanged();
}

void EditorModel::pushUndo()
{
    State* s = state();
    if (!s)
        return;
    if (m_inGesture) {
        if (m_gestureSnapshot)
            return;
        m_gestureSnapshot = true;
    }
    s->undo.push_back(s->now);
    if (s->undo.size() > 200)
        s->undo.erase(s->undo.begin());
    s->redo.clear();
}

void EditorModel::touch()
{
    if (State* s = state())
        s->dirty = true;
    ++m_rev;
    emit changed();
}

void EditorModel::beginGesture()
{
    m_inGesture = true;
    m_gestureSnapshot = false;
}
void EditorModel::endGesture()
{
    m_inGesture = false;
    m_gestureSnapshot = false;
}

EditItem EditorModel::newItem(int page, Kind kind) const
{
    EditItem it = m_style;
    it.kind = kind;
    it.page = page;
    it.id = 0;
    it.rect = QRectF();
    it.a = it.b = QPointF();
    it.points.clear();
    it.text.clear();
    it.imagePath.clear();
    it.angle = 0;
    if (kind != Kind::Rect && kind != Kind::Ellipse && kind != Kind::Text && kind != Kind::Stamp)
        it.fill = QColor();
    if (kind == Kind::Highlight) {
        if (m_style.color == QColor(0, 0, 0))
            it.color = QColor(255, 235, 0);
        it.opacity = 0.4 * m_style.opacity;
    }
    if (kind == Kind::Stamp) {
        it.color = m_style.color == QColor(0, 0, 0) ? QColor(200, 30, 30) : m_style.color;
        it.angle = -12;
    }
    return it;
}

// ---- style -----------------------------------------------------------------------------------------------------------------------

void EditorModel::applyStyleTo(EditItem& it, const QString& key, const QVariant& value)
{
    if (key == QLatin1String("color")) it.color = value.value<QColor>();
    else if (key == QLatin1String("fill")) it.fill = value.value<QColor>();
    else if (key == QLatin1String("noFill")) it.fill = QColor();
    else if (key == QLatin1String("width")) it.width = std::clamp(value.toDouble(), 0.0, 60.0);
    else if (key == QLatin1String("opacity")) it.opacity = std::clamp(value.toDouble(), 0.05, 1.0);
    else if (key == QLatin1String("font")) it.font = value.toString();
    else if (key == QLatin1String("size")) it.fontSize = std::clamp(value.toDouble(), 4.0, 400.0);
    else if (key == QLatin1String("bold")) it.bold = value.toBool();
    else if (key == QLatin1String("italic")) it.italic = value.toBool();
    else if (key == QLatin1String("align")) it.align = std::clamp(value.toInt(), 0, 2);
}

void EditorModel::loadStyleFrom(const EditItem& it)
{
    m_style.color = it.color;
    m_style.fill = it.fill;
    m_style.width = it.width;
    m_style.opacity = it.opacity;
    if (it.kind == Kind::Text || it.kind == Kind::Stamp) {
        m_style.font = it.font;
        m_style.fontSize = it.fontSize;
        m_style.bold = it.bold;
        m_style.italic = it.italic;
        m_style.align = it.align;
    }
}

void EditorModel::setStyle(const QString& key, const QVariant& value)
{
    QMutexLocker lock(&m_mutex);
    applyStyleTo(m_style, key, value);
    if (EditItem* it = find(m_selected)) {
        pushUndo();
        it = find(m_selected);
        applyStyleTo(*it, key, value);
        touch();
    }
    emit styleChanged();
}

// ---- selection ---------------------------------------------------------------------------------------------------------------------

void EditorModel::select(int id)
{
    QMutexLocker lock(&m_mutex);
    if (id == m_selected)
        return;
    endEdit();
    m_selected = id;
    if (const EditItem* it = find(id))
        loadStyleFrom(*it);
    emit selectionChanged();
    emit styleChanged();
}

int EditorModel::hitTest(int page, double x, double y) const
{
    QMutexLocker lock(&m_mutex);
    const State* s = state();
    if (!s)
        return 0;
    const QPointF p(x, y);
    for (int i = int(s->now.items.size()) - 1; i >= 0; --i) {
        const EditItem& it = s->now.items[i];
        if (it.page != page)
            continue;
        if (isLine(it.kind)) {
            if (distanceToSegment(p, it.a, it.b) <= std::max(5.0, it.width / 2 + 3))
                return it.id;
        } else if (it.kind == Kind::Pen) {
            const double tol = std::max(5.0, it.width / 2 + 3);
            if (it.points.size() == 1 && std::hypot(x - it.points[0].x(), y - it.points[0].y()) <= tol)
                return it.id;
            for (int k = 1; k < it.points.size(); ++k)
                if (distanceToSegment(p, it.points[k - 1], it.points[k]) <= tol)
                    return it.id;
        } else {
            QRectF box = it.bounds();
            if (it.kind == Kind::Underline || it.kind == Kind::Strike)
                box.adjust(0, -3, 0, 3);
            if (box.contains(unrotate(p, box.center(), it.angle)))
                return it.id;
        }
    }
    return 0;
}

QVariantMap EditorModel::itemInfo(int id) const
{
    QMutexLocker lock(&m_mutex);
    const EditItem* it = find(id);
    if (!it)
        return {};
    const QRectF b = it->bounds();
    QVariantList handles;
    const bool turned = std::fmod(it->angle, 360.0) != 0.0;
    if (!turned) {
        if (isLine(it->kind)) {
            handles << QVariantMap{{"x", it->a.x()}, {"y", it->a.y()}} << QVariantMap{{"x", it->b.x()}, {"y", it->b.y()}};
        } else {
            const double xs[3] = {b.left(), b.center().x(), b.right()};
            const double ys[3] = {b.top(), b.center().y(), b.bottom()};
            const int order[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
            for (const auto& o : order)
                handles << QVariantMap{{"x", xs[o[0]]}, {"y", ys[o[1]]}};
        }
    }
    return {{"id", it->id},       {"kind", kindName(it->kind)}, {"page", it->page},    {"x", b.x()},         {"y", b.y()},
            {"w", b.width()},     {"h", b.height()},            {"angle", it->angle},  {"text", it->text},   {"handles", handles}};
}

int EditorModel::handleAt(int page, double x, double y, double tolerance) const
{
    const QVariantMap info = itemInfo(m_selected);
    if (info.isEmpty() || info.value("page").toInt() != page)
        return -1;
    const QVariantList handles = info.value("handles").toList();
    int best = -1;
    double bestDist = tolerance;
    for (int i = 0; i < handles.size(); ++i) {
        const QVariantMap h = handles[i].toMap();
        const double d = std::hypot(x - h.value("x").toDouble(), y - h.value("y").toDouble());
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

// ---- creating -----------------------------------------------------------------------------------------------------------------------

int EditorModel::beginDraft(int page, const QString& tool, double x, double y)
{
    QMutexLocker lock(&m_mutex);
    Kind kind;
    State* s = state();
    if (!s || !kindFromTool(tool, kind) || kind == Kind::Image)
        return 0;
    endEdit();
    beginGesture();
    pushUndo();
    EditItem it = newItem(page, kind);
    it.id = s->nextId++;
    m_draftStart = QPointF(x, y);
    if (isLine(kind)) {
        it.a = it.b = m_draftStart;
    } else if (kind == Kind::Pen) {
        it.points = {m_draftStart};
    } else {
        it.rect = QRectF(m_draftStart, QSizeF(0, 0));
    }
    s->now.items.append(it);
    m_draft = it.id;
    m_selected = it.id;
    touch();
    emit selectionChanged();
    return it.id;
}

void EditorModel::updateDraft(double x, double y)
{
    QMutexLocker lock(&m_mutex);
    EditItem* it = find(m_draft);
    if (!it)
        return;
    const QPointF p(x, y);
    if (isLine(it->kind))
        it->b = p;
    else if (it->kind == Kind::Pen) {
        const QPointF last = it->points.last();
        if (std::hypot(p.x() - last.x(), p.y() - last.y()) >= 0.8)
            it->points.append(p);
    } else {
        it->rect = QRectF(m_draftStart, p).normalized();
    }
    touch();
}

bool EditorModel::endDraft()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    EditItem* it = find(m_draft);
    const int id = m_draft;
    m_draft = 0;
    if (!s || !it) {
        endGesture();
        return false;
    }
    bool keep = true;
    if (it->kind == Kind::Text) {
        if (it->rect.width() < 20 || it->rect.height() < 12)
            it->rect = QRectF(it->rect.topLeft(), QSizeF(std::max(220.0, it->rect.width()), it->fontSize * 1.6 + 6));
    } else if (it->kind == Kind::Stamp) {
        it->rect = QRectF(m_draftStart, QSizeF(std::max(160.0, it->rect.width()), std::max(40.0, it->fontSize * 2.2)));
    } else if (isLine(it->kind)) {
        keep = std::hypot(it->b.x() - it->a.x(), it->b.y() - it->a.y()) >= 3;
    } else if (it->kind == Kind::Pen) {
        keep = !it->points.isEmpty();
    } else if (it->kind == Kind::Underline || it->kind == Kind::Strike) {
        keep = it->rect.width() >= 3;
    } else {
        keep = it->rect.width() >= 3 && it->rect.height() >= 3;
    }
    if (it->kind == Kind::Stamp && it->text.isEmpty())
        it->text = QStringLiteral("APPROVED");
    if (!keep) {
        s->now = s->undo.back();                 // as if it had never been started
        s->undo.pop_back();
        m_selected = 0;
        emit selectionChanged();
    } else {
        m_selected = id;
    }
    endGesture();
    touch();
    return keep;
}

QString EditorModel::addImage(int page, const QString& pathOrUrl, double x, double y)
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s)
        return QStringLiteral("file");
    const QString path = localOf(pathOrUrl);
    if (!QFileInfo::exists(path))
        return QStringLiteral("file");
    QImageReader reader(path);
    const QSize px = reader.size();
    if (!reader.canRead() || px.isEmpty())
        return QStringLiteral("format");
    const double scale = std::min(1.0, 240.0 / std::max(px.width(), px.height()));
    const QSizeF size(std::max(8.0, px.width() * scale), std::max(8.0, px.height() * scale));
    pushUndo();
    EditItem it = newItem(page, Kind::Image);
    it.id = s->nextId++;
    it.imagePath = path;
    it.rect = QRectF(QPointF(x - size.width() / 2, y - size.height() / 2), size);
    s->now.items.append(it);
    m_selected = it.id;
    touch();
    emit selectionChanged();
    return {};
}

int EditorModel::addStamp(int page, double x, double y, const QString& text)
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s)
        return 0;
    pushUndo();
    EditItem it = newItem(page, Kind::Stamp);
    it.id = s->nextId++;
    it.text = text.isEmpty() ? QStringLiteral("APPROVED") : text;
    it.bold = true;
    it.fontSize = std::max(m_style.fontSize, 22.0);
    it.rect = QRectF(x - 90, y - 24, 180, 48);
    s->now.items.append(it);
    m_selected = it.id;
    touch();
    emit selectionChanged();
    return it.id;
}

// ---- editing --------------------------------------------------------------------------------------------------------------------------

void EditorModel::moveSelected(double dx, double dy)
{
    QMutexLocker lock(&m_mutex);
    EditItem* it = find(m_selected);
    if (!it)
        return;
    pushUndo();
    it = find(m_selected);
    const QPointF d(dx, dy);
    it->rect.translate(d);
    it->a += d;
    it->b += d;
    for (QPointF& p : it->points)
        p += d;
    touch();
}

void EditorModel::dragHandle(int handle, double x, double y)
{
    QMutexLocker lock(&m_mutex);
    EditItem* it = find(m_selected);
    if (!it || handle < 0)
        return;
    pushUndo();
    it = find(m_selected);
    const QPointF p(x, y);
    if (isLine(it->kind)) {
        (handle == 0 ? it->a : it->b) = p;
        touch();
        return;
    }
    const QRectF old = it->bounds();
    double l = old.left(), t = old.top(), r = old.right(), b = old.bottom();
    // 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
    if (handle == 0 || handle == 6 || handle == 7) l = x;
    if (handle == 2 || handle == 3 || handle == 4) r = x;
    if (handle == 0 || handle == 1 || handle == 2) t = y;
    if (handle == 4 || handle == 5 || handle == 6) b = y;
    if (it->kind == Kind::Image && handle % 2 == 0 && old.width() > 0 && old.height() > 0) {
        // pictures keep their proportions when a corner is dragged
        const double ratio = old.width() / old.height();
        const double w = std::abs(r - l);
        const double hgt = w / ratio;
        if (handle == 0 || handle == 2) t = b - hgt; else b = t + hgt;
    }
    QRectF nr = QRectF(QPointF(l, t), QPointF(r, b)).normalized();
    if (nr.width() < 4) nr.setWidth(4);
    if (nr.height() < 4) nr.setHeight(4);
    if (it->kind == Kind::Pen) {
        for (QPointF& pt : it->points) {
            const double fx = old.width() > 0 ? (pt.x() - old.left()) / old.width() : 0.5;
            const double fy = old.height() > 0 ? (pt.y() - old.top()) / old.height() : 0.5;
            pt = QPointF(nr.left() + fx * nr.width(), nr.top() + fy * nr.height());
        }
    } else {
        it->rect = nr;
    }
    touch();
}

void EditorModel::rotateSelected(double degrees)
{
    QMutexLocker lock(&m_mutex);
    EditItem* it = find(m_selected);
    if (!it || isLine(it->kind) || it->kind == Kind::Pen)
        return;
    pushUndo();
    it = find(m_selected);
    it->angle = std::fmod(it->angle + degrees, 360.0);
    touch();
    emit selectionChanged();
}

void EditorModel::beginEdit(int id)
{
    QMutexLocker lock(&m_mutex);
    const EditItem* it = find(id);
    if (!it || (it->kind != Kind::Text && it->kind != Kind::Stamp))
        return;
    m_selected = id;
    m_editing = id;
    ++m_rev;
    emit selectionChanged();
    emit editingChanged();
    emit changed();
}

void EditorModel::endEdit()
{
    QMutexLocker lock(&m_mutex);
    if (m_editing == 0)
        return;
    m_editing = 0;
    ++m_rev;
    emit editingChanged();
    emit changed();
}

void EditorModel::setText(int id, const QString& text)
{
    QMutexLocker lock(&m_mutex);
    EditItem* it = find(id);
    if (!it || it->text == text)
        return;
    pushUndo();
    it = find(id);                      // after the snapshot: the list was shared with it
    it->text = text;
    touch();
}

void EditorModel::deleteSelected()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s)
        return;
    for (int i = 0; i < s->now.items.size(); ++i)
        if (s->now.items[i].id == m_selected) {
            endEdit();
            pushUndo();
            s->now.items.removeAt(i);
            m_selected = 0;
            touch();
            emit selectionChanged();
            return;
        }
}

void EditorModel::toFront()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s)
        return;
    for (int i = 0; i + 1 < s->now.items.size(); ++i)
        if (s->now.items[i].id == m_selected) {
            pushUndo();
            s->now.items.move(i, int(s->now.items.size()) - 1);
            touch();
            return;
        }
}

void EditorModel::toBack()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s)
        return;
    for (int i = 1; i < s->now.items.size(); ++i)
        if (s->now.items[i].id == m_selected) {
            pushUndo();
            s->now.items.move(i, 0);
            touch();
            return;
        }
}

void EditorModel::undo()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s || s->undo.empty())
        return;
    endEdit();
    s->redo.push_back(s->now);
    s->now = s->undo.back();
    s->undo.pop_back();
    if (!find(m_selected)) {
        m_selected = 0;
        emit selectionChanged();
    }
    touch();
}

void EditorModel::redo()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s || s->redo.empty())
        return;
    endEdit();
    s->undo.push_back(s->now);
    s->now = s->redo.back();
    s->redo.pop_back();
    if (!find(m_selected)) {
        m_selected = 0;
        emit selectionChanged();
    }
    touch();
}

void EditorModel::discardEdits()
{
    QMutexLocker lock(&m_mutex);
    State* s = state();
    if (!s)
        return;
    endEdit();
    *s = State();
    m_selected = 0;
    emit selectionChanged();
    touch();
    s->dirty = false;
    emit changed();
}

// ---- marks --------------------------------------------------------------------------------------------------------------------------

QVariantMap EditorModel::markDefaults(const QString& type) const
{
    QVariantMap m{{"font", "Noto Sans Thai"}, {"size", 12}, {"bold", false}, {"italic", false}, {"color", QColor(60, 60, 60)},
                  {"opacity", 1.0}, {"align", 1}, {"from", 1}, {"to", 0}, {"text", ""}};
    if (type == QLatin1String("watermark")) {
        m["text"] = QStringLiteral("DRAFT");
        m["size"] = 72;
        m["bold"] = true;
        m["color"] = QColor(128, 128, 128);
        m["opacity"] = 0.3;
    } else if (type == QLatin1String("pageNumber")) {
        m["text"] = QStringLiteral("{n} / {total}");
    }
    return m;
}

void EditorModel::setMark(const QString& type, const QVariantMap& in)
{
    QMutexLocker lock(&m_mutex);
    bool ok = false;
    const MarkDef::Type t = markType(type, &ok);
    State* s = state();
    if (!ok || !s)
        return;
    QVariantMap m = markDefaults(type);
    for (auto it = in.begin(); it != in.end(); ++it)
        m[it.key()] = it.value();
    if (m.value("text").toString().trimmed().isEmpty()) {
        removeMark(type);
        return;
    }
    MarkDef d;
    d.type = t;
    d.text = m.value("text").toString();
    d.font = m.value("font").toString();
    d.fontSize = std::clamp(m.value("size").toDouble(), 4.0, 400.0);
    d.bold = m.value("bold").toBool();
    d.italic = m.value("italic").toBool();
    d.color = m.value("color").value<QColor>();
    d.opacity = std::clamp(m.value("opacity").toDouble(), 0.05, 1.0);
    d.align = std::clamp(m.value("align").toInt(), 0, 2);
    d.fromPage = std::max(0, m.value("from").toInt() - 1);
    d.toPage = m.value("to").toInt() <= 0 ? -1 : m.value("to").toInt() - 1;
    pushUndo();
    s->now.marks.erase(std::remove_if(s->now.marks.begin(), s->now.marks.end(), [t](const MarkDef& o) { return o.type == t; }), s->now.marks.end());
    s->now.marks.append(d);
    touch();
}

void EditorModel::removeMark(const QString& type)
{
    QMutexLocker lock(&m_mutex);
    bool ok = false;
    const MarkDef::Type t = markType(type, &ok);
    State* s = state();
    if (!ok || !s)
        return;
    const int before = int(s->now.marks.size());
    if (std::none_of(s->now.marks.begin(), s->now.marks.end(), [t](const MarkDef& o) { return o.type == t; }))
        return;
    pushUndo();
    s->now.marks.erase(std::remove_if(s->now.marks.begin(), s->now.marks.end(), [t](const MarkDef& o) { return o.type == t; }), s->now.marks.end());
    Q_UNUSED(before);
    touch();
}

QVariantMap EditorModel::mark(const QString& type) const
{
    QMutexLocker lock(&m_mutex);
    bool ok = false;
    const MarkDef::Type t = markType(type, &ok);
    const State* s = state();
    if (!ok || !s)
        return {};
    for (const MarkDef& d : s->now.marks)
        if (d.type == t)
            return {{"text", d.text}, {"font", d.font}, {"size", d.fontSize}, {"bold", d.bold}, {"italic", d.italic}, {"color", d.color},
                    {"opacity", d.opacity}, {"align", d.align}, {"from", d.fromPage + 1}, {"to", d.toPage < 0 ? 0 : d.toPage + 1}};
    return {};
}

// ---- result ---------------------------------------------------------------------------------------------------------------------------

QStringList EditorModel::restrictedFontNames() const
{
    QMutexLocker lock(&m_mutex);
    const State* s = state();
    return s ? tools::restrictedFonts(s->now.items, s->now.marks) : QStringList();
}

QList<EditItem> EditorModel::itemsForPage(int tabId, int page, const QSizeF& size, int pageCount, int* hiddenId) const
{
    QMutexLocker lock(&m_mutex);
    QList<EditItem> out;
    if (hiddenId)
        *hiddenId = m_editing;
    const auto it = m_states.find(tabId);
    if (it == m_states.end())
        return out;
    // marks lie under the user's own items
    out = tools::marksForPage(it->second.now.marks, page, pageCount, size);
    for (const EditItem& item : it->second.now.items)
        if (item.page == page)
            out.append(item);
    return out;
}

QString EditorModel::save(const QString& outPath, bool overwrite)
{
    QMutexLocker lock(&m_mutex);
    const DocumentController::SourceInfo src = m_controller->currentSource();
    const State* s = state();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    if (!s || (s->now.items.isEmpty() && s->now.marks.isEmpty()))
        return QStringLiteral("noEdits");
    const QString path = QDir::toNativeSeparators(QFileInfo(outPath).absoluteFilePath());
    if (QFileInfo(path) == QFileInfo(src.path))
        return QStringLiteral("sameFile");
    if (!overwrite && QFileInfo::exists(path))
        return QStringLiteral("exists");
    if (!tools::restrictedFonts(s->now.items, s->now.marks).isEmpty())
        return QStringLiteral("fontRestricted");

    QList<tools::PageEdits> pages;
    const int n = src.doc->pageCount();
    for (int p = 0; p < n; ++p) {
        tools::PageEdits pe;
        pe.page = p;
        pe.size = src.doc->pageSizePt(p, 0);
        pe.items = tools::marksForPage(s->now.marks, p, n, pe.size);
        for (const EditItem& item : s->now.items)
            if (item.page == p)
                pe.items.append(item);
        if (!pe.items.isEmpty())
            pages.append(pe);
    }
    const int tabId = src.id;

    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    m_busy = true;
    m_progress = 0.0;
    emit savingChanged();
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, path, overwrite, pages, cancel, tabId] {
        const tools::EditResult r = tools::saveWithEdits(src.path, src.password, path, pages, overwrite, [this, cancel](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] { m_progress = total > 0 ? double(done) / total : 0.0; emit savingChanged(); },
                                      Qt::QueuedConnection);
            return !cancel->load();
        });
        QMetaObject::invokeMethod(this, [this, r, path, tabId] {
            m_busy = false;
            m_progress = r.ok ? 1.0 : m_progress;
            if (r.ok) {
                QMutexLocker lock(&m_mutex);
                if (auto it = m_states.find(tabId); it != m_states.end())
                    it->second.dirty = false;
                emit changed();
            }
            emit savingChanged();
            emit saveFinished(r.ok, r.error, r.detail, r.warnings, r.pages, double(r.bytes), path);
        }, Qt::QueuedConnection);
    }));
    return {};
}

void EditorModel::cancelSave()
{
    if (m_cancel)
        m_cancel->store(true);
}

} // namespace dp

namespace dp {

bool EditorModel::modifiedFor(int tabId) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_states.find(tabId);
    return it != m_states.end() && it->second.dirty && (!it->second.now.items.isEmpty() || !it->second.now.marks.isEmpty());
}

} // namespace dp
