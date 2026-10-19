// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarymanager.h"

#include "core/catalog.h"
#include "core/downloads.h"
#include "core/library.h"
#include "core/settings.h"
#include "core/zimarchive.h"
#include "core/zimreader.h"
#include "articlestore.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QStorageInfo>

#include <KLocalizedString>

LibraryManager::LibraryManager(QObject *parent)
    : QObject(parent)
    , m_feed(new CatalogFeed(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("catalog-eng.xml")),
                             Catalog::defaultFeedUrl(), this))
    , m_downloader(new DownloadManager(this))
{
    connect(m_feed, &CatalogFeed::changed, this, [this] {
        if (m_catalog.allEntries().size() != m_feed->entries().size() || m_catalog.allEntries().isEmpty()
            || m_feed->fetchedAt() != m_lastFetch) {
            m_lastFetch = m_feed->fetchedAt();
            m_catalog.setEntries(m_feed->entries());
            syncCatalogState();
            reloadInstalled();  // update badges depend on the catalog
        }
        Q_EMIT catalogChanged();
    });
    connect(m_downloader, &DownloadManager::listChanged, this, &LibraryManager::reloadDownloads);
    connect(m_downloader, &DownloadManager::changed, this, [this](qint64 id) {
        if (const auto r = m_downloader->byId(id))
            m_downloads.update(*r);
        syncCatalogState();
        Q_EMIT downloadsChanged();
    });
    connect(m_downloader, &DownloadManager::completed, this, [this](qint64, qint64 libraryId) {
        reloadInstalled();
        syncCatalogState();
        Q_EMIT folderChanged();  // free space moved
        if (const auto lib = Library::byId(libraryId))
            Q_EMIT message(i18n("%1 is ready", lib->title), false);
    });
    connect(m_downloader, &DownloadManager::failed, this, [this](qint64 id, const QString &text) {
        const auto r = m_downloader->byId(id);
        Q_EMIT message(i18n("Download of %1 failed: %2", r ? r->filename : QString(), text), true);
    });
}

namespace
{
LibraryManager *s_instance = nullptr;
}

LibraryManager *LibraryManager::createInstance(QObject *parent)
{
    Q_ASSERT(!s_instance);
    s_instance = new LibraryManager(parent);
    return s_instance;
}

LibraryManager *LibraryManager::instance()
{
    return s_instance;
}

LibraryManager *LibraryManager::create(QQmlEngine *, QJSEngine *)
{
    auto *m = instance();
    QQmlEngine::setObjectOwnership(m, QQmlEngine::CppOwnership);
    return m;
}

void LibraryManager::initialize()
{
    m_downloader->reconcile();
    QDir().mkpath(folder());
    Library::scan(folder());
    reloadInstalled();
    reloadDownloads();
}

QString LibraryManager::folder() const
{
    return AppSettings::libraryFolder();
}

double LibraryManager::freeSpace() const
{
    return double(QStorageInfo(folder()).bytesAvailable());
}

int LibraryManager::activeDownloads() const
{
    return m_downloader->activeCount();
}

bool LibraryManager::catalogLoading() const
{
    return m_feed->isLoading();
}

QString LibraryManager::catalogError() const
{
    return m_feed->error();
}

QDateTime LibraryManager::catalogFetchedAt() const
{
    return m_feed->fetchedAt();
}

bool LibraryManager::aria2Available() const
{
    return !DownloadManager::aria2Program().isEmpty();
}

void LibraryManager::loadCatalog()
{
    m_feed->load();
}

void LibraryManager::refreshCatalog()
{
    m_feed->refresh();
}

void LibraryManager::download(int catalogRow)
{
    if (const auto entry = m_catalog.entryAt(catalogRow))
        startDownload(*entry);
}

void LibraryManager::updateLibrary(qint64 libraryId)
{
    const auto lib = Library::byId(libraryId);
    const auto parts = lib ? Catalog::splitFilename(lib->filename()) : std::nullopt;
    if (!parts)
        return;
    const CatalogEntry *newest = nullptr;
    for (const CatalogEntry &e : m_catalog.allEntries())
        if (e.base == parts->first && e.version > parts->second && (!newest || e.version > newest->version))
            newest = &e;
    if (newest)
        startDownload(*newest);
}

void LibraryManager::startDownload(const CatalogEntry &entry)
{
    const QString problem = m_downloader->start(entry.url, entry.filename, entry.title, entry.size, folder());
    if (!problem.isEmpty()) {
        Q_EMIT message(i18n("Cannot download %1: %2", entry.title, problem), true);
        return;
    }
    reloadDownloads();
}

void LibraryManager::cancelDownload(qint64 id)
{
    m_downloader->cancel(id);
}

void LibraryManager::resumeDownload(qint64 id)
{
    const QString problem = m_downloader->resume(id);
    if (!problem.isEmpty())
        Q_EMIT message(i18n("Cannot resume: %1", problem), true);
}

