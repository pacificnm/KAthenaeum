// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

struct LibraryRecord;

// Citing an article read in the app, in the styles schools ask for. A Wikimedia library (Wikipedia, Wiktionary,
// Wikiversity…) is cited as the website it is a copy of, with the article's address there and a word that it was read
// in an offline copy (Kiwix), dated by the copy; any other library as the collection it is.
namespace Citations
{
enum class Style {
    Mla,
    Apa,
    Chicago
};

QStringList styleNames(); // "mla", "apa", "chicago": the order they are offered in
Style style(const QString &name); // an unknown name is MLA
QString name(Style style);
QString label(Style style); // "MLA (9th edition)"
QString shortLabel(Style style); // "MLA": the style's own name, the same in every language
QString listHeading(Style style); // what the list is called: Works Cited, References, Bibliography

// What a citation is made from
struct Source {
    QString title; // the article's
    QString path; // in the ZIM
    QString site; // "Wikipedia", or the library's title
    QString publisher; // "Wikimedia Foundation", or the ZIM's creator
    QString url; // the article's address on the web, when it has one
    QDate copied; // when the offline copy was made (the ZIM's date)
};
// A Wikimedia library's site name and the article's address: from the ZIM's name ("wikipedia_en_all" ->
// https://en.wikipedia.org/wiki/<path>), which a mwoffliner ZIM always has
Source source(const LibraryRecord &library, const QString &path, const QString &title);

// One entry. `markdown`: the title of the website (or the work) in *italics*; else plain text.
QString format(Style style, const Source &source, const QDate &accessed, bool markdown = false);
// The sources in the style's order (alphabetical, by what each entry starts with), duplicates dropped
QList<Source> sortedSources(Style style, const QList<Source> &sources, const QDate &accessed);
// Entries in the style's order (alphabetical, by what each one starts with), duplicates dropped
QStringList sorted(Style style, const QList<Source> &sources, const QDate &accessed, bool markdown = false);
// The same entry as HTML (italics as <i>; everything else escaped), for pasting into a word processor
QString html(Style style, const Source &source, const QDate &accessed);
}
