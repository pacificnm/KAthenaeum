// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "maptiling.h"

#include "pmtiles.h"

#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <map>

namespace MapTiling
{
namespace
{
constexpr double MaxLat = 85.0511287798066;

// Per tile row, the columns the shape touches, as [first, last] intervals
using Rows = std::map<qint64, std::vector<std::pair<qint64, qint64>>>;

void mark(Rows &rows, qint64 y, qint64 x0, qint64 x1, qint64 n)
{
    if (y < 0 || y >= n)
        return;
    x0 = std::clamp<qint64>(x0, 0, n - 1);
    x1 = std::clamp<qint64>(x1, 0, n - 1);
    if (x0 > x1)
        std::swap(x0, x1);
    rows[y].emplace_back(x0, x1);
}

// Every tile an edge passes through (it crosses the tile grid; Amanatides and Woo)
void markEdge(Rows &rows, QPointF a, QPointF b, qint64 n)
{
    qint64 x = qint64(std::floor(a.x())), y = qint64(std::floor(a.y()));
    const qint64 xEnd = qint64(std::floor(b.x())), yEnd = qint64(std::floor(b.y()));
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const int stepX = dx > 0 ? 1 : -1, stepY = dy > 0 ? 1 : -1;
    const double tDeltaX = dx != 0 ? std::abs(1.0 / dx) : INFINITY;
    const double tDeltaY = dy != 0 ? std::abs(1.0 / dy) : INFINITY;
    double tMaxX = dx != 0 ? ((dx > 0 ? std::floor(a.x()) + 1 - a.x() : a.x() - std::floor(a.x())) * tDeltaX) : INFINITY;
    double tMaxY = dy != 0 ? ((dy > 0 ? std::floor(a.y()) + 1 - a.y() : a.y() - std::floor(a.y())) * tDeltaY) : INFINITY;
    mark(rows, y, x, x, n);
    for (qint64 guard = std::abs(xEnd - x) + std::abs(yEnd - y) + 2; (x != xEnd || y != yEnd) && guard > 0; --guard) {
        if (tMaxX < tMaxY) {
            tMaxX += tDeltaX;
            x += stepX;
        } else {
            tMaxY += tDeltaY;
            y += stepY;
        }
        mark(rows, y, x, x, n);
    }
}

Rows rowsAt(const MultiPolygon &shape, int z)
{
    const qint64 n = qint64(1) << z;
    Rows rows;
    for (const Polygon &polygon : shape) {
        // the polygon in tile units
        QList<QList<QPointF>> rings;
        for (const Ring &ring : polygon) {
            QList<QPointF> r;
            r.reserve(ring.size());
            for (const QPointF &p : ring)
                r.append({lonToX(p.x(), z), latToY(p.y(), z)});
            rings.append(r);
        }
        // its outline
        for (const auto &r : rings)
            for (qsizetype i = 0; i + 1 < r.size(); ++i)
                markEdge(rows, r[i], r[i + 1], n);
        // its inside: each row's middle line, filled between crossings (even-odd, so holes stay empty)
        double top = INFINITY, bottom = -INFINITY;
        for (const auto &r : rings)
            for (const QPointF &p : r) {
                top = std::min(top, p.y());
                bottom = std::max(bottom, p.y());
            }
        if (rings.isEmpty() || top > bottom)
            continue;
        for (qint64 y = std::max<qint64>(0, qint64(std::floor(top))); y <= std::min<qint64>(n - 1, qint64(std::floor(bottom))); ++y) {
            const double line = y + 0.5;
            std::vector<double> crossings;
            for (const auto &r : rings)
                for (qsizetype i = 0; i + 1 < r.size(); ++i) {
                    const QPointF &a = r[i], &b = r[i + 1];
                    if ((a.y() <= line) != (b.y() <= line))
                        crossings.push_back(a.x() + (line - a.y()) * (b.x() - a.x()) / (b.y() - a.y()));
                }
            std::sort(crossings.begin(), crossings.end());
            for (size_t i = 0; i + 1 < crossings.size(); i += 2)
                mark(rows, y, qint64(std::floor(crossings[i])), qint64(std::floor(crossings[i + 1])), n);
        }
    }
    // merge each row's intervals
    for (auto &[y, spans] : rows) {
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<qint64, qint64>> merged;
        for (const auto &s : spans) {
            if (!merged.empty() && s.first <= merged.back().second + 1)
                merged.back().second = std::max(merged.back().second, s.second);
            else
                merged.push_back(s);
        }
        spans = std::move(merged);
    }
    return rows;
}
}

double lonToX(double lon, int z)
{
    return (lon + 180.0) / 360.0 * double(qint64(1) << z);
}

double latToY(double lat, int z)
{
    lat = std::clamp(lat, -MaxLat, MaxLat) * M_PI / 180.0;
    return (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / M_PI) / 2.0 * double(qint64(1) << z);
}

double xToLon(double x, int z)
{
    return x / double(qint64(1) << z) * 360.0 - 180.0;
}

double yToLat(double y, int z)
{
    const double n = M_PI - 2.0 * M_PI * y / double(qint64(1) << z);
    return 180.0 / M_PI * std::atan(std::sinh(n));
}

bool contains(const MultiPolygon &shape, double lon, double lat)
{
    // even-odd: an odd number of edges crossed going east is inside; a hole's edges count too, which leaves it out
    bool inside = false;
    for (const Polygon &polygon : shape)
        for (const Ring &ring : polygon)
            for (qsizetype i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
                const QPointF &a = ring.at(i), &b = ring.at(j);
                if ((a.y() > lat) != (b.y() > lat) && lon < (b.x() - a.x()) * (lat - a.y()) / (b.y() - a.y()) + a.x())
                    inside = !inside;
            }
    return inside;
}

QRectF bounds(const MultiPolygon &shape)
{
    double w = INFINITY, s = INFINITY, e = -INFINITY, n = -INFINITY;
    for (const Polygon &p : shape)
        for (const Ring &r : p)
            for (const QPointF &pt : r) {
                w = std::min(w, pt.x());
                e = std::max(e, pt.x());
                s = std::min(s, pt.y());
                n = std::max(n, pt.y());
            }
    if (w > e)
        return {};
    return QRectF(w, s, e - w, n - s);
}

std::vector<quint64> tileIds(const MultiPolygon &shape, int minZoom, int maxZoom)
{
    std::vector<quint64> ids;
    for (int z = minZoom; z <= maxZoom; ++z)
        for (const auto &[y, spans] : rowsAt(shape, z))
            for (const auto &[x0, x1] : spans)
                for (qint64 x = x0; x <= x1; ++x)
                    ids.push_back(PMTiles::tileId(z, quint32(x), quint32(y)));
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

quint64 tileCount(const MultiPolygon &shape, int minZoom, int maxZoom)
{
    quint64 count = 0;
    for (int z = minZoom; z <= maxZoom; ++z)
        for (const auto &[y, spans] : rowsAt(shape, z))
            for (const auto &[x0, x1] : spans)
                count += quint64(x1 - x0 + 1);
    return count;
}

MultiPolygon fromGeoJson(const QVariantMap &geometry)
{
    auto ring = [](const QVariant &v) {
        Ring r;
        for (const auto values = v.toList(); const QVariant &p : values) {
            const QVariantList xy = p.toList();
            if (xy.size() >= 2)
                r.append({xy[0].toDouble(), xy[1].toDouble()});
        }
        if (!r.isEmpty() && r.first() != r.last())
            r.append(r.first());
        return r;
    };
    auto polygon = [&ring](const QVariant &v) {
        Polygon p;
        for (const auto values = v.toList(); const QVariant &r : values)
            p.append(ring(r));
        return p;
    };
    MultiPolygon shape;
    const QString type = geometry.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("Polygon"))
        shape.append(polygon(geometry.value(QStringLiteral("coordinates"))));
    else if (type == QLatin1String("MultiPolygon"))
        for (const auto values = geometry.value(QStringLiteral("coordinates")).toList(); const QVariant &p : values)
            shape.append(polygon(p));
    return shape;
}
}
