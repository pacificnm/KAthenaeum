// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarybrowser.h"

#include "core/settings.h"

#include "core/citations.h"
#include "core/dictionary.h"

#include "articlestore.h"
#include "core/history.h"
#include "core/library.h"
#include "core/zimarchive.h"
#include "core/zimreader.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
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
            {QStringLiteral("available"), r->fileExists()},
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
    out.insert(QStringLiteral("hasCoordinates"), article->rendered.hasCoordinates);
    out.insert(QStringLiteral("lat"), article->rendered.lat);
    out.insert(QStringLiteral("lon"), article->rendered.lon);
    History::record(libraryId, article->path, article->rendered.title);
    return out;
}

bool LibraryBrowser::fullWidth() const
{
    return AppSettings::articleFullWidth();
}

void LibraryBrowser::setFullWidth(bool full)
{
    if (full == fullWidth())
        return;
    AppSettings::setArticleFullWidth(full);
    Q_EMIT fullWidthChanged();
}

QString LibraryBrowser::citationStyle() const
{
    return Citations::name(Citations::style(AppSettings::citationStyle()));
}

void LibraryBrowser::setCitationStyle(const QString &style)
{
    if (style == citationStyle() || !Citations::styleNames().contains(style))
        return;
    AppSettings::setCitationStyle(style);
    Q_EMIT citationStyleChanged();
}

QVariantList LibraryBrowser::citationStyles() const
{
    QVariantList out;
    for (const auto styles = Citations::styleNames(); const QString &name : styles)
        out.append(QVariantMap{{QStringLiteral("style"), name}, {QStringLiteral("label"), Citations::label(Citations::style(name))}});
    return out;
}

QVariantList LibraryBrowser::cite(qint64 libraryId, const QString &path, const QString &title) const
{
    const auto library = Library::byId(libraryId);
    if (!library)
        return {};
    const Citations::Source source = Citations::source(*library, path, title);
    const QDate today = QDate::currentDate();
    QStringList order = Citations::styleNames();
    order.removeAll(citationStyle());
    order.prepend(citationStyle());
    QVariantList out;
    for (const QString &name : std::as_const(order)) {
        const Citations::Style style = Citations::style(name);
        out.append(QVariantMap{{QStringLiteral("style"), name},
                               {QStringLiteral("label"), Citations::label(style)},
                               {QStringLiteral("text"), Citations::format(style, source, today)},
                               {QStringLiteral("html"), Citations::html(style, source, today)}});
    }
    return out;
}

void LibraryBrowser::copyCitation(const QString &text, const QString &html) const
{
    auto *data = new QMimeData;
    data->setText(text);
    if (!html.isEmpty())
        data->setHtml(html);
    QGuiApplication::clipboard()->setMimeData(data);
}

bool LibraryBrowser::hasDictionary() const
{
    const QList<LibraryRecord> libraries = Library::all();
    return std::any_of(libraries.cbegin(), libraries.cend(), [](const LibraryRecord &l) {
        return Dictionary::isDictionary(l) && l.fileExists();
    });
}

QVariantMap LibraryBrowser::define(const QString &word) const
{
    const QStringList candidates = Dictionary::candidates(word);
    QVariantMap out{{QStringLiteral("found"), false}, {QStringLiteral("word"), candidates.value(0, word.simplified())}};
    // the biggest dictionary first: the full Wiktionary knows more words than the Simple English one
    QList<LibraryRecord> dictionaries;
    for (const auto rows = Library::all(); const LibraryRecord &l : rows)
        if (Dictionary::isDictionary(l) && l.fileExists())
            dictionaries << l;
    std::sort(dictionaries.begin(), dictionaries.end(), [](const LibraryRecord &a, const LibraryRecord &b) {
        return a.sizeBytes > b.sizeBytes;
    });
    out.insert(QStringLiteral("hasDictionary"), !dictionaries.isEmpty());
    for (const QString &candidate : candidates) {
        QString path = candidate;
        path.replace(QLatin1Char(' '), QLatin1Char('_'));
        for (const LibraryRecord &dictionary : std::as_const(dictionaries)) {
            const auto content = ZimReader::content(dictionary.id, path);
            if (!content || !content->mimetype.startsWith(QLatin1String("text/html")))
                continue;
            auto entry = Dictionary::parse(content->data);
            if (!entry)
                continue;
            // "molecules": the plural of molecule, and what a molecule is
            QString formOf;
            const QString base = Dictionary::baseForm(*entry);
            if (!base.isEmpty() && base.compare(candidate, Qt::CaseInsensitive) != 0) {
                QString basePath = base;
                basePath.replace(QLatin1Char(' '), QLatin1Char('_'));
                if (const auto baseContent = ZimReader::content(dictionary.id, basePath)) {
                    if (auto baseEntry = Dictionary::parse(baseContent->data)) {
                        formOf = entry->parts.first().senses.first().definition;
                        entry = baseEntry;
                    }
                }
            }
            QVariantList parts;
            for (const Dictionary::PartOfSpeech &p : std::as_const(entry->parts)) {
                QVariantList senses;
                for (const Dictionary::Sense &sense : p.senses)
                    senses.append(QVariantMap{{QStringLiteral("definition"), sense.definition}, {QStringLiteral("example"), sense.example}});
                parts.append(QVariantMap{{QStringLiteral("name"), p.name}, {QStringLiteral("senses"), senses}});
            }
            return {{QStringLiteral("found"), true},
                    {QStringLiteral("word"), formOf.isEmpty() ? candidate : base},
                    {QStringLiteral("asked"), candidate},
                    {QStringLiteral("formOf"), formOf},
                    {QStringLiteral("pronunciation"), entry->pronunciation},
                    {QStringLiteral("parts"), parts},
                    {QStringLiteral("libraryId"), dictionary.id},
                    {QStringLiteral("path"), formOf.isEmpty() ? content->path : base},
                    {QStringLiteral("library"), dictionary.title}};
        }
    }
    return out;
}
