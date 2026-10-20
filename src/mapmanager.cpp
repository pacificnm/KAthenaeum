// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mapmanager.h"

#include "core/library.h"
#include "core/mapextract.h"
#include "core/maplookup.h"
#include "core/mapplaces.h"
#include "core/maptiling.h"
#include "core/network.h"
#include "core/pmtiles.h"
#include "core/settings.h"
#include "core/zimreader.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlEngine>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <cmath>

namespace
{
MapManager *s_instance = nullptr;

QString cachePath(const QString &name)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(name);
}

// "place=city" -> "City", "tourism=museum" -> "Museum", "locality" -> "Locality"
QString kindLabel(const QString &kind)
{
    static const QHash<QString, KLocalizedString> known = {
        {QStringLiteral("city"), ki18n("City")},
        {QStringLiteral("town"), ki18n("Town")},
        {QStringLiteral("village"), ki18n("Village")},
        {QStringLiteral("hamlet"), ki18n("Hamlet")},
        {QStringLiteral("suburb"), ki18n("Suburb")},
        {QStringLiteral("neighbourhood"), ki18n("Neighbourhood")},
        {QStringLiteral("locality"), ki18n("Place")},
        {QStringLiteral("country"), ki18n("Country")},
        {QStringLiteral("region"), ki18n("Region")},
        {QStringLiteral("state"), ki18n("State or province")},
        {QStringLiteral("administrative"), ki18n("Administrative area")},
        {QStringLiteral("park"), ki18n("Park")},
        {QStringLiteral("water"), ki18n("Water")},
        {QStringLiteral("peak"), ki18n("Mountain")},
        {QStringLiteral("museum"), ki18n("Museum")},
        {QStringLiteral("university"), ki18n("University")},
        {QStringLiteral("college"), ki18n("College")},
        {QStringLiteral("school"), ki18n("School")},
        {QStringLiteral("library"), ki18n("Library")},
        {QStringLiteral("theatre"), ki18n("Theatre")},
        {QStringLiteral("courthouse"), ki18n("Courthouse")},
        {QStringLiteral("stadium"), ki18n("Stadium")},
        {QStringLiteral("aerodrome"), ki18n("Airport")},
        {QStringLiteral("station"), ki18n("Station")},
        {QStringLiteral("hospital"), ki18n("Hospital")},
        {QStringLiteral("attraction"), ki18n("Attraction")},
        {QStringLiteral("monument"), ki18n("Monument")},
        {QStringLiteral("memorial"), ki18n("Memorial")},
        {QStringLiteral("place_of_worship"), ki18n("Place of worship")},
        {QStringLiteral("national_park"), ki18n("National park")},
        {QStringLiteral("protected_area"), ki18n("Protected area")},
        {QStringLiteral("nature_reserve"), ki18n("Nature reserve")},
        {QStringLiteral("island"), ki18n("Island")},
        {QStringLiteral("lake"), ki18n("Lake")},
        {QStringLiteral("river"), ki18n("River")},
        {QStringLiteral("county"), ki18n("County")},
        {QStringLiteral("national"), ki18n("Country")},
        {QStringLiteral("census"), ki18n("Census area")},
        {QStringLiteral("forest"), ki18n("Forest")},
        {QStringLiteral("wood"), ki18n("Woods")},
        {QStringLiteral("bay"), ki18n("Bay")},
        {QStringLiteral("beach"), ki18n("Beach")},
        {QStringLiteral("volcano"), ki18n("Volcano")},
        {QStringLiteral("glacier"), ki18n("Glacier")},
        {QStringLiteral("castle"), ki18n("Castle")},
        {QStringLiteral("ruins"), ki18n("Ruins")},
        {QStringLiteral("archaeological_site"), ki18n("Archaeological site")},
        {QStringLiteral("zoo"), ki18n("Zoo")},
        {QStringLiteral("viewpoint"), ki18n("Viewpoint")},
        {QStringLiteral("bridge"), ki18n("Bridge")},
        {QStringLiteral("lighthouse"), ki18n("Lighthouse")},
        {QStringLiteral("dam"), ki18n("Dam")},
    };
    QString value = kind.section(QLatin1Char('='), -1);
    if (const auto it = known.find(value); it != known.end())
        return it->toString();
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    return value.isEmpty() ? QString() : value.left(1).toUpper() + value.mid(1);
}

