// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <optional>

// What the library home and the library manager show about a ZIM, read from its metadata.
struct ZimInfo {
    QString path;
    QString name; // "wikipedia_en_chemistry"
    QString title; // "Chemistry by Wikipedia"
    QString description;
    QString language; // "eng"
    QString creator;
    QString publisher;
    QString date; // "2026-10-15"
    QString flavour; // "mini", "nopic", "maxi"
    QString scraper; // "mwoffliner 2.1.1"
    QString uuid;
    quint32 articleCount = 0; // front articles, redirects included
    bool hasFulltextIndex = false; // the ZIM's own Xapian index, which search and answers rely on
    QByteArray illustration; // 48x48 PNG, empty if the ZIM has none
};

namespace ZimArchive
{
// Reads a ZIM's metadata. Never throws: a missing or broken file returns nothing and fills `error`.
std::optional<ZimInfo> readInfo(const QString &path, QString *error = nullptr);

// Is the file at `path` a complete, intact ZIM? Returns an empty string if so, otherwise what is wrong.
// `expectedSize` is the catalog's figure (0 = unknown). Recomputes the ZIM's internal checksum, so it reads the whole
// file: minutes for a big library. Run it off the UI thread. Setting `stop` ends it within one read and returns Stopped:
// libzim's own check() cannot be interrupted, and the app could not exit until it finished.
QString verify(const QString &path, qint64 expectedSize, const std::atomic<bool> *stop = nullptr);
inline const QString Stopped = QStringLiteral("stopped");

// Is the ZIM a Wikipedia-style (mwoffliner) one, the only kind KAthenaeum can search and answer from?
bool isMwoffliner(const QString &scraper);
}
