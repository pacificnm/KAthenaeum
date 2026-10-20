// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarymanager.h"

#include "connectivity.h"

#include "articlestore.h"
#include "core/catalog.h"
#include "core/downloads.h"
#include "core/library.h"
#include "core/settings.h"
#include "core/zimarchive.h"
#include "core/zimreader.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>

#include <KLocalizedString>

LibraryManager::LibraryManager(QObject *parent)
    : QObject(parent)
    , m_feed(new CatalogFeed(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("catalog-eng.xml")),
                             Catalog::defaultFeedUrl(),
                             this))
    , m_downloader(new DownloadManager(this))
{
    connect(m_feed, &CatalogFeed::changed, this, [this] {
        if (m_catalog.allEntries().size() != m_feed->entries().size() || m_catalog.allEntries().isEmpty() || m_feed->fetchedAt() != m_lastFetch) {
            m_lastFetch = m_feed->fetchedAt();
            m_catalog.setEntries(m_feed->entries());
            syncCatalogState();
            reloadInstalled(); // update badges depend on the catalog
        }
        if (m_checkingUpdates && !m_feed->isLoading())
            finishUpdateCheck();
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
        Q_EMIT folderChanged(); // free space moved
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
    // updates known from the catalog already on disk, without the network
    m_feed->useCache();
    // and once a week, a little after starting, the catalog is fetched to look for newer versions
    const QDateTime last = AppSettings::lastLibraryUpdateCheck();
    if (AppSettings::autoCheckLibraryUpdates() && !Library::all().isEmpty() && (!last.isValid() || last.daysTo(QDateTime::currentDateTime()) >= 7))
        QTimer::singleShot(20000, this, [this] {
            checkForUpdates(true);
        });
}

QDateTime LibraryManager::lastUpdateCheck() const
{
    return AppSettings::lastLibraryUpdateCheck();
}

bool LibraryManager::autoCheckUpdates() const
{
    return AppSettings::autoCheckLibraryUpdates();
}

void LibraryManager::setAutoCheckUpdates(bool on)
{
    if (on == autoCheckUpdates())
        return;
    AppSettings::setAutoCheckLibraryUpdates(on);
    Q_EMIT updatesChanged();
}

namespace
{
bool online()
{
    const Connectivity *c = Connectivity::instance();
    return !c || c->online();
}
}

void LibraryManager::checkForUpdates(bool quiet)
{
    if (m_checkingUpdates)
        return;
    if (!online()) {
        if (!quiet)
            Q_EMIT message(i18n("KAthenaeum is offline: library updates can be checked once it is online again."), true);
        return;
    }
    m_checkingUpdates = true;
    m_quietCheck = quiet;
    Q_EMIT updatesChanged();
    m_feed->refresh();
}

void LibraryManager::finishUpdateCheck()
{
    m_checkingUpdates = false;
    const QString error = m_feed->error();
    if (!error.isEmpty()) {
        if (!m_quietCheck)
            Q_EMIT message(i18n("Could not check for library updates: %1", error), true);
        Q_EMIT updatesChanged();
        return;
    }
    AppSettings::setLastLibraryUpdateCheck(QDateTime::currentDateTime());
    if (m_updateCount > 0)
        Q_EMIT message(i18np("An update is available: %2. The Library Manager has it.",
                             "%1 updates are available: %2. The Library Manager has them.",
                             m_updateCount,
                             m_updateNames.join(QStringLiteral(", "))),
                       false);
    else if (!m_quietCheck)
        Q_EMIT message(i18n("All libraries are up to date."), false);
    Q_EMIT updatesChanged();
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
    if (online())
        m_feed->load();
    else
        m_feed->useCache(); // the copy fetched last time, which says how old it is
}

void LibraryManager::refreshCatalog()
{
    if (!online()) {
        m_feed->useCache();
        Q_EMIT message(i18n("KAthenaeum is offline: showing the catalog as it was last fetched."), false);
        return;
    }
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
    if (!online()) {
        Q_EMIT message(i18n("Cannot download %1 while KAthenaeum is offline.", entry.title), true);
        return;
    }
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
    if (!online()) {
        Q_EMIT message(i18n("Cannot resume the download while KAthenaeum is offline."), true);
        return;
    }
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
    ZimReader::close(id); // let go of the file before it is deleted
    ArticleStore::clear();
    QString error;
    if (!Library::remove(id, deleteFile, &error)) {
        Q_EMIT message(i18n("Cannot remove the library: %1", error), true);
        return;
    }
    reloadInstalled();
    syncCatalogState();
    Q_EMIT folderChanged();
    if (!error.isEmpty() && lib) // removed, but its file could not be deleted
        Q_EMIT message(i18n("Removed %1 from KAthenaeum, but its file could not be deleted: %2", lib->title, lib->path), true);
    else if (lib)
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
    QList<LibraryRecord> libraries;
    QHash<QString, int> titles;
    for (const auto rows = Library::all(); const LibraryRecord &r : rows)
        if (r.fileExists()) {
            libraries << r;
            ++titles[r.title];
        }
    // two libraries with one title (Ray Charles, with and without pictures) are told apart by their flavour
    auto flavour = [](const QString &f) {
        return f == QLatin1String("mini") ? i18n("introductions only")
            : f == QLatin1String("nopic") ? i18n("no pictures")
            : f == QLatin1String("maxi")  ? i18n("with pictures")
                                          : f;
    };
    QVariantList out;
    for (const LibraryRecord &r : std::as_const(libraries)) {
        QString title = r.title;
        if (titles.value(r.title) > 1)
            title = i18nc("library title (flavour)", "%1 (%2)", r.title, r.flavour.isEmpty() ? r.filename() : flavour(r.flavour));
        out.append(QVariantMap{{QStringLiteral("libraryId"), r.id}, {QStringLiteral("title"), title}});
    }
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
    m_updateNames.clear();
    for (const LibraryRecord &lib : libs)
        if (updates.contains(lib.id))
            m_updateNames << lib.title;
    if (m_updateCount != updates.size()) {
        m_updateCount = int(updates.size());
        Q_EMIT updatesChanged();
    }
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