// "eng" (a ZIM's language) -> "en" (Wikipedia's)
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
    , m_downloader(new MapDownloader(m_network, this))
{
    // progress comes with every chunk received: the list is read again and redrawn at most four times a second
    m_progressTimer.setSingleShot(true);
    m_progressTimer.setInterval(250);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        regionsUpdated(false);
    });
    connect(m_downloader, &MapDownloader::changed, this, [this] {
        if (!m_progressTimer.isActive())
            m_progressTimer.start();
    });
    connect(m_downloader, &MapDownloader::finished, this, [this](qint64 id, const QString &error) {
        const auto r = MapRegions::byId(id);
        const QString name = r ? r->name : QString();
        if (error.isEmpty())
            Q_EMIT message(i18n("The map of %1 is ready", name), false);
        else if (r && r->status == QLatin1String("failed"))
            Q_EMIT message(i18n("The map of %1 could not be downloaded: %2", name, error), true);
        m_progressTimer.stop();
        regionsUpdated(true);
    });
}

void MapManager::initialize()
{
    MapRegions::reconcile();
    regionsUpdated(true);
    m_downloader->startNext();
}

QString MapManager::folder() const
{
    return MapRegions::folder();
}

double MapManager::freeSpace() const
{
    QDir().mkpath(folder());
    return double(QStorageInfo(folder()).bytesAvailable());
}

QString MapManager::assetsFolder() const
{
    // beside the program first (its own prefix, or the build directory), so an older installed copy is not used instead
    const QString beside = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).filePath(QStringLiteral(KATHENAEUM_MAP_ASSETS));
    if (QFileInfo::exists(beside))
        return beside;
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kathenaeum/map"), QStandardPaths::LocateDirectory);
}

bool MapManager::available() const
{
    return QFile::exists(assetsFolder() + QStringLiteral("/layers-light.json"));
}

QVariantList MapManager::regions() const
{
    QVariantList out;
    for (const auto rows = MapRegions::all(); const MapRegionRecord &r : rows) {
        const bool running = r.id == m_downloader->running();
        out.append(QVariantMap{
            {QStringLiteral("id"), r.id},
            {QStringLiteral("region"), r.region},
            {QStringLiteral("name"), r.region == QLatin1String("world") ? i18n("World overview") : r.name},
            {QStringLiteral("parentName"), r.parentName},
            {QStringLiteral("maxZoom"), r.maxZoom},
            {QStringLiteral("status"), r.status},
            {QStringLiteral("error"), r.error},
            {QStringLiteral("size"), double(r.size)},
            {QStringLiteral("places"), r.places},
            {QStringLiteral("build"), r.build},
            {QStringLiteral("hasMap"), !r.file.isEmpty() && QFile::exists(r.file)},
            {QStringLiteral("step"), running ? m_downloader->step() : QString()},
            {QStringLiteral("done"), running ? double(m_downloader->done()) : 0.0},
            {QStringLiteral("total"), running ? double(m_downloader->total()) : 0.0},
        });
    }
    return out;
}

bool MapManager::hasMaps() const
{
    return !MapRegions::ready().isEmpty();
}

bool MapManager::hasWorld() const
{
    const auto w = MapRegions::byRegion(QStringLiteral("world"));
    return w && (w->isActive() || (!w->file.isEmpty() && QFile::exists(w->file)));
}

void MapManager::regionsUpdated(bool styleToo)
{
    if (styleToo) {
        m_archives.clear(); // a region's file may have been replaced or removed
        ++m_revision;
        Q_EMIT styleChanged();
    }
    Q_EMIT regionsChanged();
}

