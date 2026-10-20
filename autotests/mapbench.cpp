// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
// Not a test: cuts a region out of a PMTiles archive on the web and reports what it took.
//   mapbench <archive url> <west> <south> <east> <north> <max zoom> <out.pmtiles>
// e.g. mapbench https://build.protomaps.com/20261019.pmtiles -122.85 45.4 -122.45 45.65 15 portland.pmtiles
#include "core/mapextract.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() != 8) {
        fprintf(stderr, "usage: mapbench <archive url> <west> <south> <east> <north> <max zoom> <out.pmtiles>\n");
        return 2;
    }
    const double w = a[2].toDouble(), s = a[3].toDouble(), e = a[4].toDouble(), n = a[5].toDouble();
    const MapTiling::MultiPolygon shape{{{{w, s}, {e, s}, {e, n}, {w, n}, {w, s}}}};
    QNetworkAccessManager network;
    auto *reader = new HttpRangeReader(QUrl(a[1]), &network, &app);
    int requests = 0;
    MapExtract extract(reader, shape, a[6].toInt(), a[7]);
    QElapsedTimer timer;
    timer.start();
    QObject::connect(&extract, &MapExtract::phaseChanged, &extract, [&](MapExtract::Phase p) {
        printf("%6.1f s  phase %d", timer.elapsed() / 1000.0, int(p));
        if (p == MapExtract::Tiles)
            printf("  (%lld tiles, %.1f MB to fetch)", extract.tileCount(), extract.totalBytes() / 1e6);
        printf("\n");
        fflush(stdout);
        if (p == MapExtract::Tiles && qEnvironmentVariableIsSet("DRY")) {  // DRY=1: say how big, fetch nothing
            extract.cancel();
            app.exit(0);
        }
    });
    QObject::connect(&extract, &MapExtract::progress, &extract, [&](qint64 done, qint64 total) {
        ++requests;
        if (requests % 20 == 0)
            printf("%6.1f s  %.1f of %.1f MB\n", timer.elapsed() / 1000.0, done / 1e6, total / 1e6), fflush(stdout);
    });
    QObject::connect(&extract, &MapExtract::finished, &extract, [&](const QString &error) {
        printf("%6.1f s  %s\n", timer.elapsed() / 1000.0, error.isEmpty() ? "done" : qPrintable(error));
        app.exit(error.isEmpty() ? 0 : 1);
    });
    extract.start();
    return app.exec();
}
