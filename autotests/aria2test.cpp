// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "core/downloads.h"

// Lines captured from aria2c 1.37 downloading a Kiwix metalink with --summary-interval=1
class Aria2Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesProgressLines()
    {
        auto p = Aria2::parseLine(u"[#85a9a7 1.1MiB/22MiB(5%) CN:1 DL:592KiB ETA:36s]");
        QVERIFY(p);
        QCOMPARE(p->done, qint64(1.1 * 1024 * 1024));
        QCOMPARE(p->total, qint64(22) * 1024 * 1024);
        QCOMPARE(p->percent, 5);
        QCOMPARE(p->speed, qint64(592) * 1024);
        QCOMPARE(p->etaSeconds, 36);

        p = Aria2::parseLine(u"[#85a9a7 0B/22MiB(0%) CN:1 DL:0B]");  // the first line has no ETA
        QVERIFY(p);
        QCOMPARE(p->done, qint64(0));
        QCOMPARE(p->speed, qint64(0));
        QCOMPARE(p->etaSeconds, -1);

        p = Aria2::parseLine(u"[#7d9ec2 7.2MiB/22MiB(32%) CN:1 DL:15KiB ETA:16m22s]");  // a resumed download
        QVERIFY(p);
        QCOMPARE(p->percent, 32);
        QCOMPARE(p->etaSeconds, 16 * 60 + 22);

        p = Aria2::parseLine(u"  [#1 1.0GiB/50GiB(2%) CN:8 DL:50MiB ETA:1h2m3s]  ");
        QVERIFY(p);
        QCOMPARE(p->total, qint64(50) << 30);
        QCOMPARE(p->etaSeconds, 3723);
    }

    void ignoresOtherLines()
    {
        QVERIFY(!Aria2::parseLine(u" *** Download Progress Summary as of Mon Oct 19 11:11:43 2026 *** "));
        QVERIFY(!Aria2::parseLine(u"FILE: /tmp/x.zim"));
        QVERIFY(!Aria2::parseLine(u"125508|OK  |        n/a|[MEMORY]x.zim.meta4"));
        QVERIFY(!Aria2::parseLine(u""));
    }

    void parsesSizesAndDurations()
    {
        QCOMPARE(Aria2::parseSize(u"0B"), qint64(0));
        QCOMPARE(Aria2::parseSize(u"15KiB"), qint64(15 * 1024));
        QCOMPARE(Aria2::parseSize(u"2.5MiB"), qint64(2.5 * 1024 * 1024));
        QCOMPARE(Aria2::parseSize(u"1TiB"), qint64(1) << 40);
        QCOMPARE(Aria2::parseSize(u"12MB"), qint64(-1));
        QCOMPARE(Aria2::parseDuration(u"36s"), 36);
        QCOMPARE(Aria2::parseDuration(u"24m21s"), 24 * 60 + 21);
        QCOMPARE(Aria2::parseDuration(u"2h"), 7200);
        QCOMPARE(Aria2::parseDuration(u""), -1);
        QCOMPARE(Aria2::parseDuration(u"soon"), -1);
    }

    void buildsArguments()
    {
        const QStringList args = Aria2::arguments(QStringLiteral("/data/zim"), QUrl(QStringLiteral("https://download.kiwix.org/a_2026-01.zim.meta4")));
        QVERIFY(args.contains(QStringLiteral("-c")));
        QVERIFY(args.contains(QStringLiteral("--follow-metalink=mem")));
        // the user's aria2.conf is ignored, and certificates are always checked
        QVERIFY(args.contains(QStringLiteral("--no-conf=true")));
        QVERIFY(args.contains(QStringLiteral("--check-certificate=true")));
        QCOMPARE(args.at(args.indexOf(QStringLiteral("--dir")) + 1), QStringLiteral("/data/zim"));
        QCOMPARE(args.last(), QStringLiteral("https://download.kiwix.org/a_2026-01.zim.meta4"));
    }
};

QTEST_GUILESS_MAIN(Aria2Test)
#include "aria2test.moc"