QString MapManager::styleUrl(bool dark)
{
    const QString assets = assetsFolder();
    QFile layersFile(assets + (dark ? QStringLiteral("/layers-dark.json") : QStringLiteral("/layers-light.json")));
    if (!layersFile.open(QIODevice::ReadOnly))
        return {};
    const QJsonArray base = QJsonDocument::fromJson(layersFile.readAll()).array();

    QJsonObject sources;
    QJsonArray layers;
    for (const QJsonValue &l : base)
        if (l.toObject().value(QStringLiteral("type")).toString() == QLatin1String("background"))
            layers.append(l);
    // the world first, then each region over it: inside a region its detailed land, water and labels cover the world's
    // coarse ones (MapLibre draws layers in order, and places the labels of later layers first)
    for (const auto regions = MapRegions::ready(); const MapRegionRecord &r : regions) {
        const QString source = QStringLiteral("r%1").arg(r.id);
        sources.insert(source,
                       QJsonObject{
                           {QStringLiteral("type"), QStringLiteral("vector")},
                           {QStringLiteral("url"), QString(QStringLiteral("pmtiles://") + QUrl::fromLocalFile(r.file).toString())},
                           {QStringLiteral("attribution"), QStringLiteral("© OpenStreetMap contributors")},
                       });
        for (const QJsonValue &v : base) {
            QJsonObject l = v.toObject();
            if (l.value(QStringLiteral("type")).toString() == QLatin1String("background"))
                continue;
            l.insert(QStringLiteral("id"), QString(l.value(QStringLiteral("id")).toString() + QLatin1Char('@') + source));
            l.insert(QStringLiteral("source"), source);
            layers.append(l);
        }
    }
    const QString flavour = dark ? QStringLiteral("dark") : QStringLiteral("light");
    const QJsonObject style{
        {QStringLiteral("version"), 8},
        {QStringLiteral("glyphs"), QString(QUrl::fromLocalFile(assets + QStringLiteral("/fonts")).toString() + QStringLiteral("/{fontstack}/{range}.pbf"))},
        {QStringLiteral("sprite"), QUrl::fromLocalFile(QString(assets + QStringLiteral("/sprites/") + flavour)).toString()},
        {QStringLiteral("sources"), sources},
        {QStringLiteral("layers"), layers},
    };
    // a new name for each version: MapLibre reloads the style when its address changes
    const QString name = QStringLiteral("map-style-%1-%2.json").arg(flavour).arg(m_revision);
    QDir dir(QFileInfo(cachePath(name)).absolutePath());
    dir.mkpath(QStringLiteral("."));
    for (const auto oldStyles = dir.entryList({QStringLiteral("map-style-%1-*.json").arg(flavour)}, QDir::Files); const QString &old : oldStyles)
        if (old != name)
            dir.remove(old);
    QSaveFile out(cachePath(name));
    if (!out.open(QIODevice::WriteOnly))
        return {};
    out.write(QJsonDocument(style).toJson(QJsonDocument::Compact));
    if (!out.commit())
        return {};
    return QUrl::fromLocalFile(cachePath(name)).toString();
}

// ---- the regions to choose from ----

void MapManager::loadCatalog()
{
    if (m_catalogLoading)
        return;
    auto use = [this](const QByteArray &json) {
        QString error;
        const auto list = MapCatalog::parseIndex(json, &error);
        if (!list)
            return false;
        m_catalog = *list;
        m_catalogIndex.clear();
        for (qsizetype i = 0; i < m_catalog.size(); ++i)
            m_catalogIndex.insert(m_catalog[i].id, i);
        return true;
    };
    // Geofabrik's list changes rarely: kept for a month
    const QString cached = cachePath(QStringLiteral("map-regions.json"));
    const QFileInfo info(cached);
    if (m_catalog.isEmpty() && info.exists() && info.lastModified().daysTo(QDateTime::currentDateTime()) < 30) {
        QFile f(cached);
        if (f.open(QIODevice::ReadOnly) && use(f.readAll())) {
            Q_EMIT catalogChanged();
            return;
        }
    }
    if (!m_catalog.isEmpty())
        return;
    m_catalogLoading = true;
    m_catalogError.clear();
    Q_EMIT catalogChanged();
    QNetworkRequest request = Network::guarded(QNetworkRequest(MapCatalog::indexUrl()));
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_network->get(request);
    Network::allowRedirects(reply, &MapCatalog::isAllowedUrl);
    connect(reply, &QNetworkReply::finished, this, [this, reply, use, cached] {
        reply->deleteLater();
        m_catalogLoading = false;
        const QByteArray json = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && use(json)) {
            QDir().mkpath(QFileInfo(cached).absolutePath());
            QSaveFile f(cached);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(json);
                f.commit();
            }
        } else {
            // an older copy is better than nothing when offline
            QFile f(cached);
            if (!(f.open(QIODevice::ReadOnly) && use(f.readAll())))
                m_catalogError = reply->error() != QNetworkReply::NoError ? reply->errorString() : i18n("The list of regions could not be read");
        }
        Q_EMIT catalogChanged();
    });
}

