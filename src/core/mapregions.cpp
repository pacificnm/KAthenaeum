// SPDX-License-Identifier: GPL-3.0-or-later
#include "mapregions.h"

#include "database.h"
#include "mapextract.h"
#include "mapplaces.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimeZone>
#include <QtConcurrent>

// ---- the catalog ----

QUrl MapCatalog::indexUrl()
{
    return QUrl(QStringLiteral("https://download.geofabrik.de/index-v1.json"));
}

QUrl MapCatalog::buildsUrl()
{
    return QUrl(QStringLiteral("https://build-metadata.protomaps.dev/builds.json"));
}

QUrl MapCatalog::buildUrl(const QString &key)
{
    return QUrl(QStringLiteral("https://build.protomaps.com/") + key);
}

bool MapCatalog::isAllowedUrl(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https") || !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443))
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("download.geofabrik.de") || host == QLatin1String("build.protomaps.com")
        || host == QLatin1String("build-metadata.protomaps.dev");
}

std::optional<QList<MapRegionInfo>> MapCatalog::parseIndex(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        if (error)
            *error = parseError.errorString();
        return std::nullopt;
    }
    QList<MapRegionInfo> out;
    for (const QJsonValue &f : doc.object().value(QStringLiteral("features")).toArray()) {
        const QJsonObject props = f.toObject().value(QStringLiteral("properties")).toObject();
        MapRegionInfo r;
        r.id = props.value(QStringLiteral("id")).toString();
        r.parent = props.value(QStringLiteral("parent")).toString();
        r.name = props.value(QStringLiteral("name")).toString();
        r.pbf = QUrl(props.value(QStringLiteral("urls")).toObject().value(QStringLiteral("pbf")).toString());
        r.shape = MapTiling::fromGeoJson(f.toObject().value(QStringLiteral("geometry")).toObject().toVariantMap());
        if (r.id.isEmpty() || r.name.isEmpty() || r.shape.isEmpty() || !isAllowedUrl(r.pbf))
            continue;
        // Geofabrik's list has quirks (2026-10-19): Polish and Norwegian names hold "<br />" before their English name,
        // and the US states are named by their id ("us/oregon") and listed under North America, not the United States
        r.name.replace(QStringLiteral("<br />"), QStringLiteral(" "));
        r.name = r.name.simplified();
        const qsizetype slash = r.id.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            r.parent = r.id.left(slash);
            if (r.name == r.id) {
                static const QSet<QString> small{QStringLiteral("of"), QStringLiteral("and"), QStringLiteral("the")};
                QStringList words = r.id.mid(slash + 1).split(QLatin1Char('-'));
                for (qsizetype i = 0; i < words.size(); ++i)
                    if (words[i] == QLatin1String("us"))
                        words[i] = QStringLiteral("US");
                    else if (i == 0 || !small.contains(words[i]))
                        words[i] = words[i].left(1).toUpper() + words[i].mid(1);
                r.name = words.join(QLatin1Char(' '));
            }
        }
        out.append(r);
    }
    // a region whose parent is not listed goes under its continent's list (none expected)
    QSet<QString> ids;
    for (const MapRegionInfo &r : std::as_const(out))
        ids.insert(r.id);
    for (MapRegionInfo &r : out)
        if (!r.parent.isEmpty() && !ids.contains(r.parent))
            r.parent.clear();
    if (out.isEmpty()) {
        if (error)
            *error = QObject::tr("The list of regions is empty");
        return std::nullopt;
    }
    return out;
}

QByteArray MapCatalog::shapeToJson(const MapTiling::MultiPolygon &shape)
{
    QJsonArray polygons;
    for (const MapTiling::Polygon &p : shape) {
        QJsonArray rings;
        for (const MapTiling::Ring &r : p) {
            QJsonArray points;
            for (const QPointF &pt : r)
                points.append(QJsonArray{pt.x(), pt.y()});
            rings.append(points);
        }
        polygons.append(rings);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("MultiPolygon")}, {QStringLiteral("coordinates"), polygons}})
        .toJson(QJsonDocument::Compact);
}