void LibraryManager::removeDownload(qint64 id)
{
    m_downloader->remove(id);
    syncCatalogState();
    Q_EMIT folderChanged();
}

void LibraryManager::rescan()
{
    const auto result = Library::scan(folder());
    reloadInstalled();
    syncCatalogState();
    if (!result.errors.isEmpty())
        Q_EMIT message(i18n("Some files could not be read: %1", result.errors.join(QStringLiteral("; "))), true);
    else if (!result.added.isEmpty())
        Q_EMIT message(i18np("Found one new library", "Found %1 new libraries", result.added.size()), false);
    else
        Q_EMIT message(i18n("No new libraries in %1", folder()), false);
}

void LibraryManager::addFile(const QUrl &file)
{
    QString error;
    const auto id = Library::add(file.toLocalFile(), &error);
    if (!id) {
        Q_EMIT message(i18n("Cannot add %1: %2", file.fileName(), error), true);
        return;
    }
    reloadInstalled();
    syncCatalogState();
    if (const auto lib = Library::byId(*id))
        Q_EMIT message(i18n("Added %1", lib->title), false);
}

void LibraryManager::removeLibrary(qint64 id, bool deleteFile)
{
    const auto lib = Library::byId(id);
    ZimReader::close(id);  // let go of the file before it is deleted
    ArticleStore::clear();
    QString error;
    if (!Library::remove(id, deleteFile, &error)) {
        Q_EMIT message(i18n("Cannot remove the library: %1", error), true);
        return;
    }
    reloadInstalled();
    syncCatalogState();
    Q_EMIT folderChanged();
    if (lib)
        Q_EMIT message(deleteFile ? i18n("Deleted %1", lib->title) : i18n("Removed %1 from KAthenaeum; the file is still on disk", lib->title), false);
}

bool LibraryManager::setFolder(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty() || !QDir().mkpath(path) || !QFileInfo(path).isWritable()) {
        Q_EMIT message(i18n("Cannot use %1 for libraries: it is not a writable folder", path), true);
        return false;
    }
    AppSettings::setLibraryFolder(path);
    Library::scan(path);
    reloadInstalled();
    syncCatalogState();
    Q_EMIT folderChanged();
    return true;
}

QVariantMap LibraryManager::details(qint64 id) const
{
    const auto r = Library::byId(id);
    if (!r)
        return {};
    return {{QStringLiteral("title"), r->title},
            {QStringLiteral("description"), r->description},
            {QStringLiteral("path"), r->path},
            {QStringLiteral("name"), r->name},
            {QStringLiteral("language"), r->language},
            {QStringLiteral("creator"), r->creator},
            {QStringLiteral("publisher"), r->publisher},
            {QStringLiteral("date"), r->date},
            {QStringLiteral("flavour"), r->flavour},
            {QStringLiteral("scraper"), r->scraper},
            {QStringLiteral("uuid"), r->uuid},
            {QStringLiteral("articleCount"), double(r->articleCount)},
            {QStringLiteral("size"), double(r->sizeBytes)},
            {QStringLiteral("hasFulltext"), r->hasFulltextIndex},
            {QStringLiteral("isWikipedia"), ZimArchive::isMwoffliner(r->scraper)},
            {QStringLiteral("missing"), !r->fileExists()},
            {QStringLiteral("addedAt"), r->addedAt.toLocalTime()}};
}

QVariantList LibraryManager::libraryList() const
{
    QVariantList out;
    for (const LibraryRecord &r : Library::all())
        if (r.fileExists())
            out.append(QVariantMap{{QStringLiteral("libraryId"), r.id}, {QStringLiteral("title"), r.title}});
    return out;
}

void LibraryManager::reloadInstalled()
{
    QHash<qint64, CatalogEntry> updates;
    const QList<LibraryRecord> libs = Library::all();
    for (const LibraryRecord &lib : libs) {
        const auto parts = Catalog::splitFilename(lib.filename());
        if (!parts)
            continue;
        for (const CatalogEntry &e : m_catalog.allEntries())
            if (e.base == parts->first && e.version > parts->second && (!updates.contains(lib.id) || e.version > updates[lib.id].version))
                updates.insert(lib.id, e);
    }
    // a newer version that is already installed is not an update any more
    for (const LibraryRecord &lib : libs)
        for (auto it = updates.begin(); it != updates.end();)
            it = it->filename == lib.filename() ? updates.erase(it) : std::next(it);
    m_installed.reload(folder(), updates);
}

void LibraryManager::reloadDownloads()
{
    m_downloads.reload(m_downloader->list());
    syncCatalogState();
    Q_EMIT downloadsChanged();
}

void LibraryManager::syncCatalogState()
{
    QSet<QString> installed;
    for (const LibraryRecord &lib : m_installed.records())
        if (lib.fileExists())
            installed.insert(lib.filename());
    QHash<QString, double> downloading;
    for (const DownloadRecord &d : m_downloads.records())
        if (d.isActive())
            downloading.insert(d.filename, d.progress());
    m_catalog.setLocalState(installed, downloading);
}