std::optional<MapRegionInfo> MapManager::info(const QString &id) const
{
    if (id == QLatin1String("world"))
        return MapCatalog::world();
    const auto it = m_catalogIndex.find(id);
    if (it == m_catalogIndex.end())
        return std::nullopt;
    return m_catalog[*it];
}

QString MapManager::parentName(const MapRegionInfo &r) const
{
    const auto parent = info(r.parent);
    return parent ? parent->name : QString();
}

QVariantMap MapManager::toVariant(const MapRegionInfo &r) const
{
    int children = 0;
    for (const MapRegionInfo &c : m_catalog)
        if (c.parent == r.id)
            ++children;
    const auto installed = MapRegions::byRegion(r.id);
    const QRectF b = r.bounds();
    return {
        {QStringLiteral("id"), r.id},
        {QStringLiteral("name"), r.name},
        {QStringLiteral("parentName"), parentName(r)},
        {QStringLiteral("hasChildren"), children > 0},
        {QStringLiteral("childCount"), children},
        {QStringLiteral("continent"), r.parent.isEmpty()},
        {QStringLiteral("status"), installed ? installed->status : QString()},
        {QStringLiteral("installedZoom"), installed ? installed->maxZoom : 0},
        // a rough figure before measuring: tiles at street detail
        {QStringLiteral("streetTiles"), double(MapTiling::tileCount(r.shape, MapCatalog::StreetsZoom, MapCatalog::StreetsZoom))},
        {QStringLiteral("west"), b.left()},
        {QStringLiteral("east"), b.right()},
    };
}

QVariantList MapManager::catalogChildren(const QString &parent) const
{
    QList<const MapRegionInfo *> list;
    for (const MapRegionInfo &r : m_catalog)
        if (r.parent == parent)
            list.append(&r);
    std::sort(list.begin(), list.end(), [](const MapRegionInfo *a, const MapRegionInfo *b) {
        return QString::localeAwareCompare(a->name, b->name) < 0;
    });
    QVariantList out;
    for (const MapRegionInfo *r : std::as_const(list))
        out.append(toVariant(*r));
    return out;
}

QVariantList MapManager::searchCatalog(const QString &text) const
{
    const QString t = text.simplified();
    QVariantList starts, contains;
    if (t.isEmpty())
        return {};
    for (const MapRegionInfo &r : m_catalog) {
        if (r.name.startsWith(t, Qt::CaseInsensitive))
            starts.append(toVariant(r));
        else if (r.name.contains(t, Qt::CaseInsensitive))
            contains.append(toVariant(r));
    }
    return (starts + contains).mid(0, 50);
}

