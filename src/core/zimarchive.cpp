// SPDX-License-Identifier: GPL-3.0-or-later
#include "zimarchive.h"

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
        info.articleCount = zim.getArticleCount();
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
