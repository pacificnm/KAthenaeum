// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/mapregions.h"

#include <QHash>
#include <QPointer>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

class MapDownloader;
class MapExtract;
class QNetworkAccessManager;
class QQmlEngine;
class QJSEngine;
struct MapPlace;
namespace PMTiles
{
class Archive;
}

// Maps, for the map page and Settings: the regions downloaded and downloading, the list of regions to choose from, the
// map's style (made from the downloaded regions), and what a click on the map is (and its Wikipedia article).
class MapManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
    Q_PROPERTY(bool hasMaps READ hasMaps NOTIFY regionsChanged)
    Q_PROPERTY(bool hasWorld READ hasWorld NOTIFY regionsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY styleChanged)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged)
    Q_PROPERTY(bool catalogLoaded READ catalogLoaded NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(double freeSpace READ freeSpace NOTIFY regionsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    static MapManager *createInstance(QObject *parent);
    static MapManager *instance();
    static MapManager *create(QQmlEngine *, QJSEngine *);

    void initialize();  // after the database is open: regions left downloading by a previous run are marked failed

    QVariantList regions() const;
    bool hasMaps() const;
    bool hasWorld() const;
    int revision() const { return m_revision; }
    bool catalogLoading() const { return m_catalogLoading; }
    bool catalogLoaded() const { return !m_catalog.isEmpty(); }
    QString catalogError() const { return m_catalogError; }
    QString folder() const;
    double freeSpace() const;
    bool available() const;  // the map's fonts and style are installed

    // The map's style, light or dark, made from the regions ready to show: a file URL
    Q_INVOKABLE QString styleUrl(bool dark);

    // ---- the regions to choose from (Geofabrik's list), for Settings ----
    Q_INVOKABLE void loadCatalog();
    Q_INVOKABLE QVariantList catalogChildren(const QString &parent) const;  // "" for the continents
    Q_INVOKABLE QVariantList searchCatalog(const QString &text) const;
    Q_INVOKABLE QVariantMap catalogRegion(const QString &id) const;
    // The region's outline for drawing it: {aspect: width / height, paths: [[point 0..1, …], …]} in Web Mercator
    Q_INVOKABLE QVariantMap regionShape(const QString &id) const;
    // How big the region's map would be at this detail: answered by estimated()
    Q_INVOKABLE void estimate(const QString &id, int maxZoom);
    Q_INVOKABLE void download(const QString &id, int maxZoom);
    Q_INVOKABLE void downloadWorld();
    Q_INVOKABLE void cancel(qint64 regionId);
    Q_INVOKABLE void retry(qint64 regionId);
    Q_INVOKABLE void remove(qint64 regionId);

    // ---- the map ----
    // [lat, lon] under a point dx, dy pixels from the centre of the map showing (lat, lon) at zoom
    Q_INVOKABLE QVariantList coordinateAt(double lat, double lon, double zoom, double dx, double dy) const;
    // What is at (lat, lon): {name, kindLabel, lat, lon, article: {libraryId, path, title, exact} | absent, around: [...]}
    // or an empty map if nothing is
    Q_INVOKABLE QVariantMap placeAt(double lat, double lon, double zoom, double radiusPx);
    // Places by name, for the map's search box: [{name, kindLabel, region, lat, lon, zoom}]
    Q_INVOKABLE QVariantList searchPlaces(const QString &text) const;
    // The region to show first: the last one downloaded ({lat, lon, zoom}), or the world
    Q_INVOKABLE QVariantMap startView() const;

Q_SIGNALS:
    void regionsChanged();
    void styleChanged();
    void catalogChanged();
    void estimated(const QString &id, int maxZoom, double mapBytes, int tiles, double osmBytes, const QString &error);
    void message(const QString &text, bool isError);

private:
    explicit MapManager(QObject *parent);
    void regionsUpdated(bool styleToo);
    std::optional<MapRegionInfo> info(const QString &id) const;
    QString parentName(const MapRegionInfo &r) const;
    QVariantMap toVariant(const MapRegionInfo &r) const;
    QVariantMap articleFor(const QString &name, const QString &nameEn, const MapPlace *place, double lat, double lon) const;
    PMTiles::Archive *archive(const QString &path);
    QString assetsFolder() const;

    QNetworkAccessManager *m_network;
    MapDownloader *m_downloader;
    QList<MapRegionInfo> m_catalog;
    QHash<QString, qsizetype> m_catalogIndex;
    bool m_catalogLoading = false;
    QString m_catalogError;
    QHash<QString, QPointer<MapExtract>> m_estimates;  // "region@zoom" being measured
    QHash<QString, std::shared_ptr<PMTiles::Archive>> m_archives;
    int m_revision = 0;
};
