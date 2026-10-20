// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
// Maps: the PMTiles format, cutting a region out of an archive, vector tiles, what a click finds, and OpenStreetMap places
#include "core/database.h"
#include "core/mapextract.h"
#include "core/maplookup.h"
#include "core/mapplaces.h"
#include "core/mapregions.h"
#include "core/maptiling.h"
#include "core/mvt.h"
#include "core/pmtiles.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <osmium/builder/attr.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/writer.hpp>

namespace
{
// A protobuf writer, enough to make vector tiles
struct Pb {
    QByteArray b;
    void varint(quint64 v)
    {
        while (v >= 0x80) {
            b.append(char((v & 0x7f) | 0x80));
            v >>= 7;
        }
        b.append(char(v));
    }
    void key(int field, int wire)
    {
        varint(quint64(field) << 3 | quint64(wire));
    }
    void bytes(int field, const QByteArray &v)
    {
        key(field, 2);
        varint(quint64(v.size()));
        b.append(v);
    }
    void uint(int field, quint64 v)
    {
        key(field, 0);
        varint(v);
    }
};

quint32 zz(qint32 v)
{
    return quint32((v << 1) ^ (v >> 31));
}

struct PointFeature {
    quint64 id;
    int x, y;
    QList<std::pair<QString, QString>> props;
};

// One layer of point features
QByteArray tileWith(const QString &layer, const QList<PointFeature> &features)
{
    Pb l;
    l.uint(15, 2);
    l.bytes(1, layer.toUtf8());
    QStringList keys, values;
    for (const PointFeature &f : features) {
        Pb feat;
        feat.uint(1, f.id);
        Pb tags;
        for (const auto &[k, v] : f.props) {
            if (!keys.contains(k))
                keys.append(k);
            if (!values.contains(v))
                values.append(v);
            tags.varint(quint64(keys.indexOf(k)));
            tags.varint(quint64(values.indexOf(v)));
        }
        feat.bytes(2, tags.b);
        feat.uint(3, 1); // point
        Pb geom;
        geom.varint(1 | (1 << 3)); // MoveTo, 1 point
        geom.varint(zz(f.x));
        geom.varint(zz(f.y));
        feat.bytes(4, geom.b);
        l.bytes(2, feat.b);
    }
    for (const QString &k : std::as_const(keys))
        l.bytes(3, k.toUtf8());
    for (const QString &v : std::as_const(values)) {
        Pb value;
        value.bytes(1, v.toUtf8());
        l.bytes(4, value.b);
    }
    l.uint(5, 4096);
    Pb t;
    t.bytes(3, l.b);
    return t.b;
}

// Writes an archive holding the given tiles (id -> content), gzipped as Protomaps' are; equal contents stored once
void writeArchive(const QString &path, const QMap<quint64, QByteArray> &tiles, int minZoom, int maxZoom)
{
    QByteArray data;
    QHash<QByteArray, quint64> stored;
    QList<PMTiles::Entry> entries;
    for (auto it = tiles.cbegin(); it != tiles.cend(); ++it) {
        const QByteArray z = PMTiles::gzip(it.value());
        quint64 offset;
        if (stored.contains(z)) {
            offset = stored.value(z);
        } else {
            offset = quint64(data.size());
            stored.insert(z, offset);
            data.append(z);
        }
        if (!entries.isEmpty() && entries.last().offset == offset && entries.last().tileId + entries.last().runLength == it.key())
            ++entries.last().runLength;
        else
            entries.append({it.key(), offset, quint32(z.size()), 1});
    }
    const PMTiles::Directories dirs = PMTiles::buildDirectories(entries);
    const QByteArray meta = PMTiles::gzip(QByteArray("{\"name\":\"test\"}"));
    PMTiles::Header h;
    h.rootOffset = PMTiles::HeaderSize;
    h.rootLength = quint64(dirs.root.size());
    h.metadataOffset = h.rootOffset + h.rootLength;
    h.metadataLength = quint64(meta.size());
    h.leavesOffset = h.metadataOffset + h.metadataLength;
    h.leavesLength = quint64(dirs.leaves.size());
    h.dataOffset = h.leavesOffset + h.leavesLength;
    h.dataLength = quint64(data.size());
    h.addressedTiles = quint64(tiles.size());
    h.tileEntries = quint64(entries.size());
    h.tileContents = quint64(stored.size());
    h.clustered = true;
    h.minZoom = quint8(minZoom);
    h.maxZoom = quint8(maxZoom);
    h.minLonE7 = -1800000000;
    h.maxLonE7 = 1800000000;
    h.minLatE7 = -850000000;
    h.maxLatE7 = 850000000;
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(PMTiles::serializeHeader(h));
    f.write(dirs.root);
    f.write(meta);
    f.write(dirs.leaves);
    f.write(data);
}

QByteArray contentFor(int z, quint32 x, quint32 y)
{
    // open sea at zoom 7 in the far west: the same content, so the archive stores it once and runs it
    if (z == 7 && x < 8)
        return QByteArray("sea");
    return QStringLiteral("tile %1/%2/%3").arg(z).arg(x).arg(y).toUtf8();
}
}

class MapTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tileIdsFollowTheSpec()
    {
        // the PMTiles spec's examples
        QCOMPARE(PMTiles::tileId(0, 0, 0), 0u);
        QCOMPARE(PMTiles::tileId(1, 0, 0), 1u);
        QCOMPARE(PMTiles::tileId(1, 0, 1), 2u);
        QCOMPARE(PMTiles::tileId(1, 1, 1), 3u);
        QCOMPARE(PMTiles::tileId(1, 1, 0), 4u);
        QCOMPARE(PMTiles::tileId(2, 0, 0), 5u);
        // and back, at every zoom the planet has
        for (int z = 0; z <= 15; ++z) {
            const quint32 n = 1u << z;
            for (quint32 k = 0; k < 40; ++k) {
                const quint32 x = (k * 2654435761u) % n, y = (k * 40503u + 7u) % n;
                const PMTiles::Tile t = PMTiles::tileFromId(PMTiles::tileId(z, x, y));
                QCOMPARE(t.z, z);
                QCOMPARE(t.x, x);
                QCOMPARE(t.y, y);
            }
        }
    }

    void directoriesRoundTrip()
    {
        QList<PMTiles::Entry> entries{{0, 0, 100, 1}, {1, 100, 50, 3}, {7, 500, 10, 1}, {9, 0, 100, 1}, {12, 510, 20, 0}};
        const auto back = PMTiles::parseDirectory(PMTiles::serializeDirectory(entries));
        QVERIFY(back);
        QCOMPARE(back->size(), entries.size());
        for (qsizetype i = 0; i < entries.size(); ++i) {
            QCOMPARE((*back)[i].tileId, entries[i].tileId);
            QCOMPARE((*back)[i].offset, entries[i].offset);
            QCOMPARE((*back)[i].length, entries[i].length);
            QCOMPARE((*back)[i].runLength, entries[i].runLength);
        }
        QVERIFY(!PMTiles::parseDirectory(QByteArray("\xff\xff\xff", 3))); // truncated

        // too many entries for the root: leaves, and the root fits in the first 16 KiB
        QList<PMTiles::Entry> many;
        for (quint64 i = 0; i < 60000; ++i)
            many.append({i * 3, i * 1000 + (i % 7), quint32(900 + i % 50), 1});
        const PMTiles::Directories dirs = PMTiles::buildDirectories(many);
        QVERIFY(!dirs.leaves.isEmpty());
        QVERIFY(dirs.root.size() <= PMTiles::RootSpace - PMTiles::HeaderSize);
    }

    void headerAndGzipRoundTrip()
    {
        PMTiles::Header h;
        h.rootOffset = 127;
        h.dataLength = 1234567890123;
        h.maxZoom = 15;
        h.minLonE7 = -1234567890;
        h.centerLatE7 = 455202000;
        const auto back = PMTiles::parseHeader(PMTiles::serializeHeader(h));
        QVERIFY(back);
        QCOMPARE(back->dataLength, h.dataLength);
        QCOMPARE(back->minLonE7, h.minLonE7);
        QCOMPARE(back->centerLatE7, h.centerLatE7);
        QCOMPARE(int(back->maxZoom), 15);
        QVERIFY(!PMTiles::parseHeader(QByteArray(127, 'x')));

        const QByteArray text = QByteArray("Portland, Oregon ").repeated(500);
        QCOMPARE(PMTiles::gunzip(PMTiles::gzip(text)).value_or(QByteArray()), text);
        QVERIFY(!PMTiles::gunzip(QByteArray("not gzip at all")));
        QVERIFY(!PMTiles::gunzip(PMTiles::gzip(text).left(20))); // cut short
    }

    void aPointInAShape()
    {
        // a square with a square hole, and an island across the 180° meridian (as Geofabrik gives the US Pacific: two
        // polygons, one each side), whose box would be the whole world
        const MapTiling::MultiPolygon shape = {{{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}, {{4, 4}, {6, 4}, {6, 6}, {4, 6}, {4, 4}}},
                                               {{{179, -1}, {180, -1}, {180, 1}, {179, 1}, {179, -1}}},
                                               {{{-180, -1}, {-179, -1}, {-179, 1}, {-180, 1}, {-180, -1}}}};
        QVERIFY(MapTiling::contains(shape, 2, 2));
        QVERIFY(!MapTiling::contains(shape, 5, 5)); // the hole
        QVERIFY(!MapTiling::contains(shape, 20, 2));
        QVERIFY(MapTiling::contains(shape, 179.5, 0));
        QVERIFY(MapTiling::contains(shape, -179.5, 0));
        QVERIFY(!MapTiling::contains(shape, 86.9, 27.9)); // Mount Everest is not in the Pacific
        QVERIFY(MapTiling::bounds(shape).contains(QPointF(86.9, 0))); // though the box says so
        QVERIFY(!MapTiling::contains({}, 0, 0));
    }

    void regionTiles()
    {
        // a square: at zoom 10 every tile whose square meets it, and nothing else
        const MapTiling::MultiPolygon square{{{{-122.8, 45.4}, {-122.4, 45.4}, {-122.4, 45.7}, {-122.8, 45.7}, {-122.8, 45.4}}}};
        const auto ids = MapTiling::tileIds(square, 10, 10);
        const int x0 = int(MapTiling::lonToX(-122.8, 10)), x1 = int(MapTiling::lonToX(-122.4, 10));
        const int y0 = int(MapTiling::latToY(45.7, 10)), y1 = int(MapTiling::latToY(45.4, 10));
        QCOMPARE(qsizetype(ids.size()), qsizetype((x1 - x0 + 1) * (y1 - y0 + 1)));
        QCOMPARE(quint64(ids.size()), MapTiling::tileCount(square, 10, 10));
        QVERIFY(std::is_sorted(ids.begin(), ids.end()));

        // a hole is left out
        const MapTiling::MultiPolygon ring{{{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}, {{2, 2}, {8, 2}, {8, 8}, {2, 8}, {2, 2}}}};
        const int z = 8;
        const quint64 inHole = PMTiles::tileId(z, quint32(MapTiling::lonToX(5, z)), quint32(MapTiling::latToY(5, z)));
        const quint64 onRing = PMTiles::tileId(z, quint32(MapTiling::lonToX(1, z)), quint32(MapTiling::latToY(1, z)));
        const auto ringIds = MapTiling::tileIds(ring, z, z);
        QVERIFY(!std::binary_search(ringIds.begin(), ringIds.end(), inHole));
        QVERIFY(std::binary_search(ringIds.begin(), ringIds.end(), onRing));

        // coordinates and back
        QVERIFY(qAbs(MapTiling::yToLat(MapTiling::latToY(45.52, 12), 12) - 45.52) < 1e-9);
        QVERIFY(qAbs(MapTiling::xToLon(MapTiling::lonToX(-122.67, 12), 12) + 122.67) < 1e-9);
    }

    void aLeafPointingAtItselfIsRefused()
    {
        // a hostile or broken map file: the root's one entry is a leaf, and the leaf's one entry points at the leaf
        // itself. Followed without a limit, it was fetched for ever.
        QByteArray leaf;
        quint32 length = 8;
        for (int i = 0; i < 4; ++i) { // the leaf holds its own length: settle on a size that does not change
            leaf = PMTiles::serializeDirectory({{0, 0, length, 0}});
            length = quint32(leaf.size());
        }
        const QByteArray root = PMTiles::serializeDirectory({{0, 0, length, 0}});
        PMTiles::Header h;
        h.internalCompression = PMTiles::NoCompression;
        h.rootOffset = PMTiles::HeaderSize;
        h.rootLength = quint64(root.size());
        h.metadataOffset = h.rootOffset + h.rootLength;
        h.metadataLength = 0;
        h.leavesOffset = h.metadataOffset;
        h.leavesLength = quint64(leaf.size());
        h.dataOffset = h.leavesOffset + h.leavesLength;
        h.maxZoom = 3;
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("cycle.pmtiles"));
        QFile f(source);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(PMTiles::serializeHeader(h) + root + leaf);
        f.close();

        FileRangeReader reader(source);
        MapExtract extract(&reader, {{{{-10, 36}, {15, 36}, {15, 55}, {-10, 55}, {-10, 36}}}}, 3, dir.filePath(QStringLiteral("out.pmtiles")));
        QSignalSpy finished(&extract, &MapExtract::finished);
        extract.start();
        QVERIFY(finished.wait(5000));
        QVERIFY(!finished.first().first().toString().isEmpty()); // an error, not an endless fetch
        QVERIFY2(reader.reads() <= 6, qPrintable(QString::number(reader.reads())));
    }

    void extractCutsTheRegionOut()
    {
        QTemporaryDir dir;
        // zoom 0 to 7, every tile: 21,845 tiles, so the directory needs leaves
        QMap<quint64, QByteArray> tiles;
        for (int z = 0; z <= 7; ++z)
            for (quint32 x = 0; x < (1u << z); ++x)
                for (quint32 y = 0; y < (1u << z); ++y)
                    tiles.insert(PMTiles::tileId(z, x, y), contentFor(z, x, y));
        const QString source = dir.filePath(QStringLiteral("planet.pmtiles"));
        writeArchive(source, tiles, 0, 7);
        QVERIFY(PMTiles::Archive(source).isValid());

        // western Europe
        const MapTiling::MultiPolygon shape{{{{-10, 36}, {15, 36}, {15, 55}, {-10, 55}, {-10, 36}}}};
        const QString out = dir.filePath(QStringLiteral("region.pmtiles"));
        FileRangeReader reader(source);
        MapExtract extract(&reader, shape, 6, out);
        QSignalSpy finished(&extract, &MapExtract::finished);
        extract.start();
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().first().toString(), QString());
        QVERIFY(!QFile::exists(out + QStringLiteral(".data")));

        PMTiles::Archive region(out);
        QVERIFY(region.isValid());
        QCOMPARE(int(region.header().maxZoom), 6); // less detail than the source, as asked
        const auto wanted = MapTiling::tileIds(shape, 0, 6);
        QCOMPARE(qint64(wanted.size()), extract.tileCount());
        for (quint64 id : wanted) {
            const PMTiles::Tile t = PMTiles::tileFromId(id);
            QCOMPARE(region.tile(t.z, t.x, t.y), contentFor(t.z, t.x, t.y));
        }
        // outside the region, and deeper than asked: nothing
        QVERIFY(region.tile(6, 10, 10).isEmpty()); // the Pacific
        QVERIFY(region.tile(7, 63, 42).isEmpty());
        // the reads were merged: far fewer than one per tile
        QVERIFY2(reader.reads() < int(wanted.size()) / 4, qPrintable(QString::number(reader.reads())));

        // measuring only: no file, the same figures
        const QString measured = dir.filePath(QStringLiteral("measured.pmtiles"));
        FileRangeReader reader2(source);
        MapExtract estimate(&reader2, shape, 6, measured);
        estimate.setEstimateOnly(true);
        QSignalSpy done(&estimate, &MapExtract::finished);
        estimate.start();
        QVERIFY(done.wait(10000));
        QCOMPARE(estimate.tileCount(), extract.tileCount());
        QCOMPARE(estimate.totalBytes(), extract.totalBytes());
        QVERIFY(!QFile::exists(measured));
    }

    void dedupedTilesStayShared()
    {
        QTemporaryDir dir;
        QMap<quint64, QByteArray> tiles;
        for (int z = 0; z <= 7; ++z)
            for (quint32 x = 0; x < (1u << z); ++x)
                for (quint32 y = 0; y < (1u << z); ++y)
                    tiles.insert(PMTiles::tileId(z, x, y), contentFor(z, x, y));
        const QString source = dir.filePath(QStringLiteral("planet.pmtiles"));
        writeArchive(source, tiles, 0, 7);
        // the far west at zoom 7 is all "sea": in the extract it is stored once too
        const MapTiling::MultiPolygon shape{{{{-179, -60}, {-170, -60}, {-170, 60}, {-179, 60}, {-179, -60}}}};
        FileRangeReader reader(source);
        MapExtract extract(&reader, shape, 7, dir.filePath(QStringLiteral("sea.pmtiles")));
        QSignalSpy finished(&extract, &MapExtract::finished);
        extract.start();
        QVERIFY(finished.wait(10000));
        PMTiles::Archive sea(dir.filePath(QStringLiteral("sea.pmtiles")));
        QVERIFY(sea.isValid());
        QVERIFY(sea.header().tileContents < sea.header().addressedTiles);
        QCOMPARE(sea.tile(7, 1, 60), QByteArray("sea"));
    }

    void vectorTilesDecode()
    {
        const QByteArray tile =
            tileWith(QStringLiteral("pois"),
                     {{(quint64(2) << 44) | 31732583,
                       1000,
                       2000,
                       {{QStringLiteral("name"), QStringLiteral("Pioneer Courthouse")}, {QStringLiteral("kind"), QStringLiteral("courthouse")}}}});
        const auto features = Mvt::decode(tile);
        QVERIFY(features);
        QCOMPARE(features->size(), 1);
        const Mvt::Feature &f = features->first();
        QCOMPARE(f.layer, QStringLiteral("pois"));
        QCOMPARE(f.type, Mvt::Point);
        QCOMPARE(f.properties.value(QStringLiteral("name")).toString(), QStringLiteral("Pioneer Courthouse"));
        QCOMPARE(f.parts.first().first(), QPointF(1000, 2000));
        QCOMPARE(Mvt::distance(f, QPointF(1003, 2004)), 5.0);
        QCOMPARE(MapLookup::osmKey(f.id), QStringLiteral("w31732583"));
        QVERIFY(Mvt::decode(tile, {QStringLiteral("places")})->isEmpty()); // other layers only
        QVERIFY(!Mvt::decode(QByteArray("\x1a\xff\xff\xff\xff\x0f", 6))); // a length past the end

        Mvt::Feature square;
        square.type = Mvt::Polygon;
        square.parts = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}};
        QVERIFY(Mvt::contains(square, {5, 5}));
        QVERIFY(!Mvt::contains(square, {15, 5}));
    }

    // tiles and directories come from the network: nothing they hold may crash a parser (run under ASan to mean it)
    void hostileBytesAreRefused()
    {
        const QByteArray tile = tileWith(QStringLiteral("places"), {{1, 5, 5, {{QStringLiteral("name"), QStringLiteral("X")}}}});
        for (qsizetype n = 0; n < tile.size(); ++n)
            Mvt::decode(tile.left(n)); // every truncation
        const QByteArray dir = PMTiles::serializeDirectory({{0, 0, 100, 1}, {5, 100, 20, 0}});
        for (qsizetype n = 0; n < dir.size(); ++n)
            PMTiles::parseDirectory(dir.left(n));
        quint32 seed = 12345;
        auto next = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return char(seed >> 24);
        };
        for (int i = 0; i < 3000; ++i) {
            QByteArray junk(1 + i % 300, Qt::Uninitialized);
            for (char &c : junk)
                c = next();
            Mvt::decode(junk);
            PMTiles::parseDirectory(junk);
            PMTiles::parseHeader(QByteArray("PMTiles\x03", 8) + junk.leftJustified(127, '\0'));
            PMTiles::gunzip(QByteArray("\x1f\x8b\x08\x00", 4) + junk);
        }
        // a directory claiming more entries than it has bytes
        QVERIFY(!PMTiles::parseDirectory(QByteArray("\xff\xff\x03", 3)));
    }

    void osmKeysAndCoordinates()
    {
        QCOMPARE(MapLookup::osmKey((quint64(1) << 44) | 1666626393), QStringLiteral("n1666626393"));
        QCOMPARE(MapLookup::osmKey((quint64(3) << 44) | 147401), QStringLiteral("r147401"));
        QCOMPARE(MapLookup::osmKey(12345), QString()); // no type: not an OpenStreetMap id
        const QPointF centre = MapLookup::coordinateAt(45.52, -122.67, 13, 0, 0);
        QVERIFY(qAbs(centre.x() - 45.52) < 1e-9 && qAbs(centre.y() + 122.67) < 1e-9);
        // 512 pixels at zoom 0 are the whole world
        QVERIFY(qAbs(MapLookup::coordinateAt(0, 0, 0, 256, 0).y() - 180) < 1e-9);
        // east and south are +x and +y
        const QPointF se = MapLookup::coordinateAt(45.52, -122.67, 13, 100, 100);
        QVERIFY(se.x() < 45.52 && se.y() > -122.67);
    }

    void aClickFindsTheNearestLabel()
    {
        QTemporaryDir dir;
        const double lat = 45.5186, lon = -122.6784;
        const int z = 15;
        const double x = MapTiling::lonToX(lon, z), y = MapTiling::latToY(lat, z);
        const quint32 tx = quint32(x), ty = quint32(y);
        auto at = [&](double fx, double fy) {
            return std::pair<int, int>(int((fx - tx) * 4096), int((fy - ty) * 4096));
        };
        const auto [cx, cy] = at(x, y);
        const QByteArray pois = tileWith(QStringLiteral("pois"),
                                         {{(quint64(2) << 44) | 31732583,
                                           cx + 40,
                                           cy,
                                           {{QStringLiteral("name"), QStringLiteral("Pioneer Courthouse")},
                                            {QStringLiteral("kind"), QStringLiteral("courthouse")},
                                            {QStringLiteral("min_zoom"), QStringLiteral("14")}}},
                                          {(quint64(1) << 44) | 99, cx + 400, cy, {{QStringLiteral("name"), QStringLiteral("Far Away Cafe")}}},
                                          {(quint64(1) << 44) | 98,
                                           cx,
                                           cy + 10,
                                           {{QStringLiteral("name"), QStringLiteral("Hidden Until 17")}, {QStringLiteral("min_zoom"), QStringLiteral("17")}}}});
        QMap<quint64, QByteArray> tiles{{PMTiles::tileId(z, tx, ty), pois}};
        const QString path = dir.filePath(QStringLiteral("t.pmtiles"));
        writeArchive(path, tiles, z, z);
        PMTiles::Archive archive(path);
        QVERIFY(archive.isValid());
        // at zoom 15 a tile is 512 pixels: 40 units of 4096 are 5 pixels
        const auto f = MapLookup::nearest(archive, lat, lon, 15, 20);
        QVERIFY(f);
        QCOMPARE(f->name, QStringLiteral("Pioneer Courthouse")); // the one hidden at this zoom is skipped
        QCOMPARE(f->osm, QStringLiteral("w31732583"));
        QCOMPARE(f->kind, QStringLiteral("courthouse"));
        QVERIFY(qAbs(f->distancePx - 5) < 0.5);
        QVERIFY(!MapLookup::nearest(archive, lat, lon - 0.01, 15, 20)); // nothing within 20 pixels
    }

    void placeTitles()
    {
        MapPlace p;
        p.wikipedia = QStringLiteral("en:Portland, Oregon");
        QCOMPARE(MapPlaces::articleTitle(p, QStringLiteral("en")), QStringLiteral("Portland, Oregon"));
        QCOMPARE(MapPlaces::articleTitle(p, QStringLiteral("de")), QString());
        p.wikipedia = QStringLiteral("de:Köln");
        p.wikipediaEn = QStringLiteral("Cologne");
        QCOMPARE(MapPlaces::articleTitle(p, QStringLiteral("en")), QStringLiteral("Cologne"));
        QCOMPARE(MapPlaces::articleTitle(p, QStringLiteral("de")), QStringLiteral("Köln"));
        p.wikipedia = QStringLiteral("Just a title: with a colon");
        p.wikipediaEn.clear();
        QCOMPARE(MapPlaces::articleTitle(p, QStringLiteral("en")), QString());

        auto kind = [](QHash<QString, QString> tags) {
            return MapPlaces::kindOf([&tags](const char *key) {
                return tags.value(QString::fromLatin1(key));
            });
        };
        QCOMPARE(kind({{QStringLiteral("place"), QStringLiteral("city")}, {QStringLiteral("boundary"), QStringLiteral("administrative")}}),
                 QStringLiteral("place=city"));
        QCOMPARE(kind({{QStringLiteral("boundary"), QStringLiteral("administrative")},
                       {QStringLiteral("admin_level"), QStringLiteral("6")},
                       {QStringLiteral("border_type"), QStringLiteral("county")}}),
                 QStringLiteral("boundary=county"));
        QCOMPARE(kind({{QStringLiteral("boundary"), QStringLiteral("administrative")}, {QStringLiteral("admin_level"), QStringLiteral("4")}}),
                 QStringLiteral("boundary=state"));
        QCOMPARE(kind({{QStringLiteral("boundary"), QStringLiteral("protected_area")}}), QStringLiteral("boundary=protected_area"));
        QCOMPARE(kind({{QStringLiteral("building"), QStringLiteral("no")}}), QString());
    }

    void placesAreReadFromOpenStreetMapData()
    {
        using namespace osmium::builder::attr;
        QTemporaryDir dir;
        const std::string path = dir.filePath(QStringLiteral("test.osm.pbf")).toStdString();
        {
            osmium::memory::Buffer buffer{4096, osmium::memory::Buffer::auto_grow::yes};
            // a city node, an untagged node, a park's corners
            osmium::builder::add_node(buffer,
                                      _id(1),
                                      _location(-122.6742, 45.5202),
                                      _tag("name", "Portland"),
                                      _tag("place", "city"),
                                      _tag("wikipedia", "en:Portland, Oregon"),
                                      _tag("population", "652,503"));
            osmium::builder::add_node(buffer, _id(2), _location(-122.0, 45.0));
            osmium::builder::add_node(buffer, _id(10), _location(-122.70, 45.50));
            osmium::builder::add_node(buffer, _id(11), _location(-122.60, 45.50));
            osmium::builder::add_node(buffer, _id(12), _location(-122.60, 45.60));
            osmium::builder::add_node(buffer, _id(13), _location(-122.70, 45.60));
            osmium::builder::add_node(buffer, _id(20), _location(-122.65, 45.55), _tag("name", "Lake Label"));
            osmium::builder::add_node(buffer, _id(30), _location(-123.0, 46.0));
            osmium::builder::add_node(buffer, _id(31), _location(-122.9, 46.0));
            // a park drawn as a closed way, with an article
            osmium::builder::add_way(buffer,
                                     _id(100),
                                     _nodes({10, 11, 12, 13, 10}),
                                     _tag("name", "Forest Park"),
                                     _tag("leisure", "park"),
                                     _tag("wikipedia", "en:Forest Park (Portland, Oregon)"));
            // a road with an article: a line, not an area
            osmium::builder::add_way(buffer, _id(101), _nodes({30, 31}), _tag("name", "Interstate 5"), _tag("highway", "motorway"), _tag("wikidata", "Q94"));
            // a way without tags, used by the lake relation
            osmium::builder::add_way(buffer, _id(102), _nodes({10, 11, 12, 13, 10}));
            // a lake as a multipolygon, with its label node
            osmium::builder::add_relation(buffer,
                                          _id(500),
                                          _member(osmium::item_type::way, 102, "outer"),
                                          _member(osmium::item_type::node, 20, "label"),
                                          _tag("type", "multipolygon"),
                                          _tag("name", "Test Lake"),
                                          _tag("natural", "water"),
                                          _tag("wikidata", "Q1"));
            osmium::io::Header header;
            osmium::io::Writer writer{path, header, osmium::io::overwrite::allow};
            writer(std::move(buffer));
            writer.close();
        }
        std::atomic<bool> stop{false};
        QString error;
        const auto places = MapPlaces::read(QString::fromStdString(path), stop, &error);
        QVERIFY2(places, qPrintable(error));
        QHash<QString, MapPlace> byOsm;
        for (const MapPlace &p : *places)
            byOsm.insert(p.osm, p);
        QCOMPARE(byOsm.size(), 4); // Portland, Forest Park, Interstate 5, Test Lake (not the untagged node or way)
        QCOMPARE(byOsm.value(QStringLiteral("n1")).kind, QStringLiteral("place=city"));
        QCOMPARE(byOsm.value(QStringLiteral("n1")).population, 652503);
        const MapPlace park = byOsm.value(QStringLiteral("w100"));
        QVERIFY(park.area);
        QCOMPARE(park.west, -122.70);
        QCOMPARE(park.north, 45.60);
        QVERIFY(!byOsm.value(QStringLiteral("w101")).area); // a road is a line
        const MapPlace lake = byOsm.value(QStringLiteral("r500"));
        QVERIFY(lake.area);
        QCOMPARE(lake.lat, 45.55); // at its label, not the middle of its outline
        QCOMPARE(lake.east, -122.60);
        QVERIFY(MapPlaces::outlineContains(lake.outline, 45.55, -122.65));
        QVERIFY(!MapPlaces::outlineContains(lake.outline, 45.55, -122.55));
        QVERIFY(MapPlaces::outlineContains(park.outline, 45.52, -122.62));
        QVERIFY(byOsm.value(QStringLiteral("n1")).outline.isEmpty()); // a point has none

        // stopped: nothing
        std::atomic<bool> stopped{true};
        QVERIFY(!MapPlaces::read(QString::fromStdString(path), stopped, &error));
        // not OpenStreetMap data: why
        QFile junk(dir.filePath(QStringLiteral("junk.osm.pbf")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("this is not a pbf file at all");
        junk.close();
        error.clear();
        QVERIFY(!MapPlaces::read(junk.fileName(), stop, &error));
        QVERIFY(!error.isEmpty());
    }

    void outlines()
    {
        // Douglas-Peucker: a straight run loses its middle points, a corner stays, the ends always stay
        const QList<QPointF> line{{0, 0}, {1, 0.00001}, {2, 0}, {2, 1}, {2, 2}};
        QCOMPARE(MapPlaces::simplify(line, 0.001), (QList<QPointF>{{0, 0}, {2, 0}, {2, 2}}));

        // a lake with an island, drawn as separate ways in any order and direction
        const QByteArray lake = MapPlaces::encodeOutline({
            {{0, 0}, {10, 0}, {10, 5}},
            {{2, 2}, {2, 4}, {4, 4}, {4, 2}, {2, 2}}, // the island (a hole)
            {{0, 0}, {0, 10}, {10, 10}}, // reversed
            {{10, 10}, {10, 5}},
        });
        QVERIFY(MapPlaces::outlineContains(lake, 7, 7)); // (lat 7, lon 7): water
        QVERIFY(!MapPlaces::outlineContains(lake, 3, 3)); // the island
        QVERIFY(!MapPlaces::outlineContains(lake, 7, 12)); // east of it
        QVERIFY(!MapPlaces::outlineContains(QByteArray("\xff\xff\xff\x7f", 4), 1, 1)); // damaged: not inside

        // an L-shaped county: its box holds the corner it does not cover
        const QByteArray county = MapPlaces::encodeOutline({{{0, 0}, {10, 0}, {10, 3}, {3, 3}, {3, 10}, {0, 10}, {0, 0}}});
        QVERIFY(MapPlaces::outlineContains(county, 1, 8));
        QVERIFY(!MapPlaces::outlineContains(county, 8, 8));
    }

    void placesAreStoredAndFound()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY2(Database::open(dir.filePath(QStringLiteral("t.sqlite")), &error), qPrintable(error));
        MapRegionInfo oregon;
        oregon.id = QStringLiteral("us/oregon");
        oregon.name = QStringLiteral("Oregon");
        oregon.pbf = QUrl(QStringLiteral("https://download.geofabrik.de/north-america/us/oregon-latest.osm.pbf"));
        oregon.shape = {{{{-124.6, 42}, {-116.5, 42}, {-116.5, 46.3}, {-124.6, 46.3}, {-124.6, 42}}}};
        const qint64 id = MapRegions::add(oregon, QStringLiteral("United States of America"), 13, &error);
        QVERIFY2(id > 0, qPrintable(error));
        QCOMPARE(MapRegions::byId(id)->status, QStringLiteral("queued"));
        QCOMPARE(MapRegions::byId(id)->shape.size(), 1);
        QCOMPARE(MapRegions::byId(id)->pbf, oregon.pbf);

        std::vector<MapPlace> places(3);
        places[0] = {QStringLiteral("n1"),
                     QStringLiteral("Portland"),
                     {},
                     QStringLiteral("place=city"),
                     45.52,
                     -122.67,
                     -122.67,
                     45.52,
                     -122.67,
                     45.52,
                     false,
                     {},
                     QStringLiteral("en:Portland, Oregon"),
                     {},
                     QStringLiteral("Q6106"),
                     652503};
        places[1] = {QStringLiteral("r2"),
                     QStringLiteral("Multnomah County"),
                     {},
                     QStringLiteral("boundary=county"),
                     45.5,
                     -122.4,
                     -122.93,
                     45.43,
                     -121.82,
                     45.73,
                     true,
                     {},
                     QStringLiteral("en:Multnomah County, Oregon"),
                     {},
                     {},
                     0};
        places[2] = {QStringLiteral("w3"),
                     QStringLiteral("Forest Park"),
                     {},
                     QStringLiteral("leisure=park"),
                     45.57,
                     -122.75,
                     -122.8,
                     45.53,
                     -122.7,
                     45.6,
                     true,
                     {},
                     QStringLiteral("en:Forest Park (Portland, Oregon)"),
                     {},
                     {},
                     0};
        // the county's outline leaves out the park's corner of its box: there, only the park
        places[1].outline = MapPlaces::encodeOutline(
            {{{-122.93, 45.43}, {-121.82, 45.43}, {-121.82, 45.73}, {-122.72, 45.73}, {-122.72, 45.58}, {-122.93, 45.58}, {-122.93, 45.43}}});
        QVERIFY2(MapPlaces::store(id, places, &error), qPrintable(error));
        QCOMPARE(MapPlaces::around(45.59, -122.78).size(), 1);
        QCOMPARE(MapPlaces::count(id), 3);
        QCOMPARE(MapPlaces::byOsm(QStringLiteral("r2"))->name, QStringLiteral("Multnomah County"));
        // in the park: the park first (the smaller area), then the county; never the city (a point)
        const QList<MapPlace> around = MapPlaces::around(45.56, -122.76);
        QCOMPARE(around.size(), 2);
        QCOMPARE(around[0].name, QStringLiteral("Forest Park"));
        QCOMPARE(around[1].name, QStringLiteral("Multnomah County"));
        QCOMPARE(MapPlaces::search(QStringLiteral("port")).constFirst().name, QStringLiteral("Portland"));
        QCOMPARE(MapPlaces::search(QStringLiteral("park")).constFirst().name, QStringLiteral("Forest Park"));
        QVERIFY(MapPlaces::search(QStringLiteral("100%_")).isEmpty()); // LIKE's wildcards are only letters
        // "Show on Map": the place an article is about, from the tag OpenStreetMap gives it
        QCOMPARE(MapPlaces::byArticle(QStringLiteral("en"), QStringLiteral("Forest Park (Portland, Oregon)"))->osm, QStringLiteral("w3"));
        QCOMPARE(MapPlaces::byArticle(QStringLiteral("en"), QStringLiteral("Portland, Oregon"))->name, QStringLiteral("Portland"));
        QVERIFY(!MapPlaces::byArticle(QStringLiteral("de"), QStringLiteral("Portland, Oregon"))); // another language's article
        QVERIFY(!MapPlaces::byArticle(QStringLiteral("en"), QStringLiteral("Portland")));
        QVERIFY(!MapPlaces::byArticle(QString(), QStringLiteral("Portland, Oregon")));

        // storing again replaces; removing the region removes its places (and their boxes)
        QVERIFY(MapPlaces::store(id, {places[0]}, &error));
        QCOMPARE(MapPlaces::count(id), 1);
        QVERIFY(MapPlaces::around(45.56, -122.76).isEmpty());
        QVERIFY(MapRegions::remove(id));
        QVERIFY(!MapPlaces::byOsm(QStringLiteral("n1")));
        QSqlQuery q(QStringLiteral("SELECT COUNT(*) FROM map_places_box"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        Database::close();
    }

    void regionList()
    {
        QFile sample(QFINDTESTDATA("data/geofabrik_sample.json"));
        QVERIFY(sample.open(QIODevice::ReadOnly));
        const QByteArray json = sample.readAll();
        const auto list = MapCatalog::parseIndex(json);
        QVERIFY(list);
        QHash<QString, MapRegionInfo> byId;
        for (const MapRegionInfo &r : *list)
            byId.insert(r.id, r);
        QVERIFY(!byId.contains(QStringLiteral("evil"))); // its data is not on Geofabrik
        QCOMPARE(byId.value(QStringLiteral("us/new-hampshire")).name, QStringLiteral("New Hampshire"));
        QCOMPARE(byId.value(QStringLiteral("us/new-hampshire")).parent, QStringLiteral("us"));
        QCOMPARE(byId.value(QStringLiteral("us/district-of-columbia")).name, QStringLiteral("District of Columbia"));
        QCOMPARE(byId.value(QStringLiteral("nord-norge")).name, QStringLiteral("Nord-Norge (Northern Norway)"));
        QCOMPARE(byId.value(QStringLiteral("nord-norge")).parent, QString()); // Norway is not in this list
        QCOMPARE(byId.value(QStringLiteral("us/new-hampshire")).shape.size(), 1);

        QCOMPARE(MapCatalog::latestBuild(QByteArrayLiteral(
                     "[{\"key\":\"20261017.pmtiles\"},{\"key\":\"20261019.pmtiles\"},{\"key\":\"20261018.pmtiles\"},{\"key\":\"../evil.pmtiles\"}]")),
                 QStringLiteral("20261019.pmtiles"));
        QVERIFY(MapCatalog::isAllowedUrl(QUrl(QStringLiteral("https://build.protomaps.com/20261019.pmtiles"))));
        QVERIFY(!MapCatalog::isAllowedUrl(QUrl(QStringLiteral("http://build.protomaps.com/20261019.pmtiles"))));
        QVERIFY(!MapCatalog::isAllowedUrl(QUrl(QStringLiteral("https://build.protomaps.com.evil.org/x"))));
        QVERIFY(!MapCatalog::isAllowedUrl(QUrl(QStringLiteral("https://user@download.geofabrik.de/x"))));
    }
};

QTEST_GUILESS_MAIN(MapTest)
#include "maptest.moc"
