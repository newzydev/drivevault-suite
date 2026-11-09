#include "DocumentController.h"

#include "PageRange.h"
#include "PrintTask.h"
#include "ImageExport.h"
#include "Redaction.h"
#include "pdftools/Security.h"
#include "pdftools/SplitPlan.h"

#include "dp/Version.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutexLocker>
#include <QPrinterInfo>
#include <QRunnable>
#include <QSettings>
#include <QThreadPool>

#include <algorithm>
#include <numeric>

namespace dp {

namespace {
constexpr int kMaxRecent = 10;
constexpr int kMaxRememberedViews = 100;

QString errorCode(pdf::OpenError e)
{
    switch (e) {
    case pdf::OpenError::Password: return QStringLiteral("password");
    case pdf::OpenError::Format: return QStringLiteral("format");
    case pdf::OpenError::File: return QStringLiteral("file");
    case pdf::OpenError::TooLarge: return QStringLiteral("tooLarge");
    default: return QStringLiteral("unknown");
    }
}

QVariantMap rectMap(const QRectF& r)
{
    return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("w"), r.width()}, {QStringLiteral("h"), r.height()}};
}

QSettings iniFile(const QString& dataDir)
{
    return QSettings(dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat);
}
} // namespace

DocumentController::DocumentController(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
    QDir().mkpath(m_dataDir);
    QSettings s = iniFile(m_dataDir);
    m_recent = s.value(QStringLiteral("recentFiles")).toStringList();
    m_dark = s.value(QStringLiteral("darkPages"), false).toBool();
    m_language = s.value(QStringLiteral("language")).toString();
    if (m_language != QLatin1String("th") && m_language != QLatin1String("en"))
        m_language = QLocale::system().language() == QLocale::Thai ? QStringLiteral("th") : QStringLiteral("en");
}

DocumentController::~DocumentController()
{
    // background work (search, annotations, printing) reports back to this object: stop it and wait before the object goes away
    if (m_searchCancel)
        m_searchCancel->store(true);
    if (m_printCancel)
        m_printCancel->store(true);
    if (m_splitCancel)
        m_splitCancel->store(true);
    cancelImageExport();
    m_outputPool.waitForDone();
    QThreadPool::globalInstance()->waitForDone(15000);
}

// ---- small getters ------------------------------------------------------------------------------------------------------

DocumentController::Tab* DocumentController::current()
{
    return (m_currentIndex >= 0 && m_currentIndex < int(m_tabs.size())) ? &m_tabs[size_t(m_currentIndex)] : nullptr;
}
const DocumentController::Tab* DocumentController::current() const
{
    return (m_currentIndex >= 0 && m_currentIndex < int(m_tabs.size())) ? &m_tabs[size_t(m_currentIndex)] : nullptr;
}
std::shared_ptr<pdf::PdfDocument> DocumentController::currentDoc() const
{
    QMutexLocker lock(&m_mutex);
    const Tab* t = current();
    return t ? t->doc : nullptr;
}
int DocumentController::currentRotation() const
{
    QMutexLocker lock(&m_mutex);
    const Tab* t = current();
    return t ? t->rotation : 0;
}

bool DocumentController::isOpen() const { return bool(currentDoc()); }
int DocumentController::pageCount() const
{
    const auto d = currentDoc();
    return d ? d->pageCount() : 0;
}
QString DocumentController::fileName() const
{
    const auto d = currentDoc();
    return d ? QFileInfo(d->path()).fileName() : QString();
}
QString DocumentController::filePath() const
{
    const auto d = currentDoc();
    return d ? d->path() : QString();
}
int DocumentController::docId() const
{
    QMutexLocker lock(&m_mutex);
    const Tab* t = current();
    return t ? t->id : 0;
}
int DocumentController::currentTab() const
{
    QMutexLocker lock(&m_mutex);
    return m_currentIndex;
}
int DocumentController::rotation() const { return currentRotation(); }

QVariantList DocumentController::tabs() const
{
    QMutexLocker lock(&m_mutex);
    QVariantList list;
    for (const Tab& t : m_tabs)
        list.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), QFileInfo(t.doc->path()).fileName()},
                                {QStringLiteral("path"), t.doc->path()}});
    return list;
}

QString DocumentController::version() const { return QString::fromLatin1(version::kVersion); }
QString DocumentController::buildNumber() const { return QString::fromLatin1(version::kBuildNumber); }

void DocumentController::setLanguage(const QString& lang)
{
    if ((lang != QLatin1String("th") && lang != QLatin1String("en")) || lang == m_language)
        return;
    m_language = lang;
    iniFile(m_dataDir).setValue(QStringLiteral("language"), lang);
    emit languageChanged();
}

void DocumentController::setDarkPages(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    iniFile(m_dataDir).setValue(QStringLiteral("darkPages"), dark);
    emit darkPagesChanged();
}

