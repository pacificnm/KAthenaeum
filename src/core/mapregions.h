// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "maptiling.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QUrl>
#include <atomic>
#include <memory>
#include <optional>

class MapExtract;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// A region that can be downloaded: Geofabrik's list (continents, countries, states and smaller), with each region's
// shape and its OpenStreetMap extract. The map itself is cut from Protomaps' planet build to that shape.
struct MapRegionInfo {
    QString id;  // "us/oregon"
    QString parent;  // "north-america" ("" for a continent)
    QString name;  // "Oregon"
    QUrl pbf;  // its OpenStreetMap extract
    MapTiling::MultiPolygon shape;
    QRectF bounds() const { return MapTiling::bounds(shape); }
};

namespace MapCatalog
{
QUrl indexUrl();  // Geofabrik's region list with shapes, ~3.7 MB
QUrl buildsUrl();  // Protomaps' list of planet builds
QUrl buildUrl(const QString &key);  // "20261019.pmtiles" -> its address
// The only hosts maps are fetched from (https): Geofabrik for regions and OpenStreetMap data, Protomaps for the map
bool isAllowedUrl(const QUrl &url);

std::optional<QList<MapRegionInfo>> parseIndex(const QByteArray &json, QString *error = nullptr);
QByteArray shapeToJson(const MapTiling::MultiPolygon &shape);  // as GeoJSON MultiPolygon coordinates
// The newest planet build from Protomaps' list ("20261019.pmtiles"); empty if none
QString latestBuild(const QByteArray &json);

// The overview of the whole world: country and city names, coasts and main roads (zoom 0 to 6, about 45 MB)
constexpr int WorldZoom = 6;
MapRegionInfo world();

// Detail levels offered for a region
constexpr int StreetsZoom = 15;  // streets and buildings, the most the planet build has
constexpr int RoadsZoom = 13;  // roads, towns and parks: about a fifth of the size
// Larger extracts would hold millions of tiles in memory while they are cut: choose less detail or a smaller region
constexpr quint64 MaxTiles = 4'000'000;
}

struct MapRegionRecord {
    qint64 id = 0;
    QString region;
    MapTiling::MultiPolygon shape;
    QUrl pbf;
    QString name;
    QString parentName;
    int maxZoom = 0;
    QRectF bounds;  // x = west, y = south
    QString status;  // queued | downloading | indexing | complete | failed | cancelled
    QString error;
    QString file;
    qint64 size = 0;
    QString build;
    int places = 0;
    QDateTime updated;

    bool isActive() const;
};

namespace MapRegions
{
QString folder();  // <AppDataLocation>/maps
QList<MapRegionRecord> all();  // the world first, then by name
std::optional<MapRegionRecord> byId(qint64 id);
std::optional<MapRegionRecord> byRegion(const QString &region);
// Adds a region to download (or queues an existing one again, with this detail). Returns its id, 0 on failure.
qint64 add(const MapRegionInfo &info, const QString &parentName, int maxZoom, QString *error = nullptr);
bool setStatus(qint64 id, const QString &status, const QString &error = {});
bool setDone(qint64 id, const QString &file, qint64 size, const QString &build, int places);
// Removes a region, its map file and its places
bool remove(qint64 id, QString *error = nullptr);
// Regions left downloading by a previous run (the app was closed): failed, to retry
void reconcile();
// The regions whose map is ready, for the map's style
QList<MapRegionRecord> ready();
}

// Downloads queued regions one at a time: the map (cut from the newest planet build), then, except for the world
// overview, the region's OpenStreetMap extract, read for places and deleted.
class MapDownloader : public QObject
{
    Q_OBJECT
public:
    explicit MapDownloader(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~MapDownloader() override;

    void startNext();  // starts the next queued region unless one is running
    bool cancel(qint64 id);
    qint64 running() const { return m_id; }
    // what the running region is doing: "map" (cutting the map), "places" (fetching OpenStreetMap data), "indexing"
    QString step() const { return m_step; }
    qint64 done() const { return m_done; }
    qint64 total() const { return m_total; }

Q_SIGNALS:
    void changed(qint64 id);  // status or progress
    void finished(qint64 id, const QString &error);

private:
    void gotBuild(const QString &build);
    void fetchPlaces();
    void indexPlaces();
    void finish(const QString &error);
    QString pbfPath() const;

    QNetworkAccessManager *m_network;
    qint64 m_id = 0;
    MapRegionInfo m_info;
    QString m_build;
    QString m_mapFile;
    QString m_step;
    qint64 m_done = 0;
    qint64 m_total = 0;
    QPointer<MapExtract> m_extract;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_pbf;
    std::shared_ptr<std::atomic<bool>> m_stop = std::make_shared<std::atomic<bool>>(false);
};