QVariantMap MapManager::regionShape(const QString &id) const
{
    const auto r = info(id);
    if (!r)
        return {};
    // in Web Mercator, so it looks as it does on the map; fitted into a unit box, its aspect kept separately
    // a region across the 180° meridian (Alaska, Russia, Oceania) is drawn whole: cut where its longitudes have their
    // widest gap, not at 180°, and move what is west of the cut round to the east (Antarctica has no gap: left as is)
    std::vector<double> lons;
    for (const MapTiling::Polygon &polygon : r->shape)
        for (const MapTiling::Ring &ring : polygon)
            for (const QPointF &p : ring)
                lons.push_back(p.x());
    std::sort(lons.begin(), lons.end());
    double cut = -INFINITY; // points at or west of it move east
    if (!lons.empty()) {
        double widest = lons.front() + 360 - lons.back(); // the gap across 180°
        for (size_t i = 0; i + 1 < lons.size(); ++i)
            if (lons[i + 1] - lons[i] > widest && lons[i + 1] - lons[i] < 350) { // 350+: a band round the globe (Antarctica)
                widest = lons[i + 1] - lons[i];
                cut = lons[i];
            }
    }
    QList<QList<QPointF>> rings;
    double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (const MapTiling::Polygon &polygon : r->shape)
        for (const MapTiling::Ring &ring : polygon) {
            QList<QPointF> projected;
            for (const QPointF &p : ring) {
                const double lon = p.x() <= cut ? p.x() + 360 : p.x();
                const QPointF q(MapTiling::lonToX(lon, 0), MapTiling::latToY(p.y(), 0));
                projected.append(q);
                minX = std::min(minX, q.x());
                maxX = std::max(maxX, q.x());
                minY = std::min(minY, q.y());
                maxY = std::max(maxY, q.y());
            }
            rings.append(projected);
        }
    const double w = maxX - minX, h = maxY - minY;
    if (rings.isEmpty() || w <= 0 || h <= 0)
        return {};
    const double size = std::max(w, h);
    QVariantList paths;
    for (const QList<QPointF> &ring : std::as_const(rings)) {
        QVariantList points;
        for (const auto simplified = MapPlaces::simplify(ring, size / 300); const QPointF &p : simplified) // a silhouette: a few hundred points at most
            points.append(QPointF((p.x() - minX) / w, (p.y() - minY) / h));
        if (points.size() >= 3)
            paths.append(QVariant(points));
    }
    return {{QStringLiteral("aspect"), w / h}, {QStringLiteral("paths"), paths}};
}

QVariantMap MapManager::catalogRegion(const QString &id) const
{
    const auto r = info(id);
    return r ? toVariant(*r) : QVariantMap();
}

void MapManager::estimate(const QString &id, int maxZoom)
{
    const auto r = info(id);
    if (!r)
        return;
    const quint64 tiles = MapTiling::tileCount(r->shape, 0, maxZoom);
    if (tiles > MapCatalog::MaxTiles) {
        Q_EMIT estimated(id,
                         maxZoom,
                         0,
                         int(std::min<quint64>(tiles, INT_MAX)),
                         0,
                         i18n("This region is too large to download at this detail. Choose less detail, or one of its parts."));
        return;
    }
    // both detail levels of one region are measured at once; measuring another region stops the earlier ones
    for (auto it = m_estimates.begin(); it != m_estimates.end();) {
        if (it.key().section(QLatin1Char('@'), 0, 0) != id || it.key() == id + QLatin1Char('@') + QString::number(maxZoom)) {
            if (*it) {
                (*it)->cancel();
                (*it)->deleteLater();
            }
            it = m_estimates.erase(it);
        } else {
            ++it;
        }
    }
    // the size of the map: the planet build's directories for the region (a second or two)
    QNetworkReply *builds = m_network->get(Network::guarded(QNetworkRequest(MapCatalog::buildsUrl())));
    Network::allowRedirects(builds, &MapCatalog::isAllowedUrl);
    connect(builds, &QNetworkReply::finished, this, [this, builds, id, maxZoom, shape = r->shape, pbf = r->pbf] {
        builds->deleteLater();
        const QString build = MapCatalog::latestBuild(builds->readAll());
        if (build.isEmpty()) {
            Q_EMIT estimated(id, maxZoom, 0, 0, 0, i18n("Could not reach Protomaps: %1", builds->errorString()));
            return;
        }
        auto *extract = new MapExtract(nullptr, shape, maxZoom, QString(), this);
        extract->setSource(new HttpRangeReader(MapCatalog::buildUrl(build), m_network, extract)); // goes with it
        extract->setEstimateOnly(true);
        const QString key = id + QLatin1Char('@') + QString::number(maxZoom);
        m_estimates.insert(key, extract);
        connect(extract, &MapExtract::finished, this, [this, extract, key, id, maxZoom, pbf](const QString &error) {
            extract->deleteLater();
            m_estimates.remove(key);
            const double mapBytes = double(extract->totalBytes());
            const int tiles = int(extract->tileCount());
            if (!error.isEmpty() || pbf.isEmpty()) {
                Q_EMIT estimated(id, maxZoom, mapBytes, tiles, 0, error);
                return;
            }
            // and the size of its OpenStreetMap data (read for places, then deleted)
            QNetworkReply *reply = m_network->head(Network::guarded(QNetworkRequest(pbf)));
            Network::allowRedirects(reply, &MapCatalog::isAllowedUrl);
            connect(reply, &QNetworkReply::finished, this, [this, reply, id, maxZoom, mapBytes, tiles] {
                reply->deleteLater();
                Q_EMIT estimated(id, maxZoom, mapBytes, tiles, reply->header(QNetworkRequest::ContentLengthHeader).toDouble(), QString());
            });
        });
        extract->start();
    });
}

