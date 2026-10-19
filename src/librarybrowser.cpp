// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarybrowser.h"

#include "articlestore.h"
#include "core/history.h"
#include "core/library.h"
#include "core/zimarchive.h"
#include "core/zimreader.h"

#include <QQmlEngine>

#include <KLocalizedString>

namespace
{
QVariantList toList(const QList<ZimEntry> &entries)
{
    QVariantList out;
    for (const ZimEntry &e : entries)
        out.append(QVariantMap{{QStringLiteral("path"), e.path}, {QStringLiteral("title"), e.title}});
    return out;
}
}

LibraryBrowser::LibraryBrowser(QObject *parent)
    : QObject(parent)
{
}

LibraryBrowser *LibraryBrowser::create(QQmlEngine *engine, QJSEngine *)
{
    return new LibraryBrowser(engine);
}

QVariantMap LibraryBrowser::libraryInfo(qint64 libraryId) const
{
    const auto r = Library::byId(libraryId);
    if (!r)
        return {};
    const QByteArray png = Library::illustration(libraryId);
    return {{QStringLiteral("libraryId"), r->id},
            {QStringLiteral("title"), r->title},
            {QStringLiteral("description"), r->description},
            {QStringLiteral("flavour"), r->flavour},
            {QStringLiteral("date"), r->date},
            {QStringLiteral("size"), double(r->sizeBytes)},
            {QStringLiteral("articleCount"), double(r->articleCount)},
            {QStringLiteral("hasFulltext"), r->hasFulltextIndex},
            {QStringLiteral("isWikipedia"), ZimArchive::isMwoffliner(r->scraper)},
            {QStringLiteral("missing"), !r->fileExists()},
            {QStringLiteral("icon"), png.isEmpty() ? QString() : QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64())}};
}

QVariantMap LibraryBrowser::titles(qint64 libraryId, const QString &letterOrPrefix, bool prefix, int from, int count) const
{
    const auto page = prefix ? ZimReader::titlesWithPrefix(libraryId, letterOrPrefix, from, count) : ZimReader::titles(libraryId, letterOrPrefix, from, count);
    return {{QStringLiteral("entries"), toList(page.entries)}, {QStringLiteral("next"), page.next}};
}

QVariantMap LibraryBrowser::randomArticle(qint64 libraryId) const
{
    const auto e = ZimReader::randomArticle(libraryId);
    if (!e)
        return {};
    return {{QStringLiteral("path"), e->path}, {QStringLiteral("title"), e->title}};
}

QVariantList LibraryBrowser::overviews(qint64 libraryId) const
{
    // Wikipedia's own curated overview pages, already in the ZIM as ordinary articles (see CLAUDE.md, hub articles)
    const QList<std::pair<QString, QString>> kinds = {
        {i18n("Outlines"), QStringLiteral("Outline of ")},   {i18n("Indexes"), QStringLiteral("Index of ")},
        {i18n("Glossaries"), QStringLiteral("Glossary of ")}, {i18n("Timelines"), QStringLiteral("Timeline of ")},
        {i18n("Lists"), QStringLiteral("List of ")},
    };
    constexpr int shown = 12;
    QVariantList out;
    for (const auto &[label, prefix] : kinds) {
        auto entries = ZimReader::withPrefix(libraryId, prefix, shown + 1);
        if (entries.isEmpty())
            continue;
        const bool more = entries.size() > shown;
        entries = entries.mid(0, shown);
        out.append(QVariantMap{{QStringLiteral("label"), label},
                               {QStringLiteral("prefix"), prefix},
                               {QStringLiteral("entries"), toList(entries)},
                               {QStringLiteral("more"), more}});
    }
    return out;
}

QVariantList LibraryBrowser::suggest(qint64 libraryId, const QString &text) const
{
    return toList(ZimReader::suggest(libraryId, text, 8));
}

QVariantList LibraryBrowser::recent(qint64 libraryId) const
{
    return toList(History::recent(libraryId, 12));
}

QUrl LibraryBrowser::articleUrl(qint64 libraryId, const QString &path) const
{
    return ArticleStore::url(libraryId, path);
}

bool LibraryBrowser::isArticleUrl(const QUrl &url) const
{
    qint64 id = 0;
    QString path;
    return ArticleStore::parse(url, &id, &path);
}

QVariantMap LibraryBrowser::articleInfo(const QUrl &url) const
{
    qint64 libraryId = 0;
    QString path;
    if (!ArticleStore::parse(url, &libraryId, &path))
        return {{QStringLiteral("found"), false}};
    const auto lib = Library::byId(libraryId);
    const auto article = ArticleStore::article(libraryId, path);
    QVariantMap out{{QStringLiteral("found"), bool(article)},
                    {QStringLiteral("libraryId"), libraryId},
                    {QStringLiteral("libraryTitle"), lib ? lib->title : QString()},
                    {QStringLiteral("path"), article ? article->path : path},
                    {QStringLiteral("title"), article ? article->rendered.title : path}};
    if (!article)
        return out;
    QVariantList toc;
    for (const TocEntry &t : article->rendered.toc)
        toc.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("text"), t.text}, {QStringLiteral("level"), t.level}});
    out.insert(QStringLiteral("toc"), toc);
    out.insert(QStringLiteral("excerpts"), article->rendered.highlighted);
    History::record(libraryId, article->path, article->rendered.title);
    return out;
}
