// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mapmanager.h"

#include "connectivity.h"
#include "core/library.h"
#include "core/network.h"
#include "core/settings.h"
#include "core/zimreader.h"
#include "kathenaeum-version.h"

#include <KLocalizedString>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlEngine>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrentRun>

#include <cmath>

namespace
{
constexpr int ParallelDownloads = 2; // KDE's map server makes deep tiles on request: be gentle with it
constexpr qint64 MaxTileBytes = 8 << 20; // a dense city tile is a few hundred KB
constexpr int MaxFailures = 20;
constexpr int CityZoom = 2600; // Marble's zoom for a town and its surroundings

QString twoLetter(const QString &zimLanguage)
{
    const QString first = zimLanguage.section(QLatin1Char(','), 0, 0).trimmed();
    if (first.size() == 2)
        return first;
    const QLocale::Language l = QLocale::codeToLanguage(first, QLocale::ISO639Part3);
    return l == QLocale::AnyLanguage ? QString() : QLocale::languageToCode(l, QLocale::ISO639Part1);
}

QString pathFor(const QString &title)
{
    QString p = title.trimmed();
    p.replace(QLatin1Char(' '), QLatin1Char('_'));
    return p;
}

// the size of the tiles kept, and removing them: only the numbered level folders, never anything else in the folder
qint64 tileBytes(const QString &folder)
{
    qint64 total = 0;
    QDirIterator it(folder, {QStringLiteral("*.o5m")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        total += it.nextFileInfo().size();
    return total;
}

void removeTiles(const QString &folder)
{
    const QDir dir(folder);
    for (const auto levels = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot); const QString &level : levels) {
        bool number = false;
        level.toInt(&number);
        if (number)
            QDir(dir.filePath(level)).removeRecursively();
    }
}
}

MapManager *MapManager::createInstance(QObject *parent)
{
    Q_ASSERT(!s_instance);
    s_instance = new MapManager(parent);
    return s_instance;
}

MapManager *MapManager::instance()
{
    return s_instance;
}

MapManager *MapManager::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

MapManager::MapManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void MapManager::initialize()
{
    // before 0.18 maps were region files cut from Protomaps' planet build, with OpenStreetMap extracts read for places:
    // Marble draws the map now, so they are only taking space
    QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("maps"))).removeRecursively();
    const QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    for (const auto extracts = cache.entryList({QStringLiteral("map-*.osm.pbf")}, QDir::Files); const QString &f : extracts)
        QFile::remove(cache.filePath(f));
    measureCache();
}

QVariantMap MapManager::article(const QString &wikipedia, const QString &name) const
{
    const auto linked = MapTiles::wikipediaArticle(wikipedia);
    const QList<LibraryRecord> libraries = Library::all();
    auto found = [](const LibraryRecord &lib, const QString &title, bool exact) -> QVariantMap {
        if (title.isEmpty() || !ZimReader::hasEntry(lib.id, pathFor(title)))
            return {};
        return {{QStringLiteral("libraryId"), lib.id},
                {QStringLiteral("path"), pathFor(title)},
                {QStringLiteral("title"), title},
                {QStringLiteral("library"), lib.title},
                {QStringLiteral("exact"), exact}};
    };
    // the article OpenStreetMap links to, in a library of its language
    if (linked) {
        for (const LibraryRecord &lib : libraries)
            if (lib.fileExists() && twoLetter(lib.language) == linked->language)
                if (QVariantMap a = found(lib, linked->title, true); !a.isEmpty())
                    return a;
    }
    // else an article with the place's name
    for (const LibraryRecord &lib : libraries)
        if (lib.fileExists())
            if (QVariantMap a = found(lib, name, false); !a.isEmpty())
                return a;
    return {};
}

QVariantMap MapManager::locate(const QString &title, double lat, double lon) const
{
    if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180)
        return {};
    return {{QStringLiteral("lat"), lat}, {QStringLiteral("lon"), lon}, {QStringLiteral("zoom"), CityZoom}, {QStringLiteral("name"), title}};
}

QVariantMap MapManager::startView() const
{
    const QStringList saved = AppSettings::value(QStringLiteral("map.marbleView")).split(QLatin1Char(','));
    if (saved.size() == 3) {
        bool a = false, b = false, c = false;
        const double lat = saved[0].toDouble(&a), lon = saved[1].toDouble(&b);
        const int zoom = saved[2].toInt(&c);
        if (a && b && c)
            return {{QStringLiteral("lat"), lat}, {QStringLiteral("lon"), lon}, {QStringLiteral("zoom"), zoom}};
    }
    return {}; // Marble's own start: the whole Earth
}

