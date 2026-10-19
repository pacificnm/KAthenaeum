// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

struct ZimEntry {
    QString path;
    QString title;
};

struct ZimContent {
    QString path;      // after following redirects
    QString fragment;  // the section an HTML redirect page pointed at, if any
    QString title;
    QString mimetype;
    QByteArray data;
};

// Reading installed libraries. Archives are opened on first use and kept open; every libzim call (and exception) stays
// in here. Call from the UI thread.
namespace ZimReader
{
// Follows both kinds of redirect: the ZIM's own, and mwoffliner's tiny HTML pages that forward to a section of another
// article with <meta http-equiv="refresh" content="0;URL='./Cry_(song)#Version'">
std::optional<ZimContent> content(qint64 libraryId, const QString &path);

// The target of an HTML redirect page ("Cry_(song)#Version"), or empty. `path` is the page's own path.
QString htmlRedirectTarget(const QByteArray &html, const QString &path);
bool hasEntry(qint64 libraryId, const QString &path);
void close(qint64 libraryId);  // before a library's file is deleted

// A to Z, straight from the ZIM's title order (case-sensitive bytes). `letter` is "A".."Z" (upper- and lowercase
// titles, uppercase first) or "#" (everything else: digits, punctuation, accented letters). Redirects and
// "(disambiguation)" pages are skipped. Page with `from` = the previous page's `next`; `next` is -1 at the end.
struct TitlePage {
    QList<ZimEntry> entries;
    int next = -1;
};
TitlePage titles(qint64 libraryId, const QString &letter, int from, int count);

// The same paging over the titles that start with `prefix` ("List of "), for overview lists too long to show at once
TitlePage titlesWithPrefix(qint64 libraryId, const QString &prefix, int from, int count);

std::optional<ZimEntry> randomArticle(qint64 libraryId);

// Articles whose title starts with `prefix` ("Outline of "), redirects skipped
QList<ZimEntry> withPrefix(qint64 libraryId, const QString &prefix, int limit);

// Title suggestions while typing (the ZIM's own title index)
QList<ZimEntry> suggest(qint64 libraryId, const QString &query, int limit);
}
