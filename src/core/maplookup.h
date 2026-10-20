// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <optional>

namespace PMTiles
{
class Archive;
}

// What was clicked on the map: the labelled place nearest the click, read from the map's own tiles
struct MapFeature {
    QString name;
    QString nameEn;
    QString kind;  // Protomaps' kind: "locality", "museum", "park"
    QString osm;  // "n123" | "w123" | "r123"
    QString wikidata;
    double lat = 0, lon = 0;
    double distancePx = 0;  // from the click
};

namespace MapLookup
{
// MapLibre draws 512-pixel tiles: the coordinate under a point `dx`, `dy` pixels from the centre of a map showing
// (lat, lon) at `zoom`. Returns (lat, lon).
QPointF coordinateAt(double lat, double lon, double zoom, double dx, double dy);

// Protomaps' feature ids are OpenStreetMap ids with the type above bit 44 (1 node, 2 way, 3 relation); "" if not
QString osmKey(quint64 featureId);

// The labelled point (a place or a point of interest) nearest the coordinate, within `radiusPx` pixels at `zoom`, among
// those the map shows at that zoom. Reads the tile under the click and, near its edge, its neighbours.
std::optional<MapFeature> nearest(PMTiles::Archive &archive, double lat, double lon, double zoom, double radiusPx);
}
