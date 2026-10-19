// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPalette>
#include <QString>
#include <QUrl>
#include <optional>

#include "core/articlerenderer.h"

// Rendered articles, shared by the zim:// scheme handler (which shows them) and LibraryBrowser (which gives the page
// its title and contents list), so an article is parsed once.
namespace ArticleStore
{
struct Article {
    qint64 libraryId = 0;
    QString path;  // after redirects
    RenderedArticle rendered;
};

// zim://lib3/Water  <->  (3, "Water")
QUrl url(qint64 libraryId, const QString &path, const QString &fragment = {});
bool parse(const QUrl &url, qint64 *libraryId, QString *path);

std::optional<Article> article(qint64 libraryId, const QString &path);
void clear();  // after a library is removed

// The page the web view shows: the article in a document styled from the desktop's colours
QByteArray page(const Article &article, const QPalette &palette);
QByteArray notFoundPage(qint64 libraryId, const QString &path, const QPalette &palette);
}