QString MapCatalog::latestBuild(const QByteArray &json)
{
    QString best;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QString key = v.toObject().value(QStringLiteral("key")).toString();
        // "20261019.pmtiles": the newest is the greatest
        static const QRegularExpression pattern(QStringLiteral("^\\d{8}\\.pmtiles$"));
        if (pattern.match(key).hasMatch() && key > best)
            best = key;
    }
    return best;
}

MapRegionInfo MapCatalog::world()
{
    MapRegionInfo w;
    w.id = QStringLiteral("world");
    w.name = QObject::tr("World overview");
    w.shape = {{{{-180, -85.06}, {180, -85.06}, {180, 85.06}, {-180, 85.06}, {-180, -85.06}}}};
    return w;
}

// ---- stored regions ----

bool MapRegionRecord::isActive() const
{
    return status == QLatin1String("queued") || status == QLatin1String("downloading") || status == QLatin1String("indexing");
}

namespace
{
const QString Columns = QStringLiteral("id, region, name, parent_name, max_zoom, west, south, east, north, status, error, file, size, build, places, "
                                       "updated_at, shape, pbf");

MapRegionRecord fromRow(const QSqlQuery &q)
{
    MapRegionRecord r;
    r.id = q.value(0).toLongLong();
    r.region = q.value(1).toString();
    r.name = q.value(2).toString();
    r.parentName = q.value(3).toString();
    r.maxZoom = q.value(4).toInt();
    const double w = q.value(5).toDouble(), s = q.value(6).toDouble(), e = q.value(7).toDouble(), n = q.value(8).toDouble();
    r.bounds = QRectF(w, s, e - w, n - s);
    r.status = q.value(9).toString();
    r.error = q.value(10).toString();
    r.file = q.value(11).toString();
    r.size = q.value(12).toLongLong();
    r.build = q.value(13).toString();
    r.places = q.value(14).toInt();
    r.updated = QDateTime::fromString(q.value(15).toString(), Qt::ISODateWithMs);
    r.updated.setTimeZone(QTimeZone::UTC);
    r.shape = MapTiling::fromGeoJson(QJsonDocument::fromJson(q.value(16).toByteArray()).object().toVariantMap());
    r.pbf = QUrl(q.value(17).toString());
    return r;
}
}

QString MapRegions::folder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("maps"));
}

QList<MapRegionRecord> MapRegions::all()
{
    QList<MapRegionRecord> out;
    QSqlQuery q;
    if (q.exec(QStringLiteral("SELECT %1 FROM map_regions ORDER BY region != 'world', name COLLATE NOCASE").arg(Columns)))
        while (q.next())
            out.append(fromRow(q));
    return out;
}

std::optional<MapRegionRecord> MapRegions::byId(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM map_regions WHERE id = ?").arg(Columns));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return fromRow(q);
    return std::nullopt;
}

std::optional<MapRegionRecord> MapRegions::byRegion(const QString &region)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM map_regions WHERE region = ?").arg(Columns));
    q.addBindValue(region);
    if (q.exec() && q.next())
        return fromRow(q);
    return std::nullopt;
}

qint64 MapRegions::add(const MapRegionInfo &info, const QString &parentName, int maxZoom, QString *error)
{
    const QRectF b = info.bounds();
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO map_regions (region, name, parent_name, max_zoom, shape, pbf, west, south, east, north, status) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'queued') "
                             "ON CONFLICT (region) DO UPDATE SET name = excluded.name, parent_name = excluded.parent_name, max_zoom = excluded.max_zoom, "
                             "shape = excluded.shape, pbf = excluded.pbf, "
                             "status = 'queued', error = '', updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now')"));
    q.addBindValue(info.id);
    q.addBindValue(info.name);
    q.addBindValue(Database::text(parentName));
    q.addBindValue(maxZoom);
    q.addBindValue(QString::fromUtf8(MapCatalog::shapeToJson(info.shape)));
    q.addBindValue(Database::text(info.pbf.toString()));
    q.addBindValue(b.left());
    q.addBindValue(b.top());
    q.addBindValue(b.right());
    q.addBindValue(b.bottom());
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return 0;
    }
    const auto r = byRegion(info.id);
    return r ? r->id : 0;
}

