// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/maptiles.h"

#include <QTest>

class MapTilesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void levelsAreEveryOtherOne()
    {
        QCOMPARE(MapTiles::levels(), (QList<int>{1, 3, 5, 7, 9, 11, 13, 15}));
        QCOMPARE(MapTiles::levels(5), (QList<int>{1, 3, 5}));
    }

    void aPointIsInTheTileMarbleFetchesForIt()
    {
        // downtown Portland: one of the tiles Marble fetched for this view (13/1303-1305/2929-2930)
        const auto tiles = MapTiles::tilesFor(-122.6766, 45.5230, -122.6764, 45.5232, 13);
        QVERIFY(tiles.contains(MapTiles::Tile{13, 1304, 2929}));
        QVERIFY(tiles.contains(MapTiles::Tile{1, 0, 0}));
        QCOMPARE(tiles.size(), 7); // one tile per level
        QCOMPARE(MapTiles::relativePath({13, 1304, 2930}), QStringLiteral("13/1304/2930.o5m"));
        QCOMPARE(MapTiles::url({13, 1304, 2930}), QUrl(QStringLiteral("https://maps.kde.org/earth/vectorosm/v1/13/1304/2930.o5m")));
    }

    void theCountMatchesTheList()
    {
        for (const auto &box : {std::array<double, 4>{-122.8, 45.4, -122.5, 45.6}, std::array<double, 4>{-124.6, 41.9, -116.4, 46.3}}) {
            QCOMPARE(MapTiles::countFor(box[0], box[1], box[2], box[3], 11), qint64(MapTiles::tilesFor(box[0], box[1], box[2], box[3], 11).size()));
        }
        // a city at street level is within what may be kept; a state is far beyond it
        QVERIFY(MapTiles::countFor(-122.8, 45.4, -122.5, 45.6) < MapTiles::MaxSaveTiles);
        QVERIFY(MapTiles::countFor(-124.6, 41.9, -116.4, 46.3) > 100 * MapTiles::MaxSaveTiles);
    }

    void aBoxAcrossThe180thMeridianIsTwoSpans()
    {
        // Fiji: from 177° E to 178° W
        const auto tiles = MapTiles::tilesFor(177.0, -19.0, -178.0, -16.0, 5);
        QCOMPARE(qint64(tiles.size()), MapTiles::countFor(177.0, -19.0, -178.0, -16.0, 5));
        bool east = false, west = false;
        for (const auto &t : tiles) {
            if (t.z == 5) {
                east = east || t.x == 31;
                west = west || t.x == 0;
            }
        }
        QVERIFY(east && west);
        // not the whole world in between
        int level5 = 0;
        for (const auto &t : tiles)
            level5 += t.z == 5;
        QVERIFY(level5 <= 4);
    }

    void polesAreClamped()
    {
        const auto tiles = MapTiles::tilesFor(-10, -90, 10, 90, 3);
        for (const auto &t : tiles) {
            QVERIFY(t.y >= 0 && t.y < (1 << t.z));
            QVERIFY(t.x >= 0 && t.x < (1 << t.z));
        }
    }

    void onlyKdesMapServerOverHttps()
    {
        QVERIFY(MapTiles::isAllowedUrl(QUrl(QStringLiteral("https://maps.kde.org/earth/vectorosm/v1/1/0/0.o5m"))));
        QVERIFY(!MapTiles::isAllowedUrl(QUrl(QStringLiteral("http://maps.kde.org/earth/vectorosm/v1/1/0/0.o5m"))));
        QVERIFY(!MapTiles::isAllowedUrl(QUrl(QStringLiteral("https://maps.kde.org.example.com/x"))));
        QVERIFY(!MapTiles::isAllowedUrl(QUrl(QStringLiteral("https://tile.openstreetmap.org/1/0/0.png"))));
    }

    void wikipediaLinksBecomeArticles()
    {
        // as Marble gives them (checked on real tiles)
        auto a = MapTiles::wikipediaArticle(QStringLiteral("https://en.wikipedia.org/wiki/Pearl%20District%2C%20Portland%2C%20Oregon"));
        QVERIFY(a);
        QCOMPARE(a->language, QStringLiteral("en"));
        QCOMPARE(a->title, QStringLiteral("Pearl District, Portland, Oregon"));
        // as OpenStreetMap tags them
        a = MapTiles::wikipediaArticle(QStringLiteral("de:Köln"));
        QVERIFY(a);
        QCOMPARE(a->language, QStringLiteral("de"));
        QCOMPARE(a->title, QStringLiteral("Köln"));
        // underscores and a section
        a = MapTiles::wikipediaArticle(QStringLiteral("https://en.wikipedia.org/wiki/Old_Town_Chinatown#History"));
        QVERIFY(a);
        QCOMPARE(a->title, QStringLiteral("Old Town Chinatown"));
        // not an article
        QVERIFY(!MapTiles::wikipediaArticle(QString()));
        QVERIFY(!MapTiles::wikipediaArticle(QStringLiteral("https://example.com/wiki/Portland")));
        QVERIFY(!MapTiles::wikipediaArticle(QStringLiteral("en:")));
    }
};

QTEST_GUILESS_MAIN(MapTilesTest)
#include "maptilestest.moc"
