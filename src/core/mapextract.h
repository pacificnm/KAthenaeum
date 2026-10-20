// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "maptiling.h"
#include "pmtiles.h"

#include <QFile>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// Reads byte ranges of a PMTiles archive, asynchronously
class RangeReader : public QObject
{
    Q_OBJECT
public:
    using Done = std::function<void(const QByteArray &data, const QString &error)>;
    using QObject::QObject;
    virtual void read(quint64 offset, quint64 length, Done done) = 0;
    virtual void cancel() = 0;
};

// Ranges over HTTP (Protomaps' planet build): a few requests at a time, each retried a few times, each answer checked
// to be exactly the range asked for
class HttpRangeReader : public RangeReader
{
    Q_OBJECT
public:
    HttpRangeReader(const QUrl &url, QNetworkAccessManager *network, QObject *parent = nullptr);
    void read(quint64 offset, quint64 length, Done done) override;
    void cancel() override;

    static constexpr int Parallel = 4;

private:
    struct Request {
        quint64 offset, length;
        Done done;
        int attempt = 0;
    };
    void pump();
    void send(const std::shared_ptr<Request> &r);

    QUrl m_url;
    QNetworkAccessManager *m_network;
    QList<std::shared_ptr<Request>> m_waiting;
    QList<QPointer<QNetworkReply>> m_running;
    bool m_cancelled = false;
};

// Ranges of a local file (for tests)
class FileRangeReader : public RangeReader
{
    Q_OBJECT
public:
    explicit FileRangeReader(const QString &path, QObject *parent = nullptr);
    void read(quint64 offset, quint64 length, Done done) override;
    void cancel() override
    {
        m_cancelled = true;
    }
    int reads() const
    {
        return m_reads;
    }

private:
    QFile m_file;
    bool m_cancelled = false;
    int m_reads = 0;
};

// Cuts a region out of a PMTiles archive into a file of its own: reads the directories, finds the region's tiles, fetches
// their contents in large merged ranges and writes a new archive (tiles in the same clustered order, directories rebuilt).
class MapExtract : public QObject
{
    Q_OBJECT
public:
    enum Phase {
        Directories,
        Tiles,
        Writing
    };
    Q_ENUM(Phase)

    MapExtract(RangeReader *source, const MapTiling::MultiPolygon &shape, int maxZoom, const QString &outPath, QObject *parent = nullptr);
    ~MapExtract() override;

    void setSource(RangeReader *source)
    {
        m_source = source;
    }
    void start();
    void cancel();
    // Stop once the directories have been read: finished() then reports only how big the map would be
    void setEstimateOnly(bool estimate)
    {
        m_estimateOnly = estimate;
    }

    // after Directories: what the region's map will take
    qint64 totalBytes() const
    {
        return m_totalBytes;
    }
    qint64 tileCount() const
    {
        return m_tileCount;
    }

    // ranges closer than this are fetched as one (a little wasted data instead of many small requests)
    static constexpr quint64 MergeGap = 256 * 1024;
    static constexpr quint64 MaxRequest = 16 * 1024 * 1024;

Q_SIGNALS:
    void phaseChanged(MapExtract::Phase phase);
    void progress(qint64 done, qint64 total);
    void finished(const QString &error); // empty when the file is complete

private:
    struct Found {
        quint64 tileId;
        quint64 offset; // in the source's tile data
        quint32 length;
    };
    void gotRoot(const QByteArray &data);
    void gotWanted();
    // depth: of the directory the entries come from (the root is 0)
    void walk(const QList<PMTiles::Entry> &entries, quint64 end, int depth);
    void fetchLeaves();
    void fetchTiles();
    void write();
    void writeFile();
    void fail(const QString &error);
    bool wanted(quint64 from, quint64 to) const; // any wanted id in [from, to)

    QPointer<RangeReader> m_source;
    MapTiling::MultiPolygon m_shape;
    int m_maxZoom;
    QString m_outPath;
    PMTiles::Header m_header;
    QByteArray m_metadata;
    std::vector<quint64> m_wanted;
    QList<Found> m_found;
    struct Leaf {
        quint64 offset, length, end; // end: the first tile id after the leaf's range
        int depth;
    };
    // the PMTiles writers nest leaves one level deep; a deeper chain (a leaf pointing at itself) is a broken or hostile
    // file, and would be fetched forever (as PMTiles::Archive::tile, which stops at the same depth)
    static constexpr int MaxLeafDepth = 4;
    QList<Leaf> m_leaves;
    int m_pending = 0;
    QFile m_data;
    QHash<quint64, quint64> m_newOffset; // source offset -> offset in the new archive's tile data
    qint64 m_totalBytes = 0;
    qint64 m_tileCount = 0;
    qint64 m_doneBytes = 0;
    bool m_failed = false;
    bool m_cancelled = false;
    bool m_estimateOnly = false;
};
