// SPDX-License-Identifier: GPL-3.0-or-later
#include "zimarchive.h"

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

QString ZimArchive::verify(const QString &path, qint64 expectedSize)
{
    const QFileInfo file(path);
    if (!file.isFile())
        return QStringLiteral("the file is missing");
    if (QFileInfo::exists(path + QStringLiteral(".aria2")))
        return QStringLiteral("the download did not finish (aria2 left its control file behind)");
    // The catalog rounds sizes up to the next KiB, so a complete file is up to 1023 bytes smaller than its figure
    if (expectedSize > 0 && !(file.size() > expectedSize - 1024 && file.size() <= expectedSize))
        return QStringLiteral("the file is %1 bytes but the catalog says %2").arg(file.size()).arg(expectedSize);
    try {
        const zim::Archive zim(path.toStdString());
        if (!zim.check())
            return QStringLiteral("the checksum stored in the ZIM does not match its contents");
    } catch (const std::exception &e) {
        return QStringLiteral("it is not a valid ZIM file (%1)").arg(QString::fromUtf8(e.what()));
    }
    return {};
}

bool ZimArchive::isMwoffliner(const QString &scraper)
{
    return scraper.startsWith(QLatin1String("mwoffliner"), Qt::CaseInsensitive);
}
