// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <optional>

// Mapbox vector tiles (https://github.com/mapbox/vector-tile-spec), decoded just enough to find what was clicked: each
// feature's layer, properties and geometry in tile units (0..extent).
namespace Mvt
{
enum GeomType {
    Unknown = 0,
    Point = 1,
    LineString = 2,
    Polygon = 3
};

struct Feature {
    QString layer;
    quint64 id = 0;
    GeomType type = Unknown;
    QVariantMap properties;
    QList<QList<QPointF>> parts; // points: one part of points; lines and polygons: one part per line or ring
    int extent = 4096;
};

// Nothing if the bytes are not a vector tile. `layers` limits which layers are decoded (all if empty).
std::optional<QList<Feature>> decode(const QByteArray &tile, const QStringList &layers = {});

// Whether a polygon feature contains the point (even-odd over all its rings)
bool contains(const Feature &polygon, QPointF p);
// The distance from the point to the nearest of the feature's points or lines, in tile units
double distance(const Feature &feature, QPointF p);
}
