// SPDX-License-Identifier: GPL-3.0-or-later
#include "mapextract.h"

#include "mapregions.h"
#include "network.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>

// ---- reading ranges ----

HttpRangeReader::HttpRangeReader(const QUrl &url, QNetworkAccessManager *network, QObject *parent)
    : RangeReader(parent)
    , m_url(url)
    , m_network(network)
{
}

void HttpRangeReader::read(quint64 offset, quint64 length, Done done)
{
    m_waiting.append(std::make_shared<Request>(Request{offset, length, std::move(done)}));
    pump();
}

void HttpRangeReader::cancel()
{
    m_cancelled = true;
    m_waiting.clear();
    for (const QPointer<QNetworkReply> &r : std::as_const(m_running))
        if (r)
            r->abort();
}

void HttpRangeReader::pump()
{
    m_running.removeIf([](const QPointer<QNetworkReply> &r) {
        return !r;
    });
    while (!m_cancelled && m_running.size() < Parallel && !m_waiting.isEmpty())
        send(m_waiting.takeFirst());
}

void HttpRangeReader::send(const std::shared_ptr<Request> &r)
{
    QNetworkRequest request(m_url);
    request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(r->offset) + '-' + QByteArray::number(r->offset + r->length - 1));
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_network->get(Network::guarded(request));
    Network::allowRedirects(reply, &MapCatalog::isAllowedUrl);
    // a server that ignores the range and sends the whole planet (138 GB) is stopped, not buffered
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, length = r->length](qint64 received) {
        if (quint64(received) > length)
            reply->abort();
    });
    reply->setParent(this);
    m_running.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, r] {
        reply->deleteLater();
        if (m_cancelled)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        QByteArray data;
        if (reply->error() != QNetworkReply::NoError) {
            error = reply->errorString();
        } else if (status != 206) {
            error = tr("The map server did not send part of the file (HTTP %1)").arg(status);
        } else {
            data = reply->readAll();
            const QByteArray range = reply->rawHeader("Content-Range");  // "bytes 0-16383/138993773823"
            const QByteArray expected = QByteArray("bytes ") + QByteArray::number(r->offset) + '-';
            if (!range.startsWith(expected) || quint64(data.size()) != r->length)
                error = tr("The map server sent the wrong part of the file");
        }
        const bool retry = !error.isEmpty() && r->attempt < 3 && status != 404 && status != 416;
        if (retry) {
            ++r->attempt;
            QTimer::singleShot(1000 * r->attempt * r->attempt, this, [this, r] {
                if (!m_cancelled) {
                    m_waiting.prepend(r);
                    pump();
                }
            });
        } else {
            r->done(data, error);
        }
        pump();
    });
}

FileRangeReader::FileRangeReader(const QString &path, QObject *parent)
    : RangeReader(parent)
    , m_file(path)
{
    m_file.open(QIODevice::ReadOnly);
}

void FileRangeReader::read(quint64 offset, quint64 length, Done done)
{
    ++m_reads;
    QTimer::singleShot(0, this, [this, offset, length, done = std::move(done)] {
        if (m_cancelled)
            return;
        if (!m_file.seek(qint64(offset))) {
            done({}, QStringLiteral("seek failed"));
            return;
        }
        const QByteArray data = m_file.read(qint64(length));
        done(data, quint64(data.size()) == length ? QString() : QStringLiteral("short read"));
    });
}

// ---- extracting ----

MapExtract::MapExtract(RangeReader *source, const MapTiling::MultiPolygon &shape, int maxZoom, const QString &outPath, QObject *parent)
    : QObject(parent)
    , m_source(source)
    , m_shape(shape)
    , m_maxZoom(maxZoom)
    , m_outPath(outPath)
{
}

MapExtract::~MapExtract()
{
    if (m_data.isOpen()) {
        m_data.close();
        m_data.remove();
    }
}

void MapExtract::start()
{
    Q_EMIT phaseChanged(Directories);
    m_source->read(0, PMTiles::RootSpace, [this](const QByteArray &data, const QString &error) {
        if (!error.isEmpty())
            return fail(error);
        gotRoot(data);
    });
}

void MapExtract::cancel()
{
    m_cancelled = true;
    if (m_source)
        m_source->cancel();
}

void MapExtract::fail(const QString &error)
{
    if (m_failed || m_cancelled)
        return;
    m_failed = true;
    if (m_source)
        m_source->cancel();
    if (m_data.isOpen()) {
        m_data.close();
        m_data.remove();
    }
    Q_EMIT finished(error);
}

