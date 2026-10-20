// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QtGlobal>
#include <vector>

// A region's shape and the map tiles it covers. Points are (longitude, latitude) in degrees.
namespace MapTiling
{
using Ring = QList<QPointF>;
using Polygon = QList<Ring>;          // the first ring is the outside, the others holes
using MultiPolygon = QList<Polygon>;

// Web Mercator, in tiles at zoom z (x to the east, y to the south)
double lonToX(double lon, int z);
double latToY(double lat, int z);
double xToLon(double x, int z);
double yToLat(double y, int z);

QRectF bounds(const MultiPolygon &shape);  // x = west, y = south, width and height in degrees
// Is the point inside the shape (holes left out)? Not its bounding box: a region across the 180° meridian (the US
// Pacific islands) has a box round the whole world.
bool contains(const MultiPolygon &shape, double lon, double lat);

// The PMTiles ids of every tile from minZoom to maxZoom that the shape touches, sorted
std::vector<quint64> tileIds(const MultiPolygon &shape, int minZoom, int maxZoom);
// How many tiles that is, without listing them (to say how big a region is before fetching its directories)
quint64 tileCount(const MultiPolygon &shape, int minZoom, int maxZoom);

// The shape from GeoJSON geometry (Polygon or MultiPolygon); empty if it is neither
MultiPolygon fromGeoJson(const QVariantMap &geometry);
}
