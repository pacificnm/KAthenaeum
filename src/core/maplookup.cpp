// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "maplookup.h"

#include "maptiling.h"
#include "mvt.h"
#include "pmtiles.h"

#include <cmath>

namespace MapLookup
{
QPointF coordinateAt(double lat, double lon, double zoom, double dx, double dy)
{
    // in units of 512-pixel tiles at zoom 0, scaled
    const double scale = std::pow(2.0, zoom) * 512.0;
    const double x = MapTiling::lonToX(lon, 0) + dx / scale;
    const double y = MapTiling::latToY(lat, 0) + dy / scale;
    return {MapTiling::yToLat(y, 0), MapTiling::xToLon(x, 0)};
}

QString osmKey(quint64 featureId)
{
    const quint64 type = featureId >> 44;
    const quint64 id = featureId & ((quint64(1) << 44) - 1);
    if (id == 0)
        return {};
    switch (type) {
    case 1:
        return QLatin1Char('n') + QString::number(id);
    case 2:
        return QLatin1Char('w') + QString::number(id);
    case 3:
        return QLatin1Char('r') + QString::number(id);
    default:
        return {};
    }
}

std::optional<MapFeature> nearest(PMTiles::Archive &archive, double lat, double lon, double zoom, double radiusPx)
{
    if (!archive.isValid())
        return std::nullopt;
    const PMTiles::Header &h = archive.header();
    const int z = std::clamp(int(std::floor(zoom)), int(h.minZoom), int(h.maxZoom));
    // at `zoom`, a tile of zoom z is 512 * 2^(zoom - z) pixels wide
    const double tilePx = 512.0 * std::pow(2.0, zoom - z);
    const double x = MapTiling::lonToX(lon, z), y = MapTiling::latToY(lat, z);
    const double r = radiusPx / tilePx;  // in tiles
    const qint64 n = qint64(1) << z;

    std::optional<MapFeature> best;
    int bestRank = 0;
    for (qint64 ty = qint64(std::floor(y - r)); ty <= qint64(std::floor(y + r)); ++ty) {
        if (ty < 0 || ty >= n)
            continue;
        for (qint64 tx = qint64(std::floor(x - r)); tx <= qint64(std::floor(x + r)); ++tx) {
            const qint64 wrapped = ((tx % n) + n) % n;
            const auto features = Mvt::decode(archive.tile(z, quint32(wrapped), quint32(ty)), {QStringLiteral("places"), QStringLiteral("pois")});
            if (!features)
                continue;
            for (const Mvt::Feature &f : *features) {
                if (f.type != Mvt::Point || f.parts.isEmpty() || f.parts.first().isEmpty())
                    continue;
                const QString name = f.properties.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    continue;
                // only what the map shows at this zoom
                const QVariant minZoom = f.properties.value(QStringLiteral("min_zoom"));
                if (minZoom.isValid() && minZoom.toDouble() > zoom + 0.5)
                    continue;
                const QPointF p = f.parts.first().first();
                const double fx = double(tx) + p.x() / f.extent, fy = double(ty) + p.y() / f.extent;
                const double d = std::hypot(fx - x, fy - y) * tilePx;
                if (d > radiusPx)
                    continue;
                // a place's name is drawn larger than a point of interest's: it wins a near tie
                const int rank = f.layer == QLatin1String("places") ? 1 : 0;
                if (best && (d - (rank - bestRank) * 6.0) >= best->distancePx)
                    continue;
                MapFeature m;
                m.name = name;
                m.nameEn = f.properties.value(QStringLiteral("name:en")).toString();
                m.kind = f.properties.value(QStringLiteral("kind_detail")).toString();
                if (m.kind.isEmpty() || f.layer == QLatin1String("pois"))
                    m.kind = f.properties.value(QStringLiteral("kind")).toString();
                m.osm = osmKey(f.id);
                m.wikidata = f.properties.value(QStringLiteral("wikidata")).toString();
                m.lat = MapTiling::yToLat(fy, z);
                m.lon = MapTiling::xToLon(fx, z);
                m.distancePx = d;
                best = m;
                bestRank = rank;
            }
        }
    }
    return best;
}
}
