// SPDX-License-Identifier: GPL-3.0-or-later
#include "mapplaces.h"

#include "database.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtEndian>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include <osmium/handler.hpp>
#include <osmium/io/pbf_input.hpp>
#include <osmium/visitor.hpp>

namespace MapPlaces
{
namespace
{
// The keys that say what a feature is, the most telling first
const char *const KindKeys[] = {"place", "tourism", "historic", "amenity", "leisure", "natural", "aeroway", "railway", "man_made",
                                "building", "shop", "office", "craft", "military", "boundary", "landuse", "waterway", "highway", "route"};

template<typename T>
QString tag(const T &object, const char *key)
{
    const char *v = object.tags().get_value_by_key(key);
    return v ? QString::fromUtf8(v) : QString();
}

template<typename T>
bool interesting(const T &object)
{
    const auto &t = object.tags();
    if (!t.has_key("name"))
        return false;
    return t.has_key("wikipedia") || t.has_key("wikidata") || t.has_key("place");
}

template<typename T>
MapPlace describe(const T &object, QChar type)
{
    MapPlace p;
    p.osm = type + QString::number(object.id());
    p.name = tag(object, "name");
    p.nameEn = tag(object, "name:en");
    p.kind = kindOf([&object](const char *key) {
        return tag(object, key);
    });
    p.wikipedia = tag(object, "wikipedia");
    p.wikipediaEn = tag(object, "wikipedia:en");
    p.wikidata = tag(object, "wikidata");
    p.population = tag(object, "population").remove(QLatin1Char(',')).toLongLong();
    return p;
}

struct Box {
    double west = 1e9, south = 1e9, east = -1e9, north = -1e9;
    double sumLat = 0, sumLon = 0;
    int n = 0;
    void add(double lat, double lon)
    {
        west = std::min(west, lon);
        east = std::max(east, lon);
        south = std::min(south, lat);
        north = std::max(north, lat);
        sumLat += lat;
        sumLon += lon;
        ++n;
    }
    void add(const Box &b)
    {
        if (b.n == 0)
            return;
        west = std::min(west, b.west);
        east = std::max(east, b.east);
        south = std::min(south, b.south);
        north = std::max(north, b.north);
        sumLat += b.sumLat;
        sumLon += b.sumLon;
        n += b.n;
    }
};

void place(MapPlace &p, const Box &b)
{
    p.west = b.west;
    p.east = b.east;
    p.south = b.south;
    p.north = b.north;
    p.lat = b.sumLat / b.n;  // the average of its points: inside a compact shape, near a long one
    p.lon = b.sumLon / b.n;
}

QSqlDatabase db()
{
    return QSqlDatabase::database();
}

MapPlace fromRow(const QSqlQuery &q)
{
    MapPlace p;
    p.osm = q.value(0).toString();
    p.name = q.value(1).toString();
    p.nameEn = q.value(2).toString();
    p.kind = q.value(3).toString();
    p.lat = q.value(4).toDouble();
    p.lon = q.value(5).toDouble();
    p.west = q.value(6).toDouble();
    p.south = q.value(7).toDouble();
    p.east = q.value(8).toDouble();
    p.north = q.value(9).toDouble();
    p.area = q.value(10).toBool();
    p.wikipedia = q.value(11).toString();
    p.wikipediaEn = q.value(12).toString();
    p.wikidata = q.value(13).toString();
    p.population = q.value(14).toLongLong();
    p.outline = q.value(15).toByteArray();
    return p;
}

const QString Columns = QStringLiteral(
    "p.osm, p.name, p.name_en, p.kind, p.lat, p.lon, p.west, p.south, p.east, p.north, p.area, p.wikipedia, p.wikipedia_en, p.wikidata, p.population, "
    "p.outline");
}

QList<QPointF> simplify(const QList<QPointF> &line, double tolerance)
{
    if (line.size() < 3)
        return line;
    std::vector<bool> keep(size_t(line.size()), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<qsizetype, qsizetype>> stack{{0, line.size() - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        const QPointF pa = line[a], pb = line[b];
        const double dx = pb.x() - pa.x(), dy = pb.y() - pa.y();
        const double len = std::hypot(dx, dy);
        double worst = -1;
        qsizetype at = -1;
        for (qsizetype i = a + 1; i < b; ++i) {
            const QPointF p = line[i];
            const double d = len > 0 ? std::abs(dy * (p.x() - pa.x()) - dx * (p.y() - pa.y())) / len : std::hypot(p.x() - pa.x(), p.y() - pa.y());
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (at >= 0 && worst > tolerance) {
            keep[size_t(at)] = true;
            stack.emplace_back(a, at);
            stack.emplace_back(at, b);
        }
    }
    QList<QPointF> out;
    for (qsizetype i = 0; i < line.size(); ++i)
        if (keep[size_t(i)])
            out.append(line[i]);
    return out;
}

QByteArray encodeOutline(const QList<QList<QPointF>> &lines)
{
    QByteArray out;
    auto put = [&out](auto v) {
        char b[sizeof v];
        qToLittleEndian(v, b);
        out.append(b, sizeof v);
    };
    for (const QList<QPointF> &l : lines) {
        if (l.size() < 2)
            continue;
        put(quint32(l.size()));
        for (const QPointF &p : l) {
            put(float(p.x()));
            put(float(p.y()));
        }
    }
    return out;
}

bool outlineContains(const QByteArray &outline, double lat, double lon)
{
    // crossings of the edges with the line going east from the point
    bool inside = false;
    qsizetype at = 0;
    const char *d = outline.constData();
    while (at + 4 <= outline.size()) {
        const quint32 n = qFromLittleEndian<quint32>(d + at);
        at += 4;
        if (n > quint32((outline.size() - at) / 8))
            return false;  // damaged
        float px = 0, py = 0;
        for (quint32 i = 0; i < n; ++i, at += 8) {
            const float x = qFromLittleEndian<float>(d + at), y = qFromLittleEndian<float>(d + at + 4);
            if (i > 0 && ((py > lat) != (y > lat)) && lon < (x - px) * (lat - py) / (y - py) + px)
                inside = !inside;
            px = x;
            py = y;
        }
    }
    return inside;
}

QString kindOf(const std::function<QString(const char *key)> &tag)
{
    for (const char *key : KindKeys) {
        const QString v = tag(key);
        if (v.isEmpty() || v == QLatin1String("no"))
            continue;
        // an administrative boundary says what it bounds: "county" (border_type), or its level (2 a country, 4 a state)
        if (qstrcmp(key, "boundary") == 0 && v == QLatin1String("administrative")) {
            const QString border = tag("border_type");
            if (!border.isEmpty())
                return QStringLiteral("boundary=") + border;
            const QString level = tag("admin_level");
            if (level == QLatin1String("2"))
                return QStringLiteral("boundary=country");
            if (level == QLatin1String("4"))
                return QStringLiteral("boundary=state");
        }
        return QString::fromLatin1(key) + QLatin1Char('=') + v;
    }
    return {};
}

QString articleTitle(const MapPlace &place, const QString &language)
{
    if (language == QLatin1String("en") && !place.wikipediaEn.isEmpty())
        return place.wikipediaEn;
    const QString w = place.wikipedia.trimmed();
    const qsizetype colon = w.indexOf(QLatin1Char(':'));
    // "en:Title"; a language code is 2 or 3 letters (or "simple", "zh-yue")
    if (colon > 1 && colon <= 12 && !w.left(colon).contains(QLatin1Char(' ')))
        return w.left(colon) == language ? w.mid(colon + 1).trimmed() : QString();
    return {};
}

std::optional<std::vector<MapPlace>> read(const QString &pbfPath, const std::atomic<bool> &stop, QString *error)
{
    try {
        const std::string file = pbfPath.toStdString();
        std::vector<MapPlace> out;

        // 1. relations: the interesting ones, and the ways and label nodes they are made of
        struct Rel {
            MapPlace place;
            std::vector<osmium::object_id_type> ways;  // outer: its extent
            std::vector<osmium::object_id_type> inner;  // holes: only its outline
            osmium::object_id_type label = 0;
        };
        std::vector<Rel> relations;
        std::unordered_set<osmium::object_id_type> neededWays;
        {
            osmium::io::Reader reader{file, osmium::osm_entity_bits::relation};
            while (osmium::memory::Buffer buffer = reader.read()) {
                if (stop)
                    return std::nullopt;
                for (const auto &r : buffer.select<osmium::Relation>()) {
                    if (!interesting(r))
                        continue;
                    Rel rel{describe(r, QLatin1Char('r')), {}, {}, 0};
                    const QString type = tag(r, "type");
                    rel.place.area = type == QLatin1String("multipolygon") || type == QLatin1String("boundary");
                    for (const auto &m : r.members()) {
                        if (m.type() == osmium::item_type::way && (std::string_view(m.role()).empty() || std::string_view(m.role()) == "outer")) {
                            rel.ways.push_back(m.ref());
                            neededWays.insert(m.ref());
                        } else if (m.type() == osmium::item_type::way && std::string_view(m.role()) == "inner") {
                            rel.inner.push_back(m.ref());
                            neededWays.insert(m.ref());
                        } else if (m.type() == osmium::item_type::node
                                   && (std::string_view(m.role()) == "label" || std::string_view(m.role()) == "admin_centre") && !rel.label) {
                            rel.label = m.ref();
                        }
                    }
                    relations.push_back(std::move(rel));
                }
            }
            reader.close();
        }

        // 2. ways: the interesting ones, and those the relations need, with their nodes
        struct WayNodes {
            std::vector<osmium::object_id_type> nodes;
        };
        std::unordered_map<osmium::object_id_type, WayNodes> wayNodes;
        std::vector<std::pair<MapPlace, osmium::object_id_type>> ways;
        std::vector<osmium::object_id_type> neededNodes;
        {
            osmium::io::Reader reader{file, osmium::osm_entity_bits::way};
            while (osmium::memory::Buffer buffer = reader.read()) {
                if (stop)
                    return std::nullopt;
                for (const auto &w : buffer.select<osmium::Way>()) {
                    const bool own = interesting(w);
                    if (!own && !neededWays.count(w.id()))
                        continue;
                    WayNodes &wn = wayNodes[w.id()];
                    for (const auto &n : w.nodes()) {
                        wn.nodes.push_back(n.ref());
                        neededNodes.push_back(n.ref());
                    }
                    if (own) {
                        MapPlace p = describe(w, QLatin1Char('w'));
                        const bool closed = w.nodes().size() > 3 && w.nodes().front().ref() == w.nodes().back().ref();
                        const bool linear = w.tags().has_key("highway") || w.tags().has_key("railway") || w.tags().has_key("barrier")
                            || (w.tags().has_key("waterway") && !w.tags().has_tag("waterway", "riverbank"));
                        p.area = closed && (!linear || w.tags().has_tag("area", "yes"));
                        ways.emplace_back(std::move(p), w.id());
                    }
                }
            }
            reader.close();
        }
        for (const Rel &r : relations)
            if (r.label)
                neededNodes.push_back(r.label);
        std::sort(neededNodes.begin(), neededNodes.end());
        neededNodes.erase(std::unique(neededNodes.begin(), neededNodes.end()), neededNodes.end());

        // 3. nodes: the interesting ones, and the positions the ways need (both in id order, so a merge)
        std::vector<std::pair<double, double>> positions(neededNodes.size(), {NAN, NAN});
        {
            osmium::io::Reader reader{file, osmium::osm_entity_bits::node};
            size_t at = 0;
            while (osmium::memory::Buffer buffer = reader.read()) {
                if (stop)
                    return std::nullopt;
                for (const auto &n : buffer.select<osmium::Node>()) {
                    if (!n.location().valid())
                        continue;
                    const double lat = n.location().lat(), lon = n.location().lon();
                    while (at < neededNodes.size() && neededNodes[at] < n.id())
                        ++at;
                    if (at < neededNodes.size() && neededNodes[at] == n.id())
                        positions[at] = {lat, lon};
                    if (interesting(n)) {
                        MapPlace p = describe(n, QLatin1Char('n'));
                        p.lat = p.south = p.north = lat;
                        p.lon = p.west = p.east = lon;
                        out.push_back(std::move(p));
                    }
                }
            }
            reader.close();
        }
        auto position = [&](osmium::object_id_type id) -> const std::pair<double, double> * {
            const auto it = std::lower_bound(neededNodes.begin(), neededNodes.end(), id);
            if (it == neededNodes.end() || *it != id)
                return nullptr;
            const auto &p = positions[size_t(it - neededNodes.begin())];
            return std::isnan(p.first) ? nullptr : &p;
        };
        auto wayBox = [&](osmium::object_id_type id) {
            Box b;
            const auto it = wayNodes.find(id);
            if (it != wayNodes.end())
                for (osmium::object_id_type n : it->second.nodes)
                    if (const auto *p = position(n))
                        b.add(p->first, p->second);
            return b;
        };
        auto wayLine = [&](osmium::object_id_type id) {
            QList<QPointF> line;
            const auto it = wayNodes.find(id);
            if (it != wayNodes.end())
                for (osmium::object_id_type n : it->second.nodes)
                    if (const auto *p = position(n))
                        line.append({p->second, p->first});
            return line;
        };
        // an area's outline, simplified until it is at most a few thousand points
        auto outline = [&](const std::vector<osmium::object_id_type> &ids) {
            QList<QList<QPointF>> lines;
            for (osmium::object_id_type id : ids)
                lines.append(wayLine(id));
            for (double tolerance = 0.0003;; tolerance *= 2) {
                QList<QList<QPointF>> simple;
                qsizetype points = 0;
                for (const auto &l : std::as_const(lines)) {
                    simple.append(simplify(l, tolerance));
                    points += simple.last().size();
                }
                if (points <= 4000 || tolerance > 0.1)
                    return encodeOutline(simple);
            }
        };
        for (auto &[p, id] : ways) {
            const Box b = wayBox(id);
            if (b.n == 0)
                continue;
            place(p, b);
            if (p.area)
                p.outline = outline({id});
            out.push_back(std::move(p));
        }
        for (Rel &r : relations) {
            Box b;
            for (osmium::object_id_type w : r.ways)
                b.add(wayBox(w));
            if (b.n == 0)
                continue;
            place(r.place, b);
            if (r.place.area) {
                std::vector<osmium::object_id_type> all = r.ways;
                all.insert(all.end(), r.inner.begin(), r.inner.end());
                r.place.outline = outline(all);
            }
            if (const auto *l = r.label ? position(r.label) : nullptr) {
                r.place.lat = l->first;  // a town's centre, not the middle of its boundary
                r.place.lon = l->second;
            }
            out.push_back(std::move(r.place));
        }
        return out;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return std::nullopt;
    }
}

bool store(qint64 regionId, const std::vector<MapPlace> &places, QString *error)
{
    QSqlDatabase d = db();
    auto failed = [&](const QSqlQuery &q) {
        if (error)
            *error = q.lastError().text();
        d.rollback();
        return false;
    };
    if (!d.transaction()) {
        if (error)
            *error = d.lastError().text();
        return false;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("DELETE FROM map_places_box WHERE id IN (SELECT id FROM map_places WHERE region_id = ?)"));
    q.addBindValue(regionId);
    if (!q.exec())
        return failed(q);
    q.prepare(QStringLiteral("DELETE FROM map_places WHERE region_id = ?"));
    q.addBindValue(regionId);
    if (!q.exec())
        return failed(q);
    QSqlQuery insert(d), box(d);
    insert.prepare(QStringLiteral("INSERT INTO map_places (region_id, osm, name, name_en, kind, lat, lon, west, south, east, north, area, "
                                  "wikipedia, wikipedia_en, wikidata, population, outline) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    box.prepare(QStringLiteral("INSERT INTO map_places_box (id, west, east, south, north) VALUES (?, ?, ?, ?, ?)"));
    for (const MapPlace &p : places) {
        insert.addBindValue(regionId);
        insert.addBindValue(p.osm);
        insert.addBindValue(Database::text(p.name));
        insert.addBindValue(Database::text(p.nameEn));
        insert.addBindValue(Database::text(p.kind));
        insert.addBindValue(p.lat);
        insert.addBindValue(p.lon);
        insert.addBindValue(p.west);
        insert.addBindValue(p.south);
        insert.addBindValue(p.east);
        insert.addBindValue(p.north);
        insert.addBindValue(p.area ? 1 : 0);
        insert.addBindValue(Database::text(p.wikipedia));
        insert.addBindValue(Database::text(p.wikipediaEn));
        insert.addBindValue(Database::text(p.wikidata));
        insert.addBindValue(p.population);
        insert.addBindValue(p.outline.isNull() ? QByteArray("") : p.outline);
        if (!insert.exec())
            return failed(insert);
        box.addBindValue(insert.lastInsertId());
        box.addBindValue(p.west);
        box.addBindValue(p.east);
        box.addBindValue(p.south);
        box.addBindValue(p.north);
        if (!box.exec())
            return failed(box);
    }
    if (!d.commit()) {
        if (error)
            *error = d.lastError().text();
        d.rollback();  // left open, the transaction would take in every later write
        return false;
    }
    return true;
}

void removeRegion(qint64 regionId)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM map_places_box WHERE id IN (SELECT id FROM map_places WHERE region_id = ?)"));
    q.addBindValue(regionId);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM map_places WHERE region_id = ?"));
    q.addBindValue(regionId);
    q.exec();
}

int count(qint64 regionId)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM map_places WHERE region_id = ?"));
    q.addBindValue(regionId);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

std::optional<MapPlace> byOsm(const QString &osm)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM map_places p WHERE p.osm = ? LIMIT 1").arg(Columns));
    q.addBindValue(osm);
    if (q.exec() && q.next())
        return fromRow(q);
    return std::nullopt;
}

QList<MapPlace> around(double lat, double lon, int limit)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM map_places_box b JOIN map_places p ON p.id = b.id "
                             "WHERE b.west <= ? AND b.east >= ? AND b.south <= ? AND b.north >= ? AND p.area = 1 "
                             // lines drawn for other purposes than saying where something is
                             "AND p.kind NOT IN ('boundary=timezone', 'boundary=maritime', 'boundary=hazard') "
                             "ORDER BY (b.east - b.west) * (b.north - b.south) LIMIT ?")
                  .arg(Columns));
    q.addBindValue(lon);
    q.addBindValue(lon);
    q.addBindValue(lat);
    q.addBindValue(lat);
    q.addBindValue(limit * 8);
    QList<MapPlace> out;
    QSet<QString> seen;  // the same article twice (a park's two outlines)
    if (q.exec())
        while (q.next() && out.size() < limit) {
            MapPlace p = fromRow(q);
            // its extent holds the point; is the point in it? (a county's box holds half its neighbours)
            if (!p.outline.isEmpty() && !outlineContains(p.outline, lat, lon))
                continue;
            const QString key = p.wikipedia.isEmpty() ? p.wikidata.isEmpty() ? p.osm : p.wikidata : p.wikipedia;
            if (!seen.contains(key)) {
                seen.insert(key);
                out.append(p);
            }
        }
    return out;
}

std::optional<MapPlace> byArticle(const QString &language, const QString &title)
{
    if (language.isEmpty() || title.trimmed().isEmpty())
        return std::nullopt;
    QSqlQuery q(db());
    // the tag names the article in its own language; wikipedia:en names the English one beside another language's
    q.prepare(QStringLiteral("SELECT %1 FROM map_places p WHERE p.wikipedia = ? OR (? = 'en' AND p.wikipedia_en = ?) "
                             "ORDER BY (p.east - p.west) * (p.north - p.south) DESC, p.population DESC LIMIT 1")
                  .arg(Columns));
    q.addBindValue(QString(language + QLatin1Char(':') + title.trimmed()));
    q.addBindValue(language);
    q.addBindValue(title.trimmed());
    if (q.exec() && q.next())
        return fromRow(q);
    return std::nullopt;
}

QList<MapPlace> search(const QString &text, int limit)
{
    const QString t = text.simplified();
    if (t.isEmpty())
        return {};
    QString like = t;
    like.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('%'), QLatin1String("\\%")).replace(QLatin1Char('_'), QLatin1String("\\_"));
    QSqlQuery q(db());
    // names that start with the text first, then the rest; within each, places with articles and people first
    q.prepare(QStringLiteral("SELECT %1 FROM map_places p WHERE p.name LIKE ? ESCAPE '\\' OR p.name_en LIKE ? ESCAPE '\\' "
                             "ORDER BY (p.name LIKE ? ESCAPE '\\' OR p.name_en LIKE ? ESCAPE '\\') DESC, (p.wikipedia != '') DESC, "
                             "p.population DESC, length(p.name) LIMIT ?")
                  .arg(Columns));
    const QString anywhere = QLatin1Char('%') + like + QLatin1Char('%'), start = like + QLatin1Char('%');
    q.addBindValue(anywhere);
    q.addBindValue(anywhere);
    q.addBindValue(start);
    q.addBindValue(start);
    q.addBindValue(limit);
    QList<MapPlace> out;
    if (q.exec())
        while (q.next())
            out.append(fromRow(q));
    return out;
}
}