QString DocumentController::localPath(const QString& pathOrUrl) const
{
    if (pathOrUrl.startsWith(QLatin1String("file:")))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

std::shared_ptr<pdf::PdfDocument> DocumentController::documentFor(int id) const
{
    QMutexLocker lock(&m_mutex);
    for (const Tab& t : m_tabs)
        if (t.id == id)
            return t.doc;
    if (const auto it = m_extras.find(id); it != m_extras.end())
        return it->second;
    return nullptr;
}

int DocumentController::rotationFor(int id) const
{
    QMutexLocker lock(&m_mutex);
    for (const Tab& t : m_tabs)
        if (t.id == id)
            return t.rotation;
    return 0;
}

// ---- tabs ------------------------------------------------------------------------------------------------------------------

QString DocumentController::open(const QString& pathOrUrl, const QString& password)
{
    const QString path = QDir::toNativeSeparators(QFileInfo(localPath(pathOrUrl)).absoluteFilePath());
    {
        QMutexLocker lock(&m_mutex);
        for (size_t i = 0; i < m_tabs.size(); ++i)
            if (m_tabs[i].doc->path().compare(path, Qt::CaseInsensitive) == 0) {
                lock.unlock();
                switchTab(int(i));
                addRecent(path);
                return {};
            }
    }
    pdf::OpenError error = pdf::OpenError::Unknown;
    auto doc = pdf::PdfDocument::open(path, password, &error);
    if (!doc)
        return errorCode(error);

    const QVariantMap saved = loadSavedView(path);
    {
        QMutexLocker lock(&m_mutex);
        Tab tab;
        tab.id = m_nextId++;
        tab.doc = doc;
        tab.password = password;
        tab.view = saved;
        tab.rotation = ((saved.value(QStringLiteral("rotation")).toInt() % 4) + 4) % 4;
        m_tabs.push_back(std::move(tab));
        m_currentIndex = int(m_tabs.size()) - 1;
    }
    resetPerDocumentState();
    addRecent(path);
    emit tabsChanged();
    emit documentChanged();
    emit rotationChanged();
    return {};
}

void DocumentController::closeTab(int index)
{
    {
        QMutexLocker lock(&m_mutex);
        if (index < 0 || index >= int(m_tabs.size()))
            return;
        m_tabs.erase(m_tabs.begin() + index);
        if (m_tabs.empty())
            m_currentIndex = -1;
        else if (index < m_currentIndex)
            --m_currentIndex;
        else if (index == m_currentIndex)
            m_currentIndex = std::min(index, int(m_tabs.size()) - 1);
    }
    resetPerDocumentState();
    emit tabsChanged();
    emit documentChanged();
    emit rotationChanged();
}

void DocumentController::close() { closeTab(currentTab()); }

void DocumentController::switchTab(int index)
{
    {
        QMutexLocker lock(&m_mutex);
        if (index < 0 || index >= int(m_tabs.size()) || index == m_currentIndex)
            return;
        m_currentIndex = index;
    }
    resetPerDocumentState();
    emit documentChanged();
    emit rotationChanged();
}

void DocumentController::nextTab(int step)
{
    int n = 0, cur = 0;
    {
        QMutexLocker lock(&m_mutex);
        n = int(m_tabs.size());
        cur = m_currentIndex;
    }
    if (n > 1)
        switchTab(((cur + step) % n + n) % n);
}

double DocumentController::pageWidthPt(int page) const
{
    const auto d = currentDoc();
    return d ? d->pageSizePt(page, currentRotation()).width() : 0.0;
}

double DocumentController::pageHeightPt(int page) const
{
    const auto d = currentDoc();
    return d ? d->pageSizePt(page, currentRotation()).height() : 0.0;
}

void DocumentController::rotateView(int turns)
{
    QString query;
    {
        QMutexLocker lock(&m_mutex);
        Tab* t = current();
        if (!t)
            return;
        t->rotation = (((t->rotation + turns) % 4) + 4) % 4;
        t->view.insert(QStringLiteral("rotation"), t->rotation);
    }
    clearSelection();
    emit rotationChanged();
    // search highlights are stored for one rotation: look again with the same words
    if (!m_lastQuery.text.isEmpty())
        search(m_lastQuery.text, m_lastQuery.matchCase, m_lastQuery.wholeWord, m_lastQuery.startPage);
}

QString DocumentController::viewKey(const QString& path) const
{
    return QString::fromLatin1(QCryptographicHash::hash(path.toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}

QVariantMap DocumentController::loadSavedView(const QString& path) const
{
    QSettings s = iniFile(m_dataDir);
    const QByteArray json = s.value(QStringLiteral("views/") + viewKey(path)).toString().toUtf8();
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

QVariantMap DocumentController::viewState() const
{
    QMutexLocker lock(&m_mutex);
    const Tab* t = current();
    return t ? t->view : QVariantMap();
}

void DocumentController::saveViewState(const QVariantMap& state) { saveViewStateFor(docId(), state); }

void DocumentController::saveViewStateFor(int docId, const QVariantMap& state)
{
    QString path;
    QVariantMap view;
    {
        QMutexLocker lock(&m_mutex);
        Tab* t = nullptr;
        for (Tab& candidate : m_tabs)
            if (candidate.id == docId)
                t = &candidate;
        if (!t)
            return;
        view = state;
        view.insert(QStringLiteral("rotation"), t->rotation);
        t->view = view;
        path = t->doc->path();
    }
    QSettings s = iniFile(m_dataDir);
    const QString key = viewKey(path);
    s.setValue(QStringLiteral("views/") + key, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(view)).toJson(QJsonDocument::Compact)));
    QStringList order = s.value(QStringLiteral("viewOrder")).toStringList();
    order.removeAll(key);
    order.prepend(key);
    while (order.size() > kMaxRememberedViews)
        s.remove(QStringLiteral("views/") + order.takeLast());
    s.setValue(QStringLiteral("viewOrder"), order);
}

void DocumentController::addRecent(const QString& path)
{
    m_recent.removeAll(path);
    m_recent.prepend(path);
    while (m_recent.size() > kMaxRecent)
        m_recent.removeLast();
    saveRecent();
}

void DocumentController::removeRecent(const QString& path)
{
    if (m_recent.removeAll(path) > 0)
        saveRecent();
}

void DocumentController::saveRecent()
{
    iniFile(m_dataDir).setValue(QStringLiteral("recentFiles"), m_recent);
    emit recentFilesChanged();
}

void DocumentController::resetPerDocumentState()
{
    clearSearch();
    clearSelection();
    if (!m_annotations.isEmpty() || m_annotationsLoading) {
        m_annotations.clear();
        m_annotationsLoading = false;
        ++m_annotationsRun;
        emit annotationsChanged();
    }
}

// ---- search ----------------------------------------------------------------------------------------------------------------

int DocumentController::searchCount() const { return int(m_hits.size()); }

void DocumentController::stopSearch()
{
    if (m_searchCancel)
        m_searchCancel->store(true);
    m_searchCancel.reset();
    ++m_searchRun;
}

void DocumentController::clearSearch()
{
    const bool had = !m_hits.empty() || m_searching || m_searchCurrent >= 0 || !m_lastQuery.text.isEmpty();
    stopSearch();
    m_hits.clear();
    m_hitsByPage.clear();
    m_searchCurrent = -1;
    m_searching = false;
    m_searchProgress = 0.0;
    m_lastQuery = {};
    if (had)
        emit searchChanged();
}

void DocumentController::search(const QString& text, bool matchCase, bool wholeWord, int startPage)
{
    stopSearch();
    m_hits.clear();
    m_hitsByPage.clear();
    m_searchCurrent = -1;
    m_searchProgress = 0.0;
    m_lastQuery = {text, matchCase, wholeWord, startPage};
    const auto doc = currentDoc();
    if (text.trimmed().isEmpty() || !doc) {
        m_searching = false;
        m_lastQuery = {};
        emit searchChanged();
        return;
    }
    const int rotation = currentRotation();
    const int run = m_searchRun;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_searchCancel = cancel;
    m_searching = true;
    emit searchChanged();

    QThreadPool::globalInstance()->start(QRunnable::create([this, doc, text, matchCase, wholeWord, startPage, rotation, run, cancel] {
        const int n = doc->pageCount();
        for (int page = 0; page < n && !cancel->load(); ++page) {
            QList<pdf::SearchHit> hits = doc->searchPage(page, text, matchCase, wholeWord, rotation);
            const bool report = !hits.isEmpty() || page % 25 == 24;
            if (!report)
                continue;
            QMetaObject::invokeMethod(this, [this, run, page, n, hits = std::move(hits), startPage] {
                if (run != m_searchRun)
                    return;
                for (const pdf::SearchHit& h : hits) {
                    m_hitsByPage[h.page].append(int(m_hits.size()));
                    m_hits.push_back(h);
                }
                m_searchProgress = double(page + 1) / n;
                // the first hit on or after the page the search started on becomes the current one, as soon as it is found
                if (m_searchCurrent < 0 && !hits.isEmpty() && hits.first().page >= startPage) {
                    m_searchCurrent = int(m_hits.size()) - int(hits.size());
                    emit searchChanged();
                    emit searchTarget(m_hits[size_t(m_searchCurrent)].page, m_hits[size_t(m_searchCurrent)].rects.value(0).top());
                } else {
                    emit searchChanged();
                }
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, run, cancelled = cancel->load()] {
            if (run != m_searchRun)
                return;
            m_searching = false;
            m_searchProgress = cancelled ? m_searchProgress : 1.0;
            if (m_searchCurrent < 0 && !m_hits.empty()) {                      // nothing after the start page: wrap to the first hit
                m_searchCurrent = 0;
                emit searchChanged();
                emit searchTarget(m_hits[0].page, m_hits[0].rects.value(0).top());
            } else {
                emit searchChanged();
            }
        }, Qt::QueuedConnection);
    }));
}

void DocumentController::searchStep(int delta)
{
    if (m_hits.empty())
        return;
    const int n = int(m_hits.size());
    m_searchCurrent = ((m_searchCurrent < 0 ? (delta > 0 ? -1 : 0) : m_searchCurrent) + delta) % n;
    if (m_searchCurrent < 0)
        m_searchCurrent += n;
    emit searchChanged();
    const pdf::SearchHit& h = m_hits[size_t(m_searchCurrent)];
    emit searchTarget(h.page, h.rects.value(0).top());
}

QVariantList DocumentController::searchRects(int page) const
{
    QVariantList out;
    const auto it = m_hitsByPage.constFind(page);
    if (it == m_hitsByPage.constEnd())
        return out;
    for (int index : it.value())
        for (const QRectF& r : m_hits[size_t(index)].rects) {
            QVariantMap m = rectMap(r);
            m.insert(QStringLiteral("current"), index == m_searchCurrent);
            out.append(m);
        }
    return out;
}

// ---- selection -------------------------------------------------------------------------------------------------------------

bool DocumentController::selectionRange(SelPoint& from, SelPoint& to) const
{
    if (!m_selAnchor.valid() || !m_selFocus.valid())
        return false;
    from = m_selAnchor;
    to = m_selFocus;
    if (to.page < from.page || (to.page == from.page && to.ch < from.ch))
        std::swap(from, to);
    return true;
}

bool DocumentController::hasSelection() const
{
    SelPoint a, b;
    return selectionRange(a, b) && (a.page != b.page || a.ch != b.ch);
}

void DocumentController::selectStart(int page, double x, double y)
{
    const bool had = hasSelection();
    m_selAnchor = m_selFocus = {};
    if (const auto doc = currentDoc()) {
        const int idx = doc->charIndexAt(page, QPointF(x, y), 4.0, currentRotation());
        if (idx >= 0)
            m_selAnchor = m_selFocus = {page, idx};
    }
    if (had)
        emit selectionChanged();
}

void DocumentController::selectMove(int page, double x, double y)
{
    const auto doc = currentDoc();
    if (!doc)
        return;
    const int idx = doc->charIndexAt(page, QPointF(x, y), 30.0, currentRotation());     // snaps to the nearest letter
    if (idx < 0)
        return;
    if (!m_selAnchor.valid())
        m_selAnchor = {page, idx};
    if (m_selFocus.page == page && m_selFocus.ch == idx)
        return;
    m_selFocus = {page, idx};
    emit selectionChanged();
}

void DocumentController::selectAll()
{
    const auto doc = currentDoc();
    if (!doc)
        return;
    int first = -1;
    for (int p = 0; p < doc->pageCount() && first < 0; ++p)
        if (doc->charCount(p) > 0)
            first = p;
    int last = -1;
    for (int p = doc->pageCount() - 1; p >= 0 && last < 0; --p)
        if (doc->charCount(p) > 0)
            last = p;
    if (first < 0 || last < 0)
        return;
    m_selAnchor = {first, 0};
    m_selFocus = {last, doc->charCount(last) - 1};
    emit selectionChanged();
}

void DocumentController::clearSelection()
{
    const bool had = m_selAnchor.valid() || m_selFocus.valid();
    m_selAnchor = m_selFocus = {};
    if (had)
        emit selectionChanged();
}

QVariantList DocumentController::selectionRects(int page) const
{
    QVariantList out;
    SelPoint from, to;
    const auto doc = currentDoc();
    if (!doc || !selectionRange(from, to) || (from.page == to.page && from.ch == to.ch) || page < from.page || page > to.page)
        return out;
    const int start = page == from.page ? from.ch : 0;
    const int end = page == to.page ? to.ch : doc->charCount(page) - 1;
    if (end < start)
        return out;
    for (const QRectF& r : doc->textRects(page, start, end - start + 1, currentRotation()))
        out.append(rectMap(r));
    return out;
}

QString DocumentController::selectedText() const
{
    SelPoint from, to;
    const auto doc = currentDoc();
    if (!doc || !selectionRange(from, to) || (from.page == to.page && from.ch == to.ch))
        return {};
    QStringList parts;
    for (int page = from.page; page <= to.page; ++page) {
        const int start = page == from.page ? from.ch : 0;
        const int end = page == to.page ? to.ch : doc->charCount(page) - 1;
        if (end >= start)
            parts.append(doc->textRange(page, start, end - start + 1));
    }
    return parts.join(QLatin1Char('\n'));
}

bool DocumentController::copySelection() const
{
    const QString text = selectedText();
    if (text.isEmpty())
        return false;
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
    return true;
}

// ---- structure -------------------------------------------------------------------------------------------------------------

QVariantList DocumentController::outline() const
{
    QVariantList out;
    if (const auto doc = currentDoc())
        for (const pdf::OutlineItem& item : doc->outline())
            out.append(QVariantMap{{QStringLiteral("title"), item.title}, {QStringLiteral("page"), item.page}, {QStringLiteral("level"), item.level}});
    return out;
}

void DocumentController::requestAnnotations()
{
    const auto doc = currentDoc();
    if (!doc || m_annotationsLoading)
        return;
    m_annotationsLoading = true;
    const int run = ++m_annotationsRun;
    const int rotation = currentRotation();
    emit annotationsChanged();
    QThreadPool::globalInstance()->start(QRunnable::create([this, doc, rotation, run] {
        const QList<pdf::AnnotationInfo> list = doc->annotations(rotation);
        QMetaObject::invokeMethod(this, [this, run, list] {
            if (run != m_annotationsRun)
                return;
            m_annotations.clear();
            for (const pdf::AnnotationInfo& a : list) {
                QVariantMap m = rectMap(a.rect);
                m.insert(QStringLiteral("page"), a.page);
                m.insert(QStringLiteral("type"), a.type);
                m.insert(QStringLiteral("text"), a.contents);
                m_annotations.append(m);
            }
            m_annotationsLoading = false;
            emit annotationsChanged();
        }, Qt::QueuedConnection);
    }));
}

QVariantMap DocumentController::linkAt(int page, double x, double y) const
{
    QVariantMap m{{QStringLiteral("kind"), QStringLiteral("none")}, {QStringLiteral("page"), -1}, {QStringLiteral("uri"), QString()}};
    if (const auto doc = currentDoc()) {
        const pdf::LinkTarget t = doc->linkAt(page, QPointF(x, y), currentRotation());
        if (t.kind == pdf::LinkTarget::Page) {
            m.insert(QStringLiteral("kind"), QStringLiteral("page"));
            m.insert(QStringLiteral("page"), t.page);
        } else if (t.kind == pdf::LinkTarget::Uri) {
            m.insert(QStringLiteral("kind"), QStringLiteral("uri"));
            m.insert(QStringLiteral("uri"), t.uri);
        }
    }
    return m;
}

bool DocumentController::openUri(const QString& uri) const
{
    const QUrl url(uri, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("mailto")))
        return false;                      // a PDF must not start programs or open files through a link
    return QDesktopServices::openUrl(url);
}

// ---- printing --------------------------------------------------------------------------------------------------------------

QStringList DocumentController::printerNames() const { return QPrinterInfo::availablePrinterNames(); }
QString DocumentController::defaultPrinter() const { return QPrinterInfo::defaultPrinterName(); }

QString DocumentController::startPrint(const QVariantMap& options)
{
    const auto doc = currentDoc();
    if (!doc)
        return QStringLiteral("noDocument");
    if (m_printing)
        return QStringLiteral("busy");

    PrintOptions o;
    const QString spec = options.value(QStringLiteral("pages"), QStringLiteral("all")).toString().trimmed();
    if (spec == QLatin1String("all")) {
        for (int p = 0; p < doc->pageCount(); ++p)
            o.pages.append(p);
    } else if (spec == QLatin1String("current")) {
        const int page = options.value(QStringLiteral("currentPage"), 0).toInt();
        if (page < 0 || page >= doc->pageCount())
            return QStringLiteral("badRange");
        o.pages.append(page);
    } else {
        o.pages = parsePageRanges(spec, doc->pageCount());
        if (o.pages.isEmpty())
            return QStringLiteral("badRange");
    }
    o.printerName = options.value(QStringLiteral("printer")).toString();
    o.outputFile = options.value(QStringLiteral("outputFile")).toString();
    o.copies = qBound(1, options.value(QStringLiteral("copies"), 1).toInt(), 999);
    o.color = options.value(QStringLiteral("color"), true).toBool();
    o.fit = options.value(QStringLiteral("fit"), true).toBool();
    o.rotation = currentRotation();

    m_printCancel = std::make_shared<std::atomic_bool>(false);
    auto* task = new PrintTask(doc, o, m_printCancel);
    connect(task, &PrintTask::progress, this, [this](int done, int total) {
        m_printProgress = total > 0 ? double(done) / total : 0.0;
        emit printingChanged();
    }, Qt::QueuedConnection);
    connect(task, &PrintTask::finished, this, [this](bool ok, const QString& message) {
        m_printing = false;
        m_printProgress = ok ? 1.0 : m_printProgress;
        emit printingChanged();
        emit printFinished(ok, message);
    }, Qt::QueuedConnection);
    m_printing = true;
    m_printProgress = 0.0;
    emit printingChanged();
    QThreadPool::globalInstance()->start(task);
    return {};
}

void DocumentController::cancelPrint()
{
    if (m_printCancel)
        m_printCancel->store(true);
}

namespace {
QList<int> imagePages(const QString& range, int currentPage, int count)
{
    if (range == QLatin1String("all")) {
        QList<int> pages(count);
        std::iota(pages.begin(), pages.end(), 0);
        return pages;
    }
    if (range == QLatin1String("current"))
        return currentPage >= 0 && currentPage < count ? QList<int>{currentPage} : QList<int>{};
    return parsePageRanges(range, count);
}
} // namespace

int DocumentController::imagePageCount(const QString& range, int currentPage) const
{
    return int(imagePages(range, currentPage, pageCount()).size());
}

QString DocumentController::startImageExport(const QVariantMap& options, const QString& path)
{
    if (m_outputBusy)
        return QStringLiteral("busy");
    const auto doc = currentDoc();
    if (!doc)
        return QStringLiteral("noDocument");
    ImageExport job;
    job.pages = imagePages(options.value(QStringLiteral("pages")).toString(),
                           options.value(QStringLiteral("currentPage")).toInt(), doc->pageCount());
    if (job.pages.isEmpty())
        return QStringLiteral("range");
    job.jpeg = options.value(QStringLiteral("jpeg")).toBool();
    job.transparent = options.value(QStringLiteral("transparent")).toBool();
    job.dpi = options.value(QStringLiteral("dpi"), 144).toInt();
    job.quality = options.value(QStringLiteral("quality"), 90).toInt();
    job.rotation = currentRotation();

    if (path.isEmpty())
        return QStringLiteral("write");
    QString destination = QFileInfo(localPath(path)).absoluteFilePath();
    if (job.pages.size() > 1)
        job.target = options.value(QStringLiteral("folder")).toBool() ? ImageExport::Target::Folder : ImageExport::Target::Zip;
    if (job.target == ImageExport::Target::Folder) {
        const QDir parent(destination);
        if (!parent.exists())
            return QStringLiteral("write");
        const QString stem = tools::safeFileName(QFileInfo(doc->path()).completeBaseName()) + QStringLiteral("-images");
        destination = parent.filePath(stem);
        for (int n = 2; QFileInfo::exists(destination); ++n)
            destination = parent.filePath(stem + QStringLiteral("-%1").arg(n));
    } else {
        const QString suffix = QFileInfo(destination).suffix().toLower();
        const bool valid = job.target == ImageExport::Target::Zip ? suffix == QLatin1String("zip")
            : job.jpeg ? (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) : suffix == QLatin1String("png");
        if (!valid)
            return QStringLiteral("imageSuffix");
    }

    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_imageCancel = cancel;
    m_outputBusy = true;
    m_outputProgress = 0;
    emit outputChanged();
    m_outputPool.start(QRunnable::create([this, doc, job, destination, cancel] {
        const QString error = exportImages(*doc, destination, job, *cancel, [this](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] {
                m_outputProgress = double(done) / total;
                emit outputChanged();
            }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(this, [this, error, destination] { finishOutput(error, destination); }, Qt::QueuedConnection);
    }));
    return {};
}

void DocumentController::cancelImageExport()
{
    if (m_imageCancel)
        m_imageCancel->store(true);
}

QString DocumentController::startSecuritySave(const QVariantMap& options, const QString& path)
{
    if (m_outputBusy)
        return QStringLiteral("busy");
    const QString source = filePath();
    if (source.isEmpty())
        return QStringLiteral("noDocument");
    const QString destination = QFileInfo(localPath(path)).absoluteFilePath();
    if (QFileInfo(destination).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("pdfSuffix");

    const bool unlock = options.value(QStringLiteral("unlock")).toBool();
    if (unlock && !options.value(QStringLiteral("authorized")).toBool())
        return QStringLiteral("authorization");
    tools::Protection protection;
    protection.ownerPassword = options.value(QStringLiteral("owner")).toString();
    protection.userPassword = options.value(QStringLiteral("user")).toString();
    protection.print = options.value(QStringLiteral("print"), true).toBool();
    protection.copy = options.value(QStringLiteral("copy"), true).toBool();
    protection.modify = options.value(QStringLiteral("modify"), true).toBool();
    protection.annotate = options.value(QStringLiteral("annotate"), true).toBool();

    m_outputBusy = true;
    m_outputProgress = 0;
    emit outputChanged();
    m_outputPool.start(QRunnable::create([this, source, destination, unlock, protection = std::move(protection)]() mutable {
        const QString error = unlock ? tools::unlockPdf(source, destination, protection.ownerPassword)
                                     : tools::protectPdf(source, destination, protection);
        protection.ownerPassword.clear();
        protection.userPassword.clear();
        QMetaObject::invokeMethod(this, [this, error, destination] { finishOutput(error, destination); }, Qt::QueuedConnection);
    }));
    return {};
}

void DocumentController::finishOutput(const QString& error, const QString& path)
{
    m_imageCancel.reset();
    m_outputBusy = false;
    emit outputChanged();
    emit outputFinished(error, error.isEmpty() ? path : QString());
}

QString DocumentController::startRedactionScan(const QString& query, bool regex)
{
    if (m_outputBusy)
        return QStringLiteral("busy");
    const auto doc = currentDoc();
    if (!doc)
        return QStringLiteral("noDocument");
    if (currentRotation() != 0)
        return QStringLiteral("redactRotation");
    const QRegularExpression pattern(regex ? query : QRegularExpression::escape(query),
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (query.trimmed().isEmpty() || query.size() > 512 || !pattern.isValid())
        return QStringLiteral("redactPattern");

    const int sourceId = docId();
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_imageCancel = cancel;
    m_outputBusy = true;
    m_outputProgress = 0;
    emit outputChanged();
    m_outputPool.start(QRunnable::create([this, doc, sourceId, pattern, cancel] {
        constexpr int maxMarks = 2000;
        QVariantList marks;
        QString error;
        for (int page = 0; page < doc->pageCount() && !cancel->load() && error.isEmpty(); ++page) {
            const auto bounds = QRectF(QPointF(), doc->pageSizePt(page));
            for (const auto& hit : doc->matchPage(page, pattern, maxMarks + 1)) {
                for (const auto& rect : hit.rects) {
                    const auto r = rect.adjusted(-2, -2, 2, 2).intersected(bounds);
                    if (r.isEmpty())
                        continue;
                    marks.append(QVariantMap{{"page", page}, {"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}});
                }
                if (marks.size() > maxMarks) {
                    error = QStringLiteral("redactTooMany");
                    break;
                }
            }
        }
        if (cancel->load())
            error = QStringLiteral("cancelled");
        if (error.isEmpty() && marks.isEmpty())
            error = QStringLiteral("redactNoMatches");
        if (!error.isEmpty())
            marks.clear();
        QMetaObject::invokeMethod(this, [this, sourceId, error, marks] {
            m_imageCancel.reset();
            m_outputBusy = false;
            emit outputChanged();
            emit redactionScanFinished(sourceId, error, marks);
        }, Qt::QueuedConnection);
    }));
    return {};
}

QString DocumentController::startRedaction(const QVariantList& marks, const QString& color, const QString& label, const QString& path,
                                           bool selective)
{
    if (m_outputBusy)
        return QStringLiteral("busy");
    const auto doc = currentDoc();
    if (!doc)
        return QStringLiteral("noDocument");
    if (currentRotation() != 0)
        return QStringLiteral("redactRotation");
    if (marks.isEmpty() || marks.size() > 2000)
        return QStringLiteral("redactMarks");
    const QString destination = QFileInfo(localPath(path)).absoluteFilePath();
    if (QFileInfo(destination).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("pdfSuffix");
    QList<RedactionMark> areas;
    for (const auto& value : marks) {
        const auto mark = value.toMap();
        areas.append({mark.value("page", -1).toInt(), QRectF(mark.value("x").toDouble(), mark.value("y").toDouble(),
                      mark.value("w").toDouble(), mark.value("h").toDouble())});
    }
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_imageCancel = cancel;
    m_outputBusy = true;
    m_outputProgress = 0;
    emit outputChanged();
    m_outputPool.start(QRunnable::create([this, doc, areas, color, label, destination, cancel, selective] {
        const QString error = redactPdf(*doc, destination, areas, QColor(color), label, [this, cancel](int done, int total) {
            if (cancel->load())
                return false;
            QMetaObject::invokeMethod(this, [this, done, total] {
                m_outputProgress = double(done) / total;
                emit outputChanged();
            }, Qt::QueuedConnection);
            return true;
        }, selective);
        QMetaObject::invokeMethod(this, [this, error, destination] { finishOutput(error, destination); }, Qt::QueuedConnection);
    }));
    return {};
}

// ---- splitting -------------------------------------------------------------------------------------------------------------

namespace {
QString baseNameOf(const QString& path)
{
    return QFileInfo(path).completeBaseName();
}

// the parts of a file for a split mode (not "size": that needs the real files); empty = not valid
QList<tools::SplitPart> splitPartsFor(const std::shared_ptr<pdf::PdfDocument>& doc, const QString& mode, const QVariant& param)
{
    if (mode == QLatin1String("ranges")) {
        bool ok = false;
        const auto parts = tools::splitByRanges(param.toString(), doc->pageCount(), &ok);
        return ok ? parts : QList<tools::SplitPart>();
    }
    if (mode == QLatin1String("every"))
        return tools::splitEvery(param.toInt(), doc->pageCount());
    if (mode == QLatin1String("bookmarks")) {
        QList<QPair<QString, int>> top;
        for (const pdf::OutlineItem& item : doc->outline())
            if (item.level == 0 && item.page >= 0)
                top.append({item.title, item.page});
        return tools::splitByBookmarks(top, doc->pageCount());
    }
    return {};
}
} // namespace

DocumentController::SourceInfo DocumentController::currentSource() const
{
    QMutexLocker lock(&m_mutex);
    SourceInfo info;
    if (const Tab* t = current()) {
        info.id = t->id;
        info.doc = t->doc;
        info.path = t->doc->path();
        info.password = t->password;
    }
    return info;
}

int DocumentController::registerExtra(std::shared_ptr<pdf::PdfDocument> doc)
{
    QMutexLocker lock(&m_mutex);
    const int id = m_nextId++;
    m_extras[id] = std::move(doc);
    return id;
}

void DocumentController::unregisterExtra(int id)
{
    QMutexLocker lock(&m_mutex);
    m_extras.erase(id);
}

QVariantList DocumentController::splitPreview(const QString& mode, const QVariant& param) const
{
    QVariantList out;
    const auto doc = currentDoc();
    if (!doc)
        return out;
    const QString stem = tools::safeFileName(baseNameOf(doc->path()));
    for (const tools::SplitPart& part : splitPartsFor(doc, mode, param))
        out.append(QVariantMap{{QStringLiteral("name"), stem + QLatin1Char('-') + part.suffix + QStringLiteral(".pdf")},
                               {QStringLiteral("pages"), tools::rangeLabel(part.pages)},
                               {QStringLiteral("count"), int(part.pages.size())}});
    return out;
}

QString DocumentController::startSplit(const QVariantMap& options)
{
    const SourceInfo src = currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_splitting)
        return QStringLiteral("busy");
    const QString mode = options.value(QStringLiteral("mode")).toString();
    const QString outDir = options.value(QStringLiteral("outDir")).toString();
    if (outDir.isEmpty() || !QDir().mkpath(outDir))
        return QStringLiteral("noFolder");

    // the plan: known now for ranges / every / bookmarks; "size" is measured in the background task
    QList<tools::SplitPart> parts;
    qint64 maxBytes = 0;
    if (mode == QLatin1String("size")) {
        maxBytes = qint64(options.value(QStringLiteral("sizeMb")).toDouble() * 1024 * 1024);
        if (maxBytes < 4096)
            return QStringLiteral("badRange");
    } else {
        const QVariant param = mode == QLatin1String("every") ? options.value(QStringLiteral("n")) : options.value(QStringLiteral("text"));
        parts = splitPartsFor(src.doc, mode, param);
        if (parts.isEmpty())
            return mode == QLatin1String("bookmarks") ? QStringLiteral("noParts") : QStringLiteral("badRange");
    }

    m_splitCancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_splitCancel;
    m_splitting = true;
    m_splitProgress = 0.0;
    emit splitChanged();
    const QString stem = baseNameOf(src.path);
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, parts, maxBytes, outDir, stem, cancel]() mutable {
        auto report = [this](double fraction) {
            QMetaObject::invokeMethod(this, [this, fraction] { m_splitProgress = fraction; emit splitChanged(); }, Qt::QueuedConnection);
        };
        QStringList files;
        QStringList warnings;
        QString error;
        QString errorFile;
        bool ok = true;
        if (maxBytes > 0) {
            parts = tools::planSplitBySize(src.path, src.password, maxBytes, [&](int done, int total) {
                report(0.3 * done / std::max(1, total));
                return !cancel->load();
            }, &error);
            ok = !parts.isEmpty();
        }
        const double base = maxBytes > 0 ? 0.3 : 0.0;
        for (int i = 0; ok && i < parts.size(); ++i) {
            if (cancel->load()) {
                ok = false;
                error = QStringLiteral("cancelled");
                break;
            }
            QList<tools::PageSpec> specs;
            for (int p : parts.at(i).pages) {
                tools::PageSpec s;
                s.file = src.path;
                s.password = src.password;
                s.page = p;
                specs.append(s);
            }
            const QString path = tools::uniqueOutputPath(outDir, stem, parts.at(i).suffix);
            const tools::WriteResult r = tools::writePages(specs, path);
            if (!r.ok) {
                ok = false;
                error = r.error;
                errorFile = r.errorFile;
                break;
            }
            files << QDir::toNativeSeparators(path);
            for (const QString& w : r.warnings)
                if (!warnings.contains(w) && !w.startsWith(QLatin1String("linksRemoved")))
                    warnings << w;
            report(base + (1.0 - base) * (i + 1) / parts.size());
        }
        if (!ok && !files.isEmpty()) {
            // a split that failed or was cancelled removes what it already wrote: half of a result is not a result
            for (const QString& f : std::as_const(files))
                QFile::remove(f);
            files.clear();
        }
        QMetaObject::invokeMethod(this, [this, ok, error, files, warnings, errorFile] {
            m_splitting = false;
            m_splitProgress = ok ? 1.0 : m_splitProgress;
            emit splitChanged();
            emit splitFinished(ok, error, files, warnings, errorFile);
        }, Qt::QueuedConnection);
    }));
    return {};
}

void DocumentController::cancelSplit()
{
    if (m_splitCancel)
        m_splitCancel->store(true);
}

} // namespace dp
