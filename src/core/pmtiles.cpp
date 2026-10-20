// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pmtiles.h"

#include <QFile>
#include <QtEndian>
#include <zlib.h>

namespace PMTiles
{
namespace
{
template<typename T>
T readLE(const QByteArray &b, int at)
{
    return qFromLittleEndian<T>(b.constData() + at);
}

template<typename T>
void writeLE(QByteArray &b, int at, T value)
{
    qToLittleEndian<T>(value, b.data() + at);
}

void putVarint(QByteArray &out, quint64 v)
{
    while (v >= 0x80) {
        out.append(char((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.append(char(v));
}

bool getVarint(const QByteArray &in, qsizetype &pos, quint64 &v)
{
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (pos >= in.size())
            return false;
        const auto byte = quint8(in.at(pos++));
        v |= quint64(byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

// The Hilbert curve on a 2^z grid (Wikipedia's xy2d and d2xy)
void rotate(quint64 n, quint64 &x, quint64 &y, quint64 rx, quint64 ry)
{
    if (ry == 0) {
        if (rx == 1) {
            x = n - 1 - x;
            y = n - 1 - y;
        }
        std::swap(x, y);
    }
}

quint64 tilesBefore(int z)  // 4^0 + 4^1 + ... + 4^(z-1)
{
    return ((quint64(1) << (2 * z)) - 1) / 3;
}
}

std::optional<Header> parseHeader(const QByteArray &b)
{
    if (b.size() < HeaderSize || !b.startsWith("PMTiles") || quint8(b.at(7)) != 3)
        return std::nullopt;
    Header h;
    h.rootOffset = readLE<quint64>(b, 8);
    h.rootLength = readLE<quint64>(b, 16);
    h.metadataOffset = readLE<quint64>(b, 24);
    h.metadataLength = readLE<quint64>(b, 32);
    h.leavesOffset = readLE<quint64>(b, 40);
    h.leavesLength = readLE<quint64>(b, 48);
    h.dataOffset = readLE<quint64>(b, 56);
    h.dataLength = readLE<quint64>(b, 64);
    h.addressedTiles = readLE<quint64>(b, 72);
    h.tileEntries = readLE<quint64>(b, 80);
    h.tileContents = readLE<quint64>(b, 88);
    h.clustered = b.at(96) == 1;
    h.internalCompression = quint8(b.at(97));
    h.tileCompression = quint8(b.at(98));
    h.tileType = quint8(b.at(99));
    h.minZoom = quint8(b.at(100));
    h.maxZoom = quint8(b.at(101));
    h.minLonE7 = readLE<qint32>(b, 102);
    h.minLatE7 = readLE<qint32>(b, 106);
    h.maxLonE7 = readLE<qint32>(b, 110);
    h.maxLatE7 = readLE<qint32>(b, 114);
    h.centerZoom = quint8(b.at(118));
    h.centerLonE7 = readLE<qint32>(b, 119);
    h.centerLatE7 = readLE<qint32>(b, 123);
    if (h.maxZoom > 31 || h.minZoom > h.maxZoom)
        return std::nullopt;
    return h;
}

QByteArray serializeHeader(const Header &h)
{
    QByteArray b(HeaderSize, '\0');
    memcpy(b.data(), "PMTiles", 7);
    b[7] = 3;
    writeLE<quint64>(b, 8, h.rootOffset);
    writeLE<quint64>(b, 16, h.rootLength);
    writeLE<quint64>(b, 24, h.metadataOffset);
    writeLE<quint64>(b, 32, h.metadataLength);
    writeLE<quint64>(b, 40, h.leavesOffset);
    writeLE<quint64>(b, 48, h.leavesLength);
    writeLE<quint64>(b, 56, h.dataOffset);
    writeLE<quint64>(b, 64, h.dataLength);
    writeLE<quint64>(b, 72, h.addressedTiles);
    writeLE<quint64>(b, 80, h.tileEntries);
    writeLE<quint64>(b, 88, h.tileContents);
    b[96] = h.clustered ? 1 : 0;
    b[97] = char(h.internalCompression);
    b[98] = char(h.tileCompression);
    b[99] = char(h.tileType);
    b[100] = char(h.minZoom);
    b[101] = char(h.maxZoom);
    writeLE<qint32>(b, 102, h.minLonE7);
    writeLE<qint32>(b, 106, h.minLatE7);
    writeLE<qint32>(b, 110, h.maxLonE7);
    writeLE<qint32>(b, 114, h.maxLatE7);
    b[118] = char(h.centerZoom);
    writeLE<qint32>(b, 119, h.centerLonE7);
    writeLE<qint32>(b, 123, h.centerLatE7);
    return b;
}

std::optional<QList<Entry>> parseDirectory(const QByteArray &in)
{
    qsizetype pos = 0;
    quint64 n = 0;
    // every entry takes at least a byte for each of its four fields: a count beyond that is a lie, and would allocate it
    if (!getVarint(in, pos, n) || n > quint64(in.size()) / 4)
        return std::nullopt;
    QList<Entry> entries(static_cast<qsizetype>(n));
    quint64 last = 0, v = 0;
    for (Entry &e : entries) {
        if (!getVarint(in, pos, v))
            return std::nullopt;
        last += v;
        e.tileId = last;
    }
    for (Entry &e : entries) {
        if (!getVarint(in, pos, v) || v > UINT32_MAX)
            return std::nullopt;
        e.runLength = quint32(v);
    }
    for (Entry &e : entries) {
        if (!getVarint(in, pos, v) || v > UINT32_MAX)
            return std::nullopt;
        e.length = quint32(v);
    }
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (!getVarint(in, pos, v))
            return std::nullopt;
        if (v == 0 && i > 0)
            entries[i].offset = entries[i - 1].offset + entries[i - 1].length;  // right after the previous one
        else if (v == 0)
            return std::nullopt;
        else
            entries[i].offset = v - 1;
    }
    return entries;
}

QByteArray serializeDirectory(const QList<Entry> &entries)
{
    QByteArray out;
    putVarint(out, entries.size());
    quint64 last = 0;
    for (const Entry &e : entries) {
        putVarint(out, e.tileId - last);
        last = e.tileId;
    }
    for (const Entry &e : entries)
        putVarint(out, e.runLength);
    for (const Entry &e : entries)
        putVarint(out, e.length);
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const Entry &e = entries[i];
        if (i > 0 && e.offset == entries[i - 1].offset + entries[i - 1].length)
            putVarint(out, 0);
        else
            putVarint(out, e.offset + 1);
    }
    return out;
}

Directories buildDirectories(const QList<Entry> &entries, quint8 compression)
{
    auto pack = [compression](const QList<Entry> &e) {
        const QByteArray raw = serializeDirectory(e);
        return compression == Gzip ? gzip(raw) : raw;
    };
    Directories d;
    d.root = pack(entries);
    if (d.root.size() <= RootSpace - HeaderSize)
        return d;
    // as go-pmtiles does: leaves of 4096 entries, more until the root (one entry per leaf) fits
    for (qsizetype leafSize = 4096;; leafSize *= 2) {
        QList<Entry> root;
        d.leaves.clear();
        for (qsizetype i = 0; i < entries.size(); i += leafSize) {
            const QByteArray leaf = pack(entries.mid(i, leafSize));
            root.append({entries[i].tileId, quint64(d.leaves.size()), quint32(leaf.size()), 0});
            d.leaves.append(leaf);
        }
        d.root = pack(root);
        if (d.root.size() <= RootSpace - HeaderSize)
            return d;
    }
}

quint64 tileId(int z, quint32 x, quint32 y)
{
    const quint64 n = quint64(1) << z;
    quint64 tx = x, ty = y, d = 0;
    for (quint64 s = n / 2; s > 0; s /= 2) {
        const quint64 rx = (tx & s) ? 1 : 0;
        const quint64 ry = (ty & s) ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        rotate(n, tx, ty, rx, ry);
    }
    return tilesBefore(z) + d;
}

Tile tileFromId(quint64 id)
{
    int z = 0;
    while (z < 31 && tilesBefore(z + 1) <= id)
        ++z;
    const quint64 n = quint64(1) << z;
    quint64 t = id - tilesBefore(z), x = 0, y = 0;
    for (quint64 s = 1; s < n; s *= 2) {
        const quint64 rx = 1 & (t / 2);
        const quint64 ry = 1 & (t ^ rx);
        rotate(s, x, y, rx, ry);
        x += s * rx;
        y += s * ry;
        t /= 4;
    }
    return {z, quint32(x), quint32(y)};
}

Archive::Archive(const QString &path)
    : m_path(path)
{
    const QByteArray start = read(0, RootSpace);
    const auto h = parseHeader(start);
    if (!h)
        return;
    m_header = *h;
    const auto root = directory(m_header.rootOffset, m_header.rootLength);
    if (!root)
        return;
    m_root = *root;
    m_valid = true;
}

QByteArray Archive::read(quint64 offset, quint64 length)
{
    QFile f(m_path);
    if (length > (64u << 20) || !f.open(QIODevice::ReadOnly) || !f.seek(qint64(offset)))
        return {};
    return f.read(qint64(length));
}

std::optional<QList<Entry>> Archive::directory(quint64 offset, quint64 length)
{
    const auto raw = decompress(read(offset, length), m_header.internalCompression, DirectoryLimit);
    return raw ? parseDirectory(*raw) : std::nullopt;
}

QByteArray Archive::tile(int z, quint32 x, quint32 y)
{
    if (!m_valid || z < m_header.minZoom || z > m_header.maxZoom)
        return {};
    const quint64 id = tileId(z, x, y);
    QList<Entry> dir = m_root;
    for (int depth = 0; depth < 4; ++depth) {
        // the last entry starting at or before the id
        auto it = std::upper_bound(dir.cbegin(), dir.cend(), id, [](quint64 v, const Entry &e) {
            return v < e.tileId;
        });
        if (it == dir.cbegin())
            return {};
        const Entry e = *(it - 1);
        if (e.runLength > 0) {
            if (id >= e.tileId + e.runLength)
                return {};
            const auto data = decompress(read(m_header.dataOffset + e.offset, e.length), m_header.tileCompression, TileLimit);
            return data ? *data : QByteArray();
        }
        const quint64 at = m_header.leavesOffset + e.offset;
        if (!m_leaves.contains(at)) {
            const auto leaf = directory(at, e.length);
            if (!leaf)
                return {};
            m_leaves.insert(at, *leaf);
        }
        dir = m_leaves.value(at);
    }
    return {};
}

QByteArray gzip(const QByteArray &data)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray out(qsizetype(deflateBound(&zs, uLong(data.size()))) + 32, Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    zs.avail_in = uInt(data.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    out.resize(qsizetype(zs.total_out));
    deflateEnd(&zs);
    return rc == Z_STREAM_END ? out : QByteArray();
}

std::optional<QByteArray> gunzip(const QByteArray &data, qsizetype limit)
{
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK)  // gzip or zlib, by its header
        return std::nullopt;
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    zs.avail_in = uInt(data.size());
    QByteArray out;
    char buffer[65536];
    int rc = Z_OK;
    while (rc == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef *>(buffer);
        zs.avail_out = sizeof buffer;
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END)
            break;
        out.append(buffer, qsizetype(sizeof buffer - zs.avail_out));
        if (out.size() > limit) {  // a directory is a few MB at most: anything bigger is not one
            rc = Z_MEM_ERROR;
            break;
        }
        if (rc == Z_OK && zs.avail_in == 0 && zs.avail_out != 0)
            break;  // truncated
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return std::nullopt;
    return out;
}

std::optional<QByteArray> decompress(const QByteArray &data, quint8 compression, qsizetype limit)
{
    if (compression == NoCompression)
        return data.size() <= limit ? std::optional<QByteArray>(data) : std::nullopt;
    if (compression == Gzip)
        return gunzip(data, limit);
    return std::nullopt;  // Protomaps uses gzip; brotli and zstd are not read here
}
}