void MapManager::download(const QString &id, int maxZoom)
{
    const auto r = info(id);
    if (!r)
        return;
    QString error;
    if (!MapRegions::add(*r, parentName(*r), maxZoom, &error)) {
        Q_EMIT message(i18n("Could not add %1: %2", r->name, error), true);
        return;
    }
    regionsUpdated(false);
    m_downloader->startNext();
}

void MapManager::downloadWorld()
{
    download(QStringLiteral("world"), MapCatalog::WorldZoom);
}

void MapManager::cancel(qint64 regionId)
{
    m_downloader->cancel(regionId);
    regionsUpdated(false);
}

void MapManager::retry(qint64 regionId)
{
    if (const auto r = MapRegions::byId(regionId); r && !r->isActive()) {
        MapRegions::setStatus(regionId, QStringLiteral("queued"));
        regionsUpdated(false);
        m_downloader->startNext();
    }
}

void MapManager::remove(qint64 regionId)
{
    if (regionId == m_downloader->running())
        m_downloader->cancel(regionId);
    m_archives.clear();
    QString error;
    if (!MapRegions::remove(regionId, &error) && !error.isEmpty())
        Q_EMIT message(error, true);
    regionsUpdated(true);
}

// ---- the map ----

QVariantList MapManager::coordinateAt(double lat, double lon, double zoom, double dx, double dy) const
{
    const QPointF p = MapLookup::coordinateAt(lat, lon, zoom, dx, dy);
    return {p.x(), p.y()};
}

PMTiles::Archive *MapManager::archive(const QString &path)
{
    auto it = m_archives.find(path);
    if (it == m_archives.end())
        it = m_archives.insert(path, std::make_shared<PMTiles::Archive>(path));
    return it->get();
}

QVariantMap MapManager::articleFor(const QString &name, const QString &nameEn, const MapPlace *place, double lat, double lon) const
{
    // the region it is in, for titles like "Salem, Oregon"
    QStringList regionNames;
    for (const auto regions = MapRegions::ready(); const MapRegionRecord &r : regions)
        if (r.region != QLatin1String("world") && MapTiling::contains(r.shape, lon, lat))
            regionNames.append(r.name);

    for (const auto rows = Library::all(); const LibraryRecord &lib : rows) {
        if (!lib.fileExists())
            continue;
        const QString language = twoLetter(lib.language);
        auto found = [&](const QString &title, bool exact) -> QVariantMap {
            if (title.isEmpty() || !ZimReader::hasEntry(lib.id, pathFor(title)))
                return {};
            return {{QStringLiteral("libraryId"), lib.id},
                    {QStringLiteral("path"), pathFor(title)},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("library"), lib.title},
                    {QStringLiteral("exact"), exact}};
        };
        // the article OpenStreetMap names
        if (place) {
            if (const QVariantMap a = found(MapPlaces::articleTitle(*place, language), true); !a.isEmpty())
                return a;
        }
        // else Wikipedia's usual titles for a place: "Salem, Oregon", then the name alone
        const QString local = language == QLatin1String("en") && !nameEn.isEmpty() ? nameEn : name;
        for (const QString &region : std::as_const(regionNames))
            if (const QVariantMap a = found(local + QStringLiteral(", ") + region, false); !a.isEmpty())
                return a;
        if (const QVariantMap a = found(local, false); !a.isEmpty())
            return a;
    }
    return {};
}