void MapExtract::gotRoot(const QByteArray &data)
{
    const auto header = PMTiles::parseHeader(data);
    if (!header)
        return fail(tr("The map file is not in the PMTiles format"));
    m_header = *header;
    if (m_header.rootOffset + m_header.rootLength > quint64(data.size()))
        return fail(tr("The map file's directory is not where it should be"));
    if (m_header.internalCompression != PMTiles::Gzip && m_header.internalCompression != PMTiles::NoCompression)
        return fail(tr("The map file is compressed in a way KAthenaeum cannot read"));
    m_maxZoom = std::min<int>(m_maxZoom, m_header.maxZoom);

    const QByteArray root = data.mid(qsizetype(m_header.rootOffset), qsizetype(m_header.rootLength));
    // the tiles the region covers: up to a few million at zoom 15, listed on a worker thread
    auto *watcher = new QFutureWatcher<std::vector<quint64>>(this);
    connect(watcher, &QFutureWatcher<std::vector<quint64>>::finished, this, [this, watcher, root] {
        watcher->deleteLater();
        if (m_cancelled)
            return;
        m_wanted = watcher->result();
        const auto decoded = PMTiles::decompress(root, m_header.internalCompression, PMTiles::DirectoryLimit);
        const auto entries = decoded ? PMTiles::parseDirectory(*decoded) : std::nullopt;
        if (!entries)
            return fail(tr("The map file's directory could not be read"));
        walk(*entries, UINT64_MAX, 0);
        fetchLeaves();
    });
    watcher->setFuture(QtConcurrent::run([shape = m_shape, minZoom = int(m_header.minZoom), maxZoom = m_maxZoom] {
        return MapTiling::tileIds(shape, minZoom, maxZoom);
    }));
}

bool MapExtract::wanted(quint64 from, quint64 to) const
{
    const auto it = std::lower_bound(m_wanted.begin(), m_wanted.end(), from);
    return it != m_wanted.end() && *it < to;
}

void MapExtract::walk(const QList<PMTiles::Entry> &entries, quint64 end, int depth)
{
    if (depth >= MaxLeafDepth)
        return fail(tr("The map file's directories are nested too deeply"));
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const PMTiles::Entry &e = entries[i];
        if (e.runLength == 0) {
            const quint64 leafEnd = i + 1 < entries.size() ? entries[i + 1].tileId : end;
            if (wanted(e.tileId, leafEnd))
                m_leaves.append({m_header.leavesOffset + e.offset, e.length, leafEnd, depth + 1});
            continue;
        }
        const quint64 runEnd = e.tileId + e.runLength;
        for (auto it = std::lower_bound(m_wanted.begin(), m_wanted.end(), e.tileId); it != m_wanted.end() && *it < runEnd; ++it)
            m_found.append({*it, e.offset, e.length});
    }
}

void MapExtract::fetchLeaves()
{
    if (m_failed || m_cancelled)
        return;
    if (m_leaves.isEmpty()) {
        if (m_pending == 0)
            fetchTiles();
        return;
    }
    // leaves next to each other in the file are fetched together
    QList<Leaf> leaves = std::exchange(m_leaves, {});
    std::sort(leaves.begin(), leaves.end(), [](const Leaf &a, const Leaf &b) {
        return a.offset < b.offset;
    });
    for (qsizetype i = 0; i < leaves.size();) {
        qsizetype j = i + 1;
        quint64 from = leaves[i].offset, to = leaves[i].offset + leaves[i].length;
        while (j < leaves.size() && leaves[j].offset <= to + MergeGap && leaves[j].offset + leaves[j].length - from <= MaxRequest) {
            to = std::max(to, leaves[j].offset + leaves[j].length);
            ++j;
        }
        const QList<Leaf> group = leaves.mid(i, j - i);
        ++m_pending;
        m_source->read(from, to - from, [this, group, from](const QByteArray &data, const QString &error) {
            --m_pending;
            if (!error.isEmpty())
                return fail(error);
            for (const Leaf &leaf : group) {
                const auto decoded = PMTiles::decompress(data.mid(qsizetype(leaf.offset - from), qsizetype(leaf.length)), m_header.internalCompression,
                                                         PMTiles::DirectoryLimit);
                const auto entries = decoded ? PMTiles::parseDirectory(*decoded) : std::nullopt;
                if (!entries)
                    return fail(tr("A directory of the map file could not be read"));
                walk(*entries, leaf.end, leaf.depth);  // a leaf may point to further leaves
                if (m_failed)
                    return;
            }
            fetchLeaves();
        });
        i = j;
    }
}

