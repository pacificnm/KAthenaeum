// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QPointF>
#include <QString>
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

// A place on the map that Wikipedia may have an article about: from OpenStreetMap, which tags many features with the
// article that describes them ("wikipedia=en:Portland, Oregon").
struct MapPlace {
    QString osm;  // "n123", "w123" or "r123": what the map tiles' feature ids say
    QString name;
    QString nameEn;
    QString kind;  // the tag that says what it is: "place=city", "tourism=museum"
    double lat = 0, lon = 0;  // a point on it, to show it
    double west = 0, south = 0, east = 0, north = 0;  // its extent
    bool area = false;  // a closed shape (a park, a lake, a town's boundary), not a point or a line
    QByteArray outline;  // an area's edges, simplified (see encodeOutline): what "is the point in it" is tested against
    QString wikipedia;  // "en:Portland, Oregon" (sometimes just the title)
    QString wikipediaEn;  // wikipedia:en, when the main tag names another language's article
    QString wikidata;  // "Q6106"
    qint64 population = 0;
};

namespace MapPlaces
{
// Reads an OpenStreetMap extract (.osm.pbf): every named feature with a Wikipedia or Wikidata tag, and every named
// settlement. Three passes (relations, then the ways they and the tagged ways need, then the nodes for their
// positions); Oregon's 255 MB took about 2 s. Nothing on failure or when stopped, with why in `error`.
std::optional<std::vector<MapPlace>> read(const QString &pbfPath, const std::atomic<bool> &stop, QString *error);

// What a feature is, from its tags ("place=city"); empty if it is none of the kinds kept
QString kindOf(const std::function<QString(const char *key)> &tag);

// The article a tag names, in the language given (ISO 639-1, "en"): "en:Portland, Oregon" -> "Portland, Oregon".
// Empty if the tag names an article in another language.
QString articleTitle(const MapPlace &place, const QString &language);

// An area's outline: its ways as polylines, simplified (about 30 m), in any order and direction, outer and inner alike.
// Whether a point is inside needs only the edges (an even number of crossings to the east is outside), not whole rings.
QByteArray encodeOutline(const QList<QList<QPointF>> &lines);  // points are (lon, lat)
bool outlineContains(const QByteArray &outline, double lat, double lon);
QList<QPointF> simplify(const QList<QPointF> &line, double tolerance);  // Douglas-Peucker; keeps both ends

// ---- stored with their region (main thread only) ----
bool store(qint64 regionId, const std::vector<MapPlace> &places, QString *error = nullptr);
void removeRegion(qint64 regionId);
int count(qint64 regionId);
std::optional<MapPlace> byOsm(const QString &osm);
// Areas around the point, smallest first (a building, its park, its town, its county)
QList<MapPlace> around(double lat, double lon, int limit = 6);
// The place OpenStreetMap links to this article (its wikipedia tag, "en:Portland, Oregon"), the biggest if several do
std::optional<MapPlace> byArticle(const QString &language, const QString &title);
// Places whose name starts with (then contains) the text, the most important first
QList<MapPlace> search(const QString &text, int limit = 20);
}
