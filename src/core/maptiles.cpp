// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "maptiles.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double MaxLatitude = 85.0511287798; // where Web Mercator ends

int tileX(double lon, int z)
{
    const int n = 1 << z;
    return std::clamp(int(std::floor((lon + 180.0) / 360.0 * n)), 0, n - 1);
}

int tileY(double lat, int z)
{
    const int n = 1 << z;
    const double r = std::clamp(lat, -MaxLatitude, MaxLatitude) * M_PI / 180.0;
    return std::clamp(int(std::floor((1.0 - std::asinh(std::tan(r)) / M_PI) / 2.0 * n)), 0, n - 1);
}

// the box as one or two spans of longitude (two when it crosses the 180th meridian)
QList<std::pair<double, double>> spans(double west, double east)
{
    if (west <= east)
        return {{west, east}};
    return {{west, 180.0}, {-180.0, east}};
}
}

QList<int> MapTiles::levels(int maxLevel)
{
    QList<int> out;
    for (int z = 1; z <= maxLevel; z += 2)
        out.append(z);
    return out;
}

QList<MapTiles::Tile> MapTiles::tilesFor(double west, double south, double east, double north, int maxLevel)
{
    QList<Tile> out;
    for (const auto zs = levels(maxLevel); const int z : zs) {
        const int y0 = tileY(std::max(south, north), z), y1 = tileY(std::min(south, north), z);
        for (const auto parts = spans(west, east); const auto &[w, e] : parts)
            for (int x = tileX(w, z); x <= tileX(e, z); ++x)
                for (int y = y0; y <= y1; ++y)
                    out.append({z, x, y});
    }
    return out;
}

qint64 MapTiles::countFor(double west, double south, double east, double north, int maxLevel)
{
    qint64 count = 0;
    for (const auto zs = levels(maxLevel); const int z : zs) {
        const qint64 rows = tileY(std::min(south, north), z) - tileY(std::max(south, north), z) + 1;
        for (const auto parts = spans(west, east); const auto &[w, e] : parts)
            count += qint64(tileX(e, z) - tileX(w, z) + 1) * rows;
    }
    return count;
}

QUrl MapTiles::url(const Tile &tile)
{
    return QUrl(QStringLiteral("https://maps.kde.org/earth/vectorosm/v1/") + relativePath(tile));
}

QString MapTiles::relativePath(const Tile &tile)
{
    return QStringLiteral("%1/%2/%3.o5m").arg(tile.z).arg(tile.x).arg(tile.y);
}

QString MapTiles::cacheFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("marble/maps/earth/vectorosm"));
}

bool MapTiles::isAllowedUrl(const QUrl &url)
{
    return url.scheme() == QLatin1String("https") && url.host() == QLatin1String("maps.kde.org");
}

std::optional<MapTiles::Article> MapTiles::wikipediaArticle(const QString &link)
{
    const QString text = link.trimmed();
    // "https://en.wikipedia.org/wiki/Title" (Marble), with the title percent-encoded
    static const QRegularExpression web(QStringLiteral("^https?://([a-z][a-z-]*)\\.(?:m\\.)?wikipedia\\.org/wiki/(.+)$"));
    // "en:Title" (OpenStreetMap's tag): a language code ("en", "zh-yue", "simple"), never a URL's scheme and "//"
    static const QRegularExpression tag(QStringLiteral("^([a-z]{2,3}(?:-[a-z]{2,8})*|simple):([^/].*)$"));
    QString language, title;
    if (const auto m = web.match(text); m.hasMatch()) {
        language = m.captured(1);
        title = QUrl::fromPercentEncoding(m.captured(2).toUtf8());
    } else if (const auto t = tag.match(text); t.hasMatch()) {
        language = t.captured(1);
        title = t.captured(2);
    } else {
        return std::nullopt;
    }
    title = title.section(QLatin1Char('#'), 0, 0).replace(QLatin1Char('_'), QLatin1Char(' ')).trimmed();
    if (title.isEmpty())
        return std::nullopt;
    return Article{language, title};
}