void MapExtract::fetchTiles()
{
    std::sort(m_found.begin(), m_found.end(), [](const Found &a, const Found &b) {
        return a.tileId < b.tileId;
    });
    // the contents to fetch, each once (the planet stores identical tiles, such as open sea, once)
    QList<std::pair<quint64, quint32>> contents;
    contents.reserve(m_found.size());
    for (const Found &f : std::as_const(m_found))
        contents.append({f.offset, f.length});
    std::sort(contents.begin(), contents.end());
    contents.erase(std::unique(contents.begin(), contents.end()), contents.end());
    quint64 at = 0;
    for (const auto &[offset, length] : std::as_const(contents)) {
        m_newOffset.insert(offset, at);
        at += length;
    }
    m_totalBytes = qint64(at);
    m_tileCount = m_found.size();
    if (m_found.isEmpty())
        return fail(tr("The map has no tiles for this region"));
    if (m_estimateOnly) {
        Q_EMIT finished({});
        return;
    }

    m_data.setFileName(m_outPath + QStringLiteral(".data"));
    if (!m_data.open(QIODevice::ReadWrite | QIODevice::Truncate))
        return fail(tr("Could not write %1: %2").arg(m_data.fileName(), m_data.errorString()));

    Q_EMIT phaseChanged(Tiles);
    Q_EMIT progress(0, m_totalBytes);
    for (qsizetype i = 0; i < contents.size();) {
        qsizetype j = i + 1;
        const quint64 from = contents[i].first;
        quint64 to = from + contents[i].second;
        while (j < contents.size() && contents[j].first <= to + MergeGap && contents[j].first + contents[j].second - from <= MaxRequest) {
            to = std::max(to, contents[j].first + contents[j].second);
            ++j;
        }
        const QList<std::pair<quint64, quint32>> group = contents.mid(i, j - i);
        ++m_pending;
        m_source->read(m_header.dataOffset + from, to - from, [this, group, from](const QByteArray &data, const QString &error) {
            --m_pending;
            if (m_failed || m_cancelled)
                return;
            if (!error.isEmpty())
                return fail(error);
            for (const auto &[offset, length] : group) {
                m_data.seek(qint64(m_newOffset.value(offset)));
                if (m_data.write(data.constData() + (offset - from), length) != qint64(length))
                    return fail(tr("Could not write %1: %2").arg(m_data.fileName(), m_data.errorString()));
                m_doneBytes += length;
            }
            Q_EMIT progress(m_doneBytes, m_totalBytes);
            if (m_pending == 0)
                write();
        });
        i = j;
    }
}

void MapExtract::write()
{
    Q_EMIT phaseChanged(Writing);
    // the metadata (attribution, layers) is copied as it is
    m_source->read(m_header.metadataOffset, m_header.metadataLength, [this](const QByteArray &meta, const QString &error) {
        if (!error.isEmpty())
            return fail(error);
        m_metadata = meta;
        writeFile();
    });
}

void MapExtract::writeFile()
{
    // the new directory: runs of consecutive tiles with one content
    QList<PMTiles::Entry> entries;
    for (const Found &f : std::as_const(m_found)) {
        const quint64 offset = m_newOffset.value(f.offset);
        if (!entries.isEmpty()) {
            PMTiles::Entry &last = entries.last();
            if (last.offset == offset && last.tileId + last.runLength == f.tileId) {
                ++last.runLength;
                continue;
            }
        }
        entries.append({f.tileId, offset, f.length, 1});
    }
    const PMTiles::Directories dirs = PMTiles::buildDirectories(entries, m_header.internalCompression);

    PMTiles::Header h = m_header;
    h.rootOffset = PMTiles::HeaderSize;
    h.rootLength = quint64(dirs.root.size());
    h.metadataOffset = h.rootOffset + h.rootLength;
    h.metadataLength = quint64(m_metadata.size());
    h.leavesOffset = h.metadataOffset + h.metadataLength;
    h.leavesLength = quint64(dirs.leaves.size());
    h.dataOffset = h.leavesOffset + h.leavesLength;
    h.dataLength = quint64(m_totalBytes);
    h.addressedTiles = quint64(m_found.size());
    h.tileEntries = quint64(entries.size());
    h.tileContents = quint64(m_newOffset.size());
    h.clustered = true;
    h.maxZoom = quint8(m_maxZoom);
    const QRectF b = MapTiling::bounds(m_shape);
    h.minLonE7 = qint32(b.left() * 1e7);
    h.minLatE7 = qint32(b.top() * 1e7);  // top() is the smallest y: the south edge
    h.maxLonE7 = qint32(b.right() * 1e7);
    h.maxLatE7 = qint32(b.bottom() * 1e7);
    h.centerLonE7 = qint32(b.center().x() * 1e7);
    h.centerLatE7 = qint32(b.center().y() * 1e7);
    h.centerZoom = quint8(std::clamp(int(std::log2(360.0 / std::max(b.width(), 1e-6))), int(h.minZoom), m_maxZoom));

    QSaveFile out(m_outPath);
    if (!out.open(QIODevice::WriteOnly))
        return fail(tr("Could not write %1: %2").arg(m_outPath, out.errorString()));
    out.write(PMTiles::serializeHeader(h));
    out.write(dirs.root);
    out.write(m_metadata);
    out.write(dirs.leaves);
    m_data.seek(0);
    QByteArray chunk;
    while (!(chunk = m_data.read(8 << 20)).isEmpty())
        out.write(chunk);
    if (!out.commit())
        return fail(tr("Could not write %1: %2").arg(m_outPath, out.errorString()));
    m_data.close();
    m_data.remove();
    Q_EMIT finished({});
}
