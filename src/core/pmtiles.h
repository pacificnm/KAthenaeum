// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QList>
#include <cstdint>
#include <optional>

// The PMTiles v3 format (https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md): one file holding a whole tile
// pyramid, read by ranges. A region's map is cut out of Protomaps' planet build by fetching only its tiles' ranges.
namespace PMTiles
{
enum Compression : quint8 { UnknownCompression = 0, NoCompression = 1, Gzip = 2, Brotli = 3, Zstd = 4 };

constexpr int HeaderSize = 127;
constexpr int RootSpace = 16384;  // the header and the root directory are always within the first 16 KiB

struct Header {
    quint64 rootOffset = 0, rootLength = 0;
    quint64 metadataOffset = 0, metadataLength = 0;
    quint64 leavesOffset = 0, leavesLength = 0;
    quint64 dataOffset = 0, dataLength = 0;
    quint64 addressedTiles = 0, tileEntries = 0, tileContents = 0;
    bool clustered = false;
    quint8 internalCompression = Gzip;
    quint8 tileCompression = Gzip;
    quint8 tileType = 1;  // 1 = Mapbox vector tiles
    quint8 minZoom = 0, maxZoom = 0;
    qint32 minLonE7 = 0, minLatE7 = 0, maxLonE7 = 0, maxLatE7 = 0;
    quint8 centerZoom = 0;
    qint32 centerLonE7 = 0, centerLatE7 = 0;
};

std::optional<Header> parseHeader(const QByteArray &bytes);  // the first 127 bytes or more; nothing if not PMTiles v3
QByteArray serializeHeader(const Header &h);

// A directory entry: a run of tiles (runLength >= 1) sharing one content, or a leaf directory (runLength == 0)
struct Entry {
    quint64 tileId = 0;
    quint64 offset = 0;
    quint32 length = 0;
    quint32 runLength = 0;
};

std::optional<QList<Entry>> parseDirectory(const QByteArray &uncompressed);
QByteArray serializeDirectory(const QList<Entry> &entries);

// Directories as they are stored: the root, and leaves (offsets relative to the leaf section), split so the root fits
struct Directories {
    QByteArray root;
    QByteArray leaves;
};
Directories buildDirectories(const QList<Entry> &entries, quint8 compression = Gzip);

// Tile ids number the tiles of all zooms along a Hilbert curve, zoom by zoom
quint64 tileId(int z, quint32 x, quint32 y);
struct Tile {
    int z = 0;
    quint32 x = 0, y = 0;
};
Tile tileFromId(quint64 id);

// A local archive, read tile by tile (directories are cached: a click reads a tile or two)
class Archive
{
public:
    explicit Archive(const QString &path);
    bool isValid() const { return m_valid; }
    const Header &header() const { return m_header; }
    // the tile's bytes, decompressed; empty if the archive has no such tile
    QByteArray tile(int z, quint32 x, quint32 y);

private:
    std::optional<QList<Entry>> directory(quint64 offset, quint64 length);
    QByteArray read(quint64 offset, quint64 length);

    QString m_path;
    Header m_header;
    QList<Entry> m_root;
    QHash<quint64, QList<Entry>> m_leaves;  // by offset
    bool m_valid = false;
};

// gzip, as PMTiles stores directories and (from Protomaps) tiles
QByteArray gzip(const QByteArray &data);
std::optional<QByteArray> gunzip(const QByteArray &data, qsizetype limit = 256 << 20);
std::optional<QByteArray> decompress(const QByteArray &data, quint8 compression);
}
