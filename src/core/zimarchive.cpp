// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "zimarchive.h"

#include <QCryptographicHash>
#include <QFile>

#include <QFileInfo>

#include <zim/archive.h>
#include <zim/error.h>
#include <zim/item.h>

namespace
{
// Missing metadata is normal (older ZIMs lack some keys), so it reads as empty rather than failing.
QString metadata(const zim::Archive &zim, const char *key)
{
    try {
        return QString::fromStdString(zim.getMetadata(key));
    } catch (const zim::EntryNotFound &) {
        return {};
    }
}
}

std::optional<ZimInfo> ZimArchive::readInfo(const QString &path, QString *error)
{
    try {
        const zim::Archive zim(path.toStdString());
        ZimInfo info;
        info.path = path;
        info.name = metadata(zim, "Name");
        info.title = metadata(zim, "Title");
        info.description = metadata(zim, "Description");
        info.language = metadata(zim, "Language");
        info.creator = metadata(zim, "Creator");
        info.publisher = metadata(zim, "Publisher");
        info.date = metadata(zim, "Date");
        info.flavour = metadata(zim, "Flavour");
        info.scraper = metadata(zim, "Scraper");
        info.uuid = QString::fromStdString(std::string(zim.getUuid()));
        info.articleCount = zim.getArticleCount();
        info.hasFulltextIndex = zim.hasFulltextIndex();
        if (zim.hasIllustration(48)) {
            const zim::Blob blob = zim.getIllustrationItem(48).getData();
            info.illustration = QByteArray(blob.data(), qsizetype(blob.size()));
        }
        return info;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return std::nullopt;
    }
}

QString ZimArchive::verify(const QString &path, qint64 expectedSize, const std::atomic<bool> *stop)
{
    const QFileInfo file(path);
    if (!file.isFile())
        return QStringLiteral("the file is missing");
    if (QFileInfo::exists(path + QStringLiteral(".aria2")))
        return QStringLiteral("the download did not finish (aria2 left its control file behind)");
    // The catalog rounds sizes up to the next KiB, so a complete file is up to 1023 bytes smaller than its figure
    if (expectedSize > 0 && !(file.size() > expectedSize - 1024 && file.size() <= expectedSize))
        return QStringLiteral("the file is %1 bytes but the catalog says %2").arg(file.size()).arg(expectedSize);
    QString stored;
    try {
        const zim::Archive zim(path.toStdString());
        stored = QString::fromStdString(zim.getChecksum());
    } catch (const std::exception &e) {
        return QStringLiteral("it is not a valid ZIM file (%1)").arg(QString::fromUtf8(e.what()));
    }
    if (stored.isEmpty())
        return QStringLiteral("the ZIM has no checksum to check it against");
    // What libzim's check() does: the MD5 of everything but the last 16 bytes, which hold it. Read here in pieces so it
    // can be stopped.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 16)
        return QStringLiteral("the file cannot be read");
    QCryptographicHash md5(QCryptographicHash::Md5);
    qint64 left = f.size() - 16;
    QByteArray chunk(4 << 20, Qt::Uninitialized);
    while (left > 0) {
        if (stop && stop->load())
            return Stopped;
        const qint64 n = f.read(chunk.data(), qMin<qint64>(chunk.size(), left));
        if (n <= 0)
            return QStringLiteral("the file could not be read to the end");
        md5.addData(QByteArrayView(chunk.constData(), n));
        left -= n;
    }
    if (QString::fromLatin1(md5.result().toHex()) != stored.toLower())
        return QStringLiteral("the checksum stored in the ZIM does not match its contents");
    return {};
}

bool ZimArchive::isMwoffliner(const QString &scraper)
{
    return scraper.startsWith(QLatin1String("mwoffliner"), Qt::CaseInsensitive);
}