QVariantMap MapManager::placeAt(double lat, double lon, double zoom, double radiusPx)
{
    // the labelled point nearest the click, from the regions the click is in (the detailed ones first) and the world
    std::optional<MapFeature> feature;
    QList<MapRegionRecord> ready = MapRegions::ready();
    std::stable_sort(ready.begin(), ready.end(), [](const MapRegionRecord &a, const MapRegionRecord &b) {
        return a.maxZoom > b.maxZoom;
    });
    for (const MapRegionRecord &r : std::as_const(ready)) {
        if (r.region != QLatin1String("world") && !r.bounds.adjusted(-0.05, -0.05, 0.05, 0.05).contains(QPointF(lon, lat)))
            continue;
        if (const auto f = MapLookup::nearest(*archive(r.file), lat, lon, zoom, radiusPx); f && (!feature || f->distancePx < feature->distancePx))
            feature = f;
        if (feature && r.region != QLatin1String("world"))
            break; // a region's own tiles know better than the world's
    }

    QVariantMap out;
    std::optional<MapPlace> place;
    QList<MapPlace> around = feature ? QList<MapPlace>() : MapPlaces::around(lat, lon, 6);
    if (feature) {
        place = feature->osm.isEmpty() ? std::nullopt : MapPlaces::byOsm(feature->osm);
        out.insert(QStringLiteral("name"), feature->name);
        out.insert(QStringLiteral("kindLabel"), kindLabel(place ? place->kind : feature->kind));
        out.insert(QStringLiteral("lat"), feature->lat);
        out.insert(QStringLiteral("lon"), feature->lon);
        out.insert(QStringLiteral("osm"), feature->osm);
        const QVariantMap article = articleFor(feature->name, feature->nameEn, place ? &*place : nullptr, feature->lat, feature->lon);
        if (!article.isEmpty())
            out.insert(QStringLiteral("article"), article);
    } else if (!around.isEmpty()) {
        // no label near the click: the smallest area it is in (a building, a park, a lake)
        place = around.takeFirst();
        out.insert(QStringLiteral("name"), place->name);
        out.insert(QStringLiteral("kindLabel"), kindLabel(place->kind));
        out.insert(QStringLiteral("lat"), lat);
        out.insert(QStringLiteral("lon"), lon);
        out.insert(QStringLiteral("osm"), place->osm);
        const QVariantMap article = articleFor(place->name, place->nameEn, &*place, place->lat, place->lon);
        if (!article.isEmpty())
            out.insert(QStringLiteral("article"), article);
    } else {
        return {};
    }
    // and what it is in: its park, its town, its county
    out.insert(QStringLiteral("around"), aroundList(lat, lon, place ? &*place : nullptr, out.value(QStringLiteral("name")).toString()));
    return out;
}

QVariantList MapManager::aroundList(double lat, double lon, const MapPlace *place, const QString &name) const
{
    QVariantList inside;
    for (const auto nearby = MapPlaces::around(lat, lon, 6); const MapPlace &p : nearby) {
        if (place && (p.osm == place->osm || (!p.wikipedia.isEmpty() && p.wikipedia == place->wikipedia)))
            continue;
        if (p.name == name)
            continue;
        QVariantMap a{{QStringLiteral("name"), p.name}, {QStringLiteral("kindLabel"), kindLabel(p.kind)}};
        const QVariantMap article = articleFor(p.name, p.nameEn, &p, p.lat, p.lon);
        if (!article.isEmpty())
            a.insert(QStringLiteral("article"), article);
        inside.append(a);
    }
    return inside;
}

