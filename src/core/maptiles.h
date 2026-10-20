// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

// The map is drawn by Marble (KDE's map library) from its "Vector OSM" tiles: OpenStreetMap data in o5m files, served
// by maps.kde.org and cached by Marble under <GenericDataLocation>/marble. These are the pure parts the app needs around
// that: which tiles cover an area (to keep it for offline use), where they come from and go, and which Wikipedia article
// a place on the map links to.
namespace MapTiles
{
// Marble's vector tiles exist at every other level; the levels up to 15 show streets, shops and stops (17, the
// building close-up, needs 16 times as many tiles and is left to the online map)
QList<int> levels(int maxLevel = 15);

// Keeping an area offline is limited to this many tiles (a city at street level is a few hundred to a few thousand):
// the tiles come from KDE's servers one at a time
constexpr qsizetype MaxSaveTiles = 4000;

struct Tile {
    int z = 0;
    int x = 0;
    int y = 0;
    bool operator==(const Tile &) const = default;
};

// The tiles covering a box (degrees; west may be greater than east across the 180th meridian), level by level
QList<Tile> tilesFor(double west, double south, double east, double north, int maxLevel = 15);
// How many that is, without listing them
qint64 countFor(double west, double south, double east, double north, int maxLevel = 15);

QUrl url(const Tile &tile);
QString relativePath(const Tile &tile); // "13/1304/2930.o5m", under the cache folder
// Marble's cache of the Vector OSM theme, shared with the Marble application
QString cacheFolder();
// Downloads go only to KDE's map server, over https
bool isAllowedUrl(const QUrl &url);

// The article a map feature links to: Marble gives "https://en.wikipedia.org/wiki/Pearl%20District%2C%20Portland",
// OpenStreetMap's own tag is "en:Pearl District, Portland"
struct Article {
    QString language; // "en"
    QString title; // "Pearl District, Portland"
};
std::optional<Article> wikipediaArticle(const QString &link);
}