void MapManager::saveView(double lat, double lon, int zoom)
{
    if (!std::isfinite(lat) || !std::isfinite(lon))
        return;
    AppSettings::setValue(QStringLiteral("map.marbleView"), QStringLiteral("%1,%2,%3").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(zoom));
}

int MapManager::tileCount(double west, double south, double east, double north) const
{
    return int(std::min<qint64>(MapTiles::countFor(west, south, east, north), INT_MAX));
}

void MapManager::saveArea(double west, double south, double east, double north)
{
    if (saving())
        return;
    if (MapTiles::countFor(west, south, east, north) > MapTiles::MaxSaveTiles) {
        Q_EMIT message(i18n("This area is too large to keep at street level. Zoom in to a town or a city and try again."), true);
        return;
    }
    if (const Connectivity *c = Connectivity::instance(); c && !c->online()) {
        Q_EMIT message(i18n("KAthenaeum is offline: an area can be kept once it is online again."), true);
        return;
    }
    const QDir cache(MapTiles::cacheFolder());
    m_queue.clear();
    for (const auto tiles = MapTiles::tilesFor(west, south, east, north); const MapTiles::Tile &t : tiles)
        if (!QFileInfo::exists(cache.filePath(MapTiles::relativePath(t))))
            m_queue.append(t);
    m_saveTotal = int(m_queue.size());
    m_saveDone = 0;
    m_failed = 0;
    if (m_queue.isEmpty()) {
        Q_EMIT message(i18n("This area is already kept on this computer."), false);
        return;
    }
    Q_EMIT saveChanged();
    for (int i = 0; i < ParallelDownloads; ++i)
        fetchNext();
}

void MapManager::fetchNext()
{
    if (m_queue.isEmpty()) {
        if (m_running == 0)
            finishSave(QString());
        return;
    }
    const MapTiles::Tile tile = m_queue.takeFirst();
    QNetworkRequest request = Network::guarded(QNetworkRequest(MapTiles::url(tile)));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("KAthenaeum/%1 (+https://github.com/pacificnm/KAthenaeum)").arg(QStringLiteral(KATHENAEUM_VERSION_STRING)));
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_network->get(request);
    Network::allowRedirects(reply, MapTiles::isAllowedUrl);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > MaxTileBytes)
            reply->abort();
    });
    m_replies.append(reply);
    ++m_running;
    connect(reply, &QNetworkReply::finished, this, [this, reply, tile] {
        reply->deleteLater();
        m_replies.removeAll(reply);
        if (reply->property("cancelled").toBool())
            return; // counted out when it was cancelled
        --m_running;
        bool ok = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QString path = QDir(MapTiles::cacheFolder()).filePath(MapTiles::relativePath(tile));
            QDir().mkpath(QFileInfo(path).path());
            QSaveFile file(path);
            ok = file.open(QIODevice::WriteOnly) && file.write(reply->readAll()) >= 0 && file.commit();
        }
        if (!ok && ++m_failed > MaxFailures) {
            const QString error = reply->errorString();
            for (const auto &r : std::as_const(m_replies))
                if (r) {
                    r->setProperty("cancelled", true);
                    r->abort();
                }
            m_queue.clear();
            m_running = 0;
            finishSave(error);
            return;
        }
        ++m_saveDone;
        Q_EMIT saveChanged();
        fetchNext();
    });
}

void MapManager::cancelSave()
{
    m_queue.clear();
    for (const auto &r : std::as_const(m_replies))
        if (r) {
            r->setProperty("cancelled", true);
            r->abort();
        }
    m_replies.clear();
    m_running = 0;
    Q_EMIT saveChanged();
    measureCache();
}

void MapManager::finishSave(const QString &error)
{
    if (error.isEmpty())
        Q_EMIT message(i18n("This area is kept for use offline."), false);
    else
        Q_EMIT message(i18n("The area could not be kept: %1", error), true);
    Q_EMIT saveChanged();
    measureCache();
}

void MapManager::measureCache()
{
    auto *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_cacheBytes = double(watcher->result());
        Q_EMIT cacheChanged();
    });
    watcher->setFuture(QtConcurrent::run(tileBytes, MapTiles::cacheFolder()));
}

void MapManager::clearCache()
{
    cancelSave();
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher] {
        watcher->deleteLater();
        measureCache();
    });
    watcher->setFuture(QtConcurrent::run(removeTiles, MapTiles::cacheFolder()));
}
