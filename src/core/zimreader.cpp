// SPDX-License-Identifier: GPL-3.0-or-later
#include "zimreader.h"

#include "articlerenderer.h"
#include "library.h"
#include "zimarchives_p.h"

#include <QCoreApplication>
#include <QHash>
#include <QGlobalStatic>
#include <QMutex>
#include <QRegularExpression>
#include <QThread>

#include <memory>
#include <zim/archive.h>
#include <zim/entry.h>
#include <zim/error.h>
#include <zim/item.h>
#include <zim/suggestion.h>

namespace
{
using Archives = QHash<qint64, std::shared_ptr<zim::Archive>>;
Q_GLOBAL_STATIC(QMutex, archivesLock)
Q_GLOBAL_STATIC(Archives, archives)
}

std::shared_ptr<zim::Archive> ZimArchives::opened(qint64 libraryId)
{
    QMutexLocker lock(archivesLock());
    return archives->value(libraryId);
}

std::shared_ptr<zim::Archive> ZimArchives::get(qint64 libraryId)
{
    if (auto a = opened(libraryId))
        return a;
    // Opening reads the libraries table, and the database connection belongs to the main thread: a worker gets only
    // what the main thread opened (ZimSearch::prepare). Workers reached the database this way (the review, 2026-10-19).
    if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread())
        return nullptr;
    const auto lib = Library::byId(libraryId);
    if (!lib || !lib->fileExists())
        return nullptr;
    try {
        auto a = std::make_shared<zim::Archive>(lib->path.toStdString());
        QMutexLocker lock(archivesLock());
        archives->insert(libraryId, a);
        return a;
    } catch (const std::exception &) {
        return nullptr;
    }
}

void ZimArchives::close(qint64 libraryId)
{
    QMutexLocker lock(archivesLock());
    archives->remove(libraryId);  // a search takes its own references when it starts (ZimSearch candidates) and keeps them
}

