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
};

// Turns a mwoffliner article page into safe, self-contained HTML (a port of Athenaeum's render.py, same parser family:
// libxml2 is what lxml uses). Never show ZIM HTML without going through this.
namespace ArticleRenderer
{
// Does the ZIM have an entry at this path? Links to anything else become plain, dimmed text; images are dropped.
using HasEntry = std::function<bool(const QString &path)>;

// `path` is the article's own path in the ZIM (links are relative to its directory). Internal links and images are
// rewritten to `urlPrefix` + percent-encoded path, e.g. "zim://lib3/". Returns nothing for input that is not an article.
std::optional<RenderedArticle> render(const QByteArray &raw, const QString &path, const QString &urlPrefix, const HasEntry &hasEntry,
                                      QString *error = nullptr);

// "../img/a.png" seen from "dir/Article" -> "img/a.png". A path that climbs out of the ZIM keeps its "../" (and matches
// nothing). Fragment and query are dropped, percent-escapes decoded. Not a URL parse: "Chemistry:_A_Journal" is a page,
// not a scheme.
QString resolve(const QString &basePath, const QString &href);

// The path as it goes into a zim:// URL
QString encodePath(const QString &path);
}
