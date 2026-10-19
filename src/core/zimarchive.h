// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// What the library home and the library manager show about a ZIM, read from its metadata.
struct ZimInfo {
    QString path;
    QString name;        // "wikipedia_en_chemistry"
    QString title;       // "Chemistry by Wikipedia"
    QString description;
    QString language;    // "eng"
    QString creator;
    QString publisher;
    QString date;        // "2026-10-15"
    QString flavour;     // "mini", "nopic", "maxi"
    QString scraper;     // "mwoffliner 2.1.1"
    quint32 articleCount = 0;  // front articles, redirects included (see CLAUDE.md, A-Z)
    QByteArray illustration;   // 48x48 PNG, empty if the ZIM has none
};

namespace ZimArchive
{
// Reads a ZIM's metadata. Never throws: a missing or broken file returns nothing and fills `error`.
std::optional<ZimInfo> readInfo(const QString &path, QString *error = nullptr);
}