bool MapRegions::setStatus(qint64 id, const QString &status, const QString &error)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE map_regions SET status = ?, error = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    q.addBindValue(status);
    q.addBindValue(Database::text(error));
    q.addBindValue(id);
    return q.exec();
}

bool MapRegions::setDone(qint64 id, const QString &file, qint64 size, const QString &build, int places)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE map_regions SET status = 'complete', error = '', file = ?, size = ?, build = ?, places = ?, "
                             "updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    q.addBindValue(file);
    q.addBindValue(size);
    q.addBindValue(Database::text(build));
    q.addBindValue(places);
    q.addBindValue(id);
    return q.exec();
}

bool MapRegions::remove(qint64 id, QString *error)
{
    const auto r = byId(id);
    if (!r)
        return false;
    MapPlaces::removeRegion(id);
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM map_regions WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    // only files in the maps folder are ever deleted
    if (!r->file.isEmpty() && QFileInfo(r->file).absolutePath() == QDir(folder()).absolutePath())
        QFile::remove(r->file);
    return true;
}

void MapRegions::reconcile()
{
    QSqlQuery q;
    q.exec(QStringLiteral("UPDATE map_regions SET status = 'failed', error = 'Stopped when KAthenaeum closed' "
                          "WHERE status IN ('downloading', 'indexing')"));
}

QList<MapRegionRecord> MapRegions::ready()
{
    QList<MapRegionRecord> out;
    for (const MapRegionRecord &r : all())
        if (!r.file.isEmpty() && QFile::exists(r.file))  // a region being updated keeps showing its old map
            out.append(r);
    return out;
}

// ---- downloading ----

MapDownloader::MapDownloader(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

MapDownloader::~MapDownloader()
{
    *m_stop = true;
    if (m_reply)
        m_reply->abort();
}

QString MapDownloader::pbfPath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("map-%1.osm.pbf").arg(m_id));
}

void MapDownloader::startNext()
{
    if (m_id)
        return;
    std::optional<MapRegionRecord> next;
    for (const MapRegionRecord &r : MapRegions::all())
        if (r.status == QLatin1String("queued") && (!next || r.updated < next->updated))
            next = r;
    if (!next)
        return;
    m_id = next->id;
    m_info.id = next->region;
    m_info.name = next->name;
    m_info.shape = next->shape;
    m_info.pbf = next->pbf;
    m_done = m_total = 0;
    m_step = QStringLiteral("map");
    *m_stop = false;
    MapRegions::setStatus(m_id, QStringLiteral("downloading"));
    Q_EMIT changed(m_id);
    if (m_info.shape.isEmpty())
        return finish(tr("The region has no shape"));

    // the newest planet build
    QNetworkReply *reply = m_network->get(QNetworkRequest(MapCatalog::buildsUrl()));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, id = m_id] {
        reply->deleteLater();
        if (id != m_id)
            return;
        if (reply->error() != QNetworkReply::NoError)
            return finish(tr("Could not reach Protomaps for the map: %1").arg(reply->errorString()));
        const QString build = MapCatalog::latestBuild(reply->readAll());
        if (build.isEmpty())
            return finish(tr("Protomaps lists no map to download"));
        gotBuild(build);
    });
}