QVariantMap MapManager::locate(qint64 libraryId, const QString &title, bool hasCoordinates, double lat, double lon) const
{
    const QList<MapRegionRecord> ready = MapRegions::ready();
    if (ready.isEmpty())
        return {};
    const auto library = Library::byId(libraryId);
    const QString language = library ? twoLetter(library->language) : QString();
    auto regionAt = [&ready](double la, double lo) -> std::optional<MapRegionRecord> {
        std::optional<MapRegionRecord> best;
        for (const MapRegionRecord &r : ready)
            if ((r.region == QLatin1String("world") || MapTiling::contains(r.shape, lo, la)) && (!best || r.maxZoom > best->maxZoom))
                best = r;
        return best;
    };

    QVariantMap out;
    std::optional<MapPlace> place = MapPlaces::byArticle(language, title);
    std::optional<MapRegionRecord> region = place ? regionAt(place->lat, place->lon) : std::nullopt;
    if (place && region) {
        // the place OpenStreetMap links to this article: zoomed to fit it
        const double span = std::max(place->east - place->west, place->north - place->south);
        const double zoom = span > 0 ? std::clamp(std::log2(360.0 / span) - 0.5, 4.0, 16.0) : 15.0;
        out = {{QStringLiteral("lat"), place->lat},
               {QStringLiteral("lon"), place->lon},
               {QStringLiteral("zoom"), std::min(zoom, double(region->maxZoom) + 1)},
               {QStringLiteral("name"), place->name},
               {QStringLiteral("kindLabel"), kindLabel(place->kind)}};
        lat = place->lat;
        lon = place->lon;
    } else if (hasCoordinates && (region = regionAt(lat, lon))) {
        // the article's own coordinates: close in on a detailed region, the whole country on the world overview
        place.reset();
        const bool world = region->region == QLatin1String("world");
        out = {{QStringLiteral("lat"), lat},
               {QStringLiteral("lon"), lon},
               {QStringLiteral("zoom"), world ? 5.0 : 12.0},
               {QStringLiteral("name"), title},
               {QStringLiteral("kindLabel"), QString()}};
    } else {
        return {};
    }
    out.insert(QStringLiteral("around"), aroundList(lat, lon, place ? &*place : nullptr, out.value(QStringLiteral("name")).toString()));
    return out;
}

QVariantList MapManager::searchPlaces(const QString &text) const
{
    QVariantList out;
    for (const auto matches = MapPlaces::search(text, 20); const MapPlace &p : matches) {
        // zoom to fit an area, close in on a point
        const double span = std::max(p.east - p.west, p.north - p.south);
        const double zoom = span > 0 ? std::clamp(std::log2(360.0 / span) - 0.5, 4.0, 16.0) : 15.0;
        out.append(QVariantMap{{QStringLiteral("name"), p.name},
                               {QStringLiteral("kindLabel"), kindLabel(p.kind)},
                               {QStringLiteral("lat"), p.lat},
                               {QStringLiteral("lon"), p.lon},
                               {QStringLiteral("zoom"), zoom}});
    }
    return out;
}

void MapManager::saveView(double lat, double lon, double zoom)
{
    if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(zoom))
        return;
    AppSettings::setValue(QStringLiteral("map.view"), QStringLiteral("%1,%2,%3").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(zoom, 0, 'f', 2));
}

QVariantMap MapManager::startView() const
{
    // where it was left: the page is made anew when it is opened from the sidebar, and it opened zoomed out every time
    const QStringList saved = AppSettings::value(QStringLiteral("map.view")).split(QLatin1Char(','));
    if (saved.size() == 3 && hasMaps()) {
        bool a = false, b = false, c = false;
        const double lat = saved[0].toDouble(&a), lon = saved[1].toDouble(&b), zoom = saved[2].toDouble(&c);
        if (a && b && c)
            return {{QStringLiteral("lat"), lat}, {QStringLiteral("lon"), lon}, {QStringLiteral("zoom"), zoom}};
    }
    std::optional<MapRegionRecord> latest;
    for (const auto regions = MapRegions::ready(); const MapRegionRecord &r : regions)
        if (r.region != QLatin1String("world") && (!latest || r.updated > latest->updated))
            latest = r;
    if (!latest)
        return {{QStringLiteral("lat"), 20.0}, {QStringLiteral("lon"), 0.0}, {QStringLiteral("zoom"), 1.5}};
    const QRectF b = latest->bounds;
    const double span = std::max(b.width(), b.height());
    return {{QStringLiteral("lat"), b.center().y()},
            {QStringLiteral("lon"), b.center().x()},
            {QStringLiteral("zoom"), std::clamp(std::log2(360.0 / std::max(span, 0.01)), 2.0, 14.0)}};
}