namespace
{
std::shared_ptr<zim::Archive> archive(qint64 libraryId)
{
    return ZimArchives::get(libraryId);
}

QString str(const std::string &s)
{
    return QString::fromStdString(s);
}

bool isMainPage(const zim::Archive &z, const zim::Entry &e)
{
    try {
        return z.hasMainEntry() && z.getMainEntry().getItem(true).getPath() == e.getPath();
    } catch (const std::exception &) {
        return false;
    }
}

bool isArticle(const zim::Archive &z, const zim::Entry &e)
{
    if (e.isRedirect() || str(e.getTitle()).endsWith(QLatin1String("(disambiguation)")))
        return false;
    // the ZIM's own front page (on Wikipedia selections, a custom alphabetical list of titles), not an article
    if (isMainPage(z, e))
        return false;
    // mwoffliner's HTML redirect pages (a few hundred bytes forwarding to a section of another article) are not
    // articles either: the article they lead to is listed under its own title. Only small pages need a look.
    const zim::Item item = e.getItem();
    if (item.getSize() > 4096 || !str(item.getMimetype()).startsWith(QLatin1String("text/html")))
        return true;
    const zim::Blob blob = item.getData();
    return ZimReader::htmlRedirectTarget(QByteArray(blob.data(), qsizetype(blob.size())), str(e.getPath())).isEmpty();
}

// The first title-order index whose title is >= key, comparing bytes as libzim orders them
quint32 lowerBound(const zim::Archive &z, const std::string &key)
{
    quint32 lo = 0, hi = z.getArticleCount();
    while (lo < hi) {
        const quint32 mid = lo + (hi - lo) / 2;
        if (z.getEntryByTitle(mid).getTitle() < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// A letter's titles as ranges of title-order indexes, in display order
QList<std::pair<quint32, quint32>> letterRanges(const zim::Archive &z, const QString &letter)
{
    const quint32 n = z.getArticleCount();
    auto bound = [&](char c) {
        return lowerBound(z, std::string(1, c));
    };
    if (letter == QLatin1String("#"))
        return {{0, bound('A')}, {bound('['), bound('a')}, {bound('{'), n}};
    if (letter.size() != 1 || !letter[0].isLetter() || letter[0].unicode() > 127)
        return {};
    const char upper = char(letter[0].toUpper().unicode());
    const char lower = char(letter[0].toLower().unicode());
    return {{bound(upper), bound(char(upper + 1))}, {bound(lower), bound(char(lower + 1))}};
}
}

QString ZimReader::htmlRedirectTarget(const QByteArray &html, const QString &path)
{
    if (html.size() > 4096)  // these pages are a few hundred bytes; a real article is never one
        return {};
    static const QRegularExpression refresh(QStringLiteral("http-equiv\\s*=\\s*[\"']refresh[\"'][^>]*content\\s*=\\s*\"\\s*\\d+\\s*;\\s*url\\s*=\\s*'?([^'\"]+)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const auto m = refresh.match(QString::fromUtf8(html));
    if (!m.hasMatch())
        return {};
    const QString href = m.captured(1).trimmed();
    const int hash = href.indexOf(QLatin1Char('#'));
    const QString target = ArticleRenderer::resolve(path, href);
    if (target.isEmpty())
        return {};
    return hash >= 0 ? target + href.mid(hash) : target;
}

std::optional<ZimContent> ZimReader::content(qint64 libraryId, const QString &path)
{
    const auto z = archive(libraryId);
    if (!z)
        return std::nullopt;
    try {
        QString current = path, fragment;
        for (int hop = 0; hop < 4; ++hop) {
            const zim::Item item = z->getEntryByPath(current.toStdString()).getItem(true);  // follows the ZIM's redirects
            if (tooLarge(qint64(item.getSize()), str(item.getMimetype()).startsWith(QLatin1String("text/html")))) {
                qWarning("Not reading %s: %lld bytes", qUtf8Printable(current), qint64(item.getSize()));
                return std::nullopt;
            }
            const zim::Blob blob = item.getData();
            ZimContent c{str(item.getPath()), fragment, str(item.getTitle()), str(item.getMimetype()), QByteArray(blob.data(), qsizetype(blob.size()))};
            const QString next = c.mimetype.startsWith(QLatin1String("text/html")) ? htmlRedirectTarget(c.data, c.path) : QString();
            if (next.isEmpty())
                return c;
            const int hash = next.indexOf(QLatin1Char('#'));
            current = hash >= 0 ? next.left(hash) : next;
            fragment = hash >= 0 ? next.mid(hash + 1) : QString();
        }
    } catch (const std::exception &) {
    }
    return std::nullopt;  // missing, or redirects going round in circles
}

bool ZimReader::hasEntry(qint64 libraryId, const QString &path)
{
    const auto z = archive(libraryId);
    try {
        return z && z->hasEntryByPath(path.toStdString());
    } catch (const std::exception &) {
        return false;
    }
}

void ZimReader::close(qint64 libraryId)
{
    ZimArchives::close(libraryId);
}

namespace
{
// The titles starting with `prefix`: from the prefix itself up to the first title past every continuation of it
QList<std::pair<quint32, quint32>> prefixRange(const zim::Archive &z, const std::string &prefix)
{
    if (prefix.empty())
        return {{0, z.getArticleCount()}};
    std::string after = prefix;
    while (!after.empty() && static_cast<unsigned char>(after.back()) == 0xff)
        after.pop_back();
    if (after.empty())
        return {{lowerBound(z, prefix), z.getArticleCount()}};
    after.back() = char(static_cast<unsigned char>(after.back()) + 1);
    return {{lowerBound(z, prefix), lowerBound(z, after)}};
}

// `from` counts positions across the ranges, redirects included, so a page is found without scanning everything before it
ZimReader::TitlePage pageOf(const zim::Archive &z, const QList<std::pair<quint32, quint32>> &ranges, int from, int count)
{
    ZimReader::TitlePage page;
    int base = 0;
    for (const auto &[begin, end] : ranges) {
        const int size = int(end - begin);
        for (int pos = std::max(from, base); pos < base + size; ++pos) {
            if (page.entries.size() == count) {
                page.next = pos;
                return page;
            }
            const zim::Entry e = z.getEntryByTitle(begin + quint32(pos - base));
            if (isArticle(z, e))
                page.entries.append({str(e.getPath()), str(e.getTitle())});
        }
        base += size;
    }
    return page;
}
}

ZimReader::TitlePage ZimReader::titlesWithPrefix(qint64 libraryId, const QString &prefix, int from, int count)
{
    const auto z = archive(libraryId);
    if (!z || from < 0)
        return {};
    try {
        return pageOf(*z, prefixRange(*z, prefix.toStdString()), from, count);
    } catch (const std::exception &) {
        return {};
    }
}

ZimReader::TitlePage ZimReader::titles(qint64 libraryId, const QString &letter, int from, int count)
{
    const auto z = archive(libraryId);
    if (!z || from < 0)
        return {};
    try {
        return pageOf(*z, letterRanges(*z, letter), from, count);
    } catch (const std::exception &) {
        return {};
    }
}

std::optional<ZimEntry> ZimReader::randomArticle(qint64 libraryId)
{
    const auto z = archive(libraryId);
    if (!z)
        return std::nullopt;
    try {
        // Redirects are drawn as often as they appear in the title list (most of it): draw again rather than follow
        // them, which would favour articles with many other names
        for (int attempt = 0; attempt < 200; ++attempt) {
            const zim::Entry e = z->getRandomEntry();
            if (isArticle(*z, e))
                return ZimEntry{str(e.getPath()), str(e.getTitle())};
        }
    } catch (const std::exception &) {
    }
    return std::nullopt;
}

QList<ZimEntry> ZimReader::withPrefix(qint64 libraryId, const QString &prefix, int limit)
{
    QList<ZimEntry> out;
    const auto z = archive(libraryId);
    if (!z)
        return out;
    try {
        for (const zim::Entry &e : z->findByTitle(prefix.toStdString())) {
            if (out.size() == limit)
                break;
            if (isArticle(*z, e))
                out.append({str(e.getPath()), str(e.getTitle())});
        }
    } catch (const std::exception &) {
    }
    return out;
}

QList<ZimEntry> ZimReader::suggest(qint64 libraryId, const QString &query, int limit)
{
    QList<ZimEntry> out;
    const auto z = archive(libraryId);
    if (!z || query.trimmed().isEmpty())
        return out;
    try {
        zim::SuggestionSearcher searcher(*z);
        const auto results = searcher.suggest(query.trimmed().toStdString()).getResults(0, limit);
        for (auto it = results.begin(); it != results.end(); ++it)
            out.append({str(it->getPath()), str(it->getTitle())});
    } catch (const std::exception &) {
    }
    return out;
}
