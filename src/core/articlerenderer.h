// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>
#include <optional>

struct TocEntry {
    QString id;
    QString text;
    int level = 2;  // 2 or 3
};

struct RenderedArticle {
    QString title;
    QString html;  // the article body only: sanitised, links and images rewritten
    QList<TocEntry> toc;
    int highlighted = 0;  // how many of the highlights asked for were found in the text
    // where the article says its subject is (Wikipedia's coordinates, the first <span class="geo">), for "Show on Map"
    bool hasCoordinates = false;
    double lat = 0, lon = 0;
};

// A passage to mark in the article (a saved excerpt or a highlight), with the tooltip to show on it
struct Highlight {
    Highlight(const QString &text = {}, const QString &label = {}, const QString &cssClass = QStringLiteral("excerpt-hl"), const QString &anchor = {})
        : text(text)
        , label(label)
        , cssClass(cssClass)
        , anchor(anchor)
    {
    }
    QString text;
    QString label;
    QString cssClass;  // the mark's class
    QString anchor;    // an id for its first mark, to jump to it ("hl-12"); none if empty
};

// Turns a mwoffliner article page into safe, self-contained HTML (a port of Athenaeum's render.py, same parser family:
// libxml2 is what lxml uses). Never show ZIM HTML without going through this.
namespace ArticleRenderer
{
// Does the ZIM have an entry at this path? Links to anything else become plain, dimmed text; images are dropped.
using HasEntry = std::function<bool(const QString &path)>;

// `path` is the article's own path in the ZIM (links are relative to its directory). Internal links and images are
// rewritten to `urlPrefix` + percent-encoded path, e.g. "zim://lib3/". Returns nothing for input that is not an article.
// Each highlight's first occurrence is wrapped in <mark class="cssClass" title="label">, one per text node it spans
// (a passage may cross links and paragraphs). Matching ignores whitespace and case, so a passage saved with other spacing
// still matches (Athenaeum's rule); a passage no longer in the text is not shown.
std::optional<RenderedArticle> render(const QByteArray &raw, const QString &path, const QString &urlPrefix, const HasEntry &hasEntry,
                                      QString *error = nullptr, const QList<Highlight> &highlights = {});

// "45.52000; -122.68194" (the text of Wikipedia's <span class="geo">): a latitude and a longitude, if both are in range
std::optional<std::pair<double, double>> parseGeo(const QString &text);

// "../img/a.png" seen from "dir/Article" -> "img/a.png". A path that climbs out of the ZIM keeps its "../" (and matches
// nothing). Fragment and query are dropped, percent-escapes decoded. Not a URL parse: "Chemistry:_A_Journal" is a page,
// not a scheme.
QString resolve(const QString &basePath, const QString &href);

// The path as it goes into a zim:// URL
QString encodePath(const QString &path);
}