void MapDownloader::gotBuild(const QString &build)
{
    m_build = build;
    const auto record = MapRegions::byId(m_id);
    if (!record)
        return finish(tr("The region was removed"));
    QDir().mkpath(MapRegions::folder());
    QString safe = m_info.id;
    safe.replace(QLatin1Char('/'), QLatin1Char('-'));
    m_mapFile = QDir(MapRegions::folder()).filePath(safe + QStringLiteral(".pmtiles"));
    // the new file replaces the old one only once it is complete (QSaveFile): an update keeps the old map until then
    m_extract = new MapExtract(nullptr, m_info.shape, record->maxZoom, m_mapFile, this);
    m_extract->setSource(new HttpRangeReader(MapCatalog::buildUrl(build), m_network, m_extract));  // goes with it
    connect(m_extract, &MapExtract::progress, this, [this](qint64 done, qint64 total) {
        m_done = done;
        m_total = total;
        Q_EMIT changed(m_id);
    });
    connect(m_extract, &MapExtract::finished, this, [this](const QString &error) {
        m_extract->deleteLater();
        if (!error.isEmpty())
            return finish(tr("The map could not be downloaded: %1").arg(error));
        if (m_info.pbf.isEmpty())
            return finish({});  // the world overview has no places of its own
        fetchPlaces();
    });
    m_extract->start();
}

void MapDownloader::fetchPlaces()
{
    m_step = QStringLiteral("places");
    m_done = m_total = 0;
    Q_EMIT changed(m_id);
    if (!MapCatalog::isAllowedUrl(m_info.pbf))
        return finish(tr("The region's OpenStreetMap data is not on Geofabrik"));
    QDir().mkpath(QFileInfo(pbfPath()).absolutePath());
    m_pbf = std::make_unique<QFile>(pbfPath());
    if (!m_pbf->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return finish(tr("Could not write %1: %2").arg(pbfPath(), m_pbf->errorString()));
    QNetworkRequest request(m_info.pbf);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120000);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl &url) {
        if (!MapCatalog::isAllowedUrl(url))
            reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_pbf && m_pbf->write(reply->readAll()) < 0)
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
        m_done = done;
        m_total = total;
        Q_EMIT changed(m_id);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_pbf)
            return;
        m_pbf->write(reply->readAll());
        m_pbf->close();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(pbfPath());
            return finish(tr("Could not download the region's OpenStreetMap data: %1").arg(reply->errorString()));
        }
        indexPlaces();
    });
}

void MapDownloader::indexPlaces()
{
    m_step = QStringLiteral("indexing");
    MapRegions::setStatus(m_id, QStringLiteral("indexing"));
    Q_EMIT changed(m_id);
    using Result = std::pair<std::optional<std::vector<MapPlace>>, QString>;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, id = m_id] {
        watcher->deleteLater();
        QFile::remove(pbfPath());
        if (id != m_id)
            return;
        const Result result = watcher->result();
        if (!result.first)
            return finish(result.second.isEmpty() ? tr("Stopped") : tr("The region's OpenStreetMap data could not be read: %1").arg(result.second));
        QString error;
        if (!MapPlaces::store(m_id, *result.first, &error))
            return finish(tr("Could not save the region's places: %1").arg(error));
        finish({});
    });
    watcher->setFuture(QtConcurrent::run([path = pbfPath(), stop = m_stop] {
        QString error;
        auto places = MapPlaces::read(path, *stop, &error);
        return Result(std::move(places), error);
    }));
}

void MapDownloader::finish(const QString &error)
{
    const qint64 id = std::exchange(m_id, 0);
    if (!id)
        return;
    m_pbf.reset();
    if (error.isEmpty()) {
        MapRegions::setDone(id, m_mapFile, QFileInfo(m_mapFile).size(), m_build.chopped(8), MapPlaces::count(id));
    } else if (const auto r = MapRegions::byId(id); r && r->status != QLatin1String("cancelled")) {
        MapRegions::setStatus(id, QStringLiteral("failed"), error);
    }
    m_step.clear();
    Q_EMIT changed(id);
    Q_EMIT finished(id, error);
    startNext();
}

bool MapDownloader::cancel(qint64 id)
{
    const auto r = MapRegions::byId(id);
    if (!r || !r->isActive())
        return false;
    MapRegions::setStatus(id, QStringLiteral("cancelled"));
    if (id == m_id) {
        *m_stop = true;
        if (m_extract) {
            m_extract->cancel();
            m_extract->deleteLater();
        }
        if (m_reply)
            m_reply->abort();
        QFile::remove(pbfPath());
        finish(tr("Cancelled"));
    } else {
        Q_EMIT changed(id);
    }
    return true;
}
