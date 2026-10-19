// SPDX-License-Identifier: GPL-3.0-or-later
#include "zimsearch.h"

#include "extract.h"
#include "zimarchives_p.h"
#include "zimreader.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <zim/entry.h>
#include <zim/error.h>
#include <zim/item.h>
#include <zim/search.h>
#include <zim/suggestion.h>

namespace
{
const QSet<QString> &stopWords()
{
    static const QSet<QString> words = [] {
        const QString list = QStringLiteral(
            "a about above after again against all also am an and any are aren't as at be because been before being below "
            "between both but by can can't cannot could couldn't did didn't do does doesn't doing don't down during each few for from further "
            "had hadn't has hasn't have haven't having he her here hers him his how i if in into is isn't it its itself just me more most my "
            "no nor not of off on once only or other our ours out over own same she should shouldn't so some such than that the their theirs "
            "them then there these they this those through to too under until up very was wasn't we were weren't what when where which while "
            "who whom why will with won't would wouldn't you your yours yourself tell explain describe give mean means please work works working "
            "happen happens get gets many much long called known");
        QSet<QString> out;
        for (QString w : list.split(QLatin1Char(' '), Qt::SkipEmptyParts))
            out.insert(w.remove(QLatin1Char('\'')));  // matched after apostrophes are removed: don't -> dont
        return out;
    }();
    return words;
}

// lower case, "'s" dropped, apostrophes removed, then runs of [a-z0-9]
QStringList words(const QString &query)
{
    static const QRegularExpression possessive(QStringLiteral("['’]s\\b"));
    static const QRegularExpression word(QStringLiteral("[a-z0-9]+"));
    QString q = query.toLower();
    q.replace(possessive, QString());
    q.remove(QLatin1Char('\'')).remove(QChar(0x2019));
    QStringList out;
    for (auto it = word.globalMatch(q); it.hasNext();)
        out << it.next().captured(0);
    return out;
}

const QRegularExpression &lead()
{
    static const QRegularExpression re(QStringLiteral("^(?:(?:what|who|whom|where|when|why|how)\\s+(?:is|are|was|were|does|do|did)|define|explain|tell me about|about)\\s+(?:an?\\s+|the\\s+)?"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString stripEnd(const QString &q)
{
    QString s = q.trimmed();
    while (!s.isEmpty() && QStringLiteral("?!. ").contains(s.back()))
        s.chop(1);
    return s.trimmed();
}

QString capitalize(const QString &s)  // Python str.capitalize
{
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1).toLower();
}

QString titleCase(const QString &s)  // Python str.title, for the words we produce
{
    QStringList parts = s.split(QLatin1Char(' '));
    for (QString &p : parts)
        p = capitalize(p);
    return parts.join(QLatin1Char(' '));
}

bool isSpecialPage(const QString &path)
{
    static const QRegularExpression ns(QStringLiteral("^(?:Category|Template|Wikipedia|Portal|File|Help|Draft|Talk|User|Module|MediaWiki|Special):"));
    return ns.match(path).hasMatch();
}

QString str(const std::string &s)
{
    return QString::fromStdString(s);
}

std::optional<QString> pathOfTitle(const zim::Archive &z, const QString &title)
{
    try {
        return str(z.getEntryByTitle(title.toStdString()).getPath());
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

QStringList xapian(const zim::Archive &z, const QString &query, int n)
{
    QStringList out;
    try {
        zim::Searcher searcher(z);
        const auto results = searcher.search(zim::Query(query.toStdString())).getResults(0, n);
        for (auto it = results.begin(); it != results.end(); ++it)
            out << str(it.getPath());
    } catch (const std::exception &) {
        // a ZIM without a full-text index, or a query Xapian cannot parse, simply finds nothing
    }
    return out;
}

struct Resolved {
    QString path;
    QString title;
};

// Follows redirects (the ZIM's own and HTML forwarding pages), and keeps only real articles: "Hypertension" and its
// aliases are one article
std::optional<Resolved> resolve(const zim::Archive &z, const QString &path, bool wantDisambiguation)
{
    try {
        zim::Item item = z.getEntryByPath(path.toStdString()).getItem(true);
        for (int hop = 0; hop < 3 && item.getSize() <= 4096 && str(item.getMimetype()).startsWith(QLatin1String("text/html")); ++hop) {
            const zim::Blob blob = item.getData();
            QString next = ZimReader::htmlRedirectTarget(QByteArray(blob.data(), qsizetype(blob.size())), str(item.getPath()));
            if (next.isEmpty())
                break;
            if (const int hash = next.indexOf(QLatin1Char('#')); hash >= 0)
                next.truncate(hash);
            item = z.getEntryByPath(next.toStdString()).getItem(true);
        }
        const QString p = str(item.getPath()), t = str(item.getTitle());
        if (isSpecialPage(p) || (t.endsWith(QLatin1String("(disambiguation)")) && !wantDisambiguation))
            return std::nullopt;
        if (!str(item.getMimetype()).startsWith(QLatin1String("text/html")))
            return std::nullopt;
        return Resolved{p, t};
    } catch (const std::exception &) {
        return std::nullopt;  // a redirect loop, a broken target
    }
}

QList<ZimSearch::Found> libraryArticles(qint64 libraryId, const std::shared_ptr<zim::Archive> &archive, const QString &q, int limit)
{
    using namespace ZimSearch;
    if (!archive)
        return {};
    const zim::Archive &z = *archive;
    const QStringList ws = terms(q);
    QList<std::pair<QStringList, double>> lists;

    std::optional<QString> exact;
    QStringList named;  // paths whose title the query names
    for (const QString &candidate : titleCandidates(q)) {
        if ((exact = pathOfTitle(z, candidate))) {
            lists.append({{*exact}, TitleWeight});
            named << *exact;
            break;
        }
    }
    int hits = 0;
    for (const auto &[gram, weight] : ngramTitles(q)) {
        if (hits >= MaxNgramHits)
            break;
        QStringList variants{capitalize(gram)};  // "Boiling point"
        if (titleCase(gram) != variants.first())
            variants << titleCase(gram);  // "Albert Einstein"
        for (const QString &v : variants) {
            if (const auto p = pathOfTitle(z, v)) {
                lists.append({{*p}, weight});
                named << *p;
                ++hits;
                break;
            }
        }
    }
    // The title index: "ray charles" -> "Ray Charles", "Ray Charles discography". Not for a question: "why is the sky blue"
    // would be pulled towards articles titled "Sky blue" and away from the explanation.
    if (!ws.isEmpty() && !isQuestion(q)) {
        try {
            QStringList found;
            zim::SuggestionSearcher searcher(z);
            const auto results = searcher.suggest(ws.join(QLatin1Char(' ')).toStdString()).getResults(0, 5);
            for (auto it = results.begin(); it != results.end(); ++it)
                found << str(it->getPath());
            lists.append({found, TitlePrefix});
        } catch (const std::exception &) {
        }
    }
    if (!ws.isEmpty()) {
        // libzim's query parser has no OR (the word is silently dropped) and ANDs every word, so a natural question that no
        // single article matches word for word is widened by hand: every word, then every word but one, then each word
        // alone. An article matching more of the words is in more lists and ranks higher.
        lists.append({xapian(z, ws.join(QLatin1Char(' ')), PerLibrary), 1.0});
        if (ws.size() > 2)  // always blend in "every word but one": an extra word must not bury the article that has the rest
            for (int i = 0; i < ws.size(); ++i) {
                QStringList rest = ws;
                rest.removeAt(i);
                lists.append({xapian(z, rest.join(QLatin1Char(' ')), PerLibrary), LeaveOneOut});
            }
        QSet<QString> distinct;
        for (const auto &l : std::as_const(lists))
            for (const QString &p : l.first)
                distinct.insert(p);
        if (distinct.size() < AndMin && ws.size() > 1)
            for (const QString &w : ws)
                lists.append({xapian(z, w, PerLibrary), SingleWord});
    }

    const bool wantDisambiguation = q.contains(QLatin1String("disambiguation"), Qt::CaseInsensitive);
    QHash<QString, double> scores;
    QHash<QString, QString> titles;
    for (const auto &[paths, weight] : std::as_const(lists)) {
        for (int rank = 1; rank <= paths.size(); ++rank) {
            const auto r = resolve(z, paths.at(rank - 1), wantDisambiguation);
            if (!r)
                continue;
            scores[r->path] += weight / (RrfK + rank);
            titles[r->path] = r->title;
        }
    }

    QStringList best = scores.keys();
    std::sort(best.begin(), best.end(), [&](const QString &a, const QString &b) {
        return scores.value(a) != scores.value(b) ? scores.value(a) > scores.value(b) : a < b;
    });
    best = best.mid(0, limit);

    QString exactPath;
    if (exact)  // the exact title may be a redirect: the article it leads to is the title match
        if (const auto r = resolve(z, *exact, true))
            exactPath = r->path;
    QSet<QString> namedPaths;
    for (const QString &p : std::as_const(named))
        if (const auto r = resolve(z, p, true))
            namedPaths.insert(r->path);
    QList<Found> out;
    for (const QString &p : best)
        out.append({libraryId, p, titles.value(p), scores.value(p), p == exactPath, namedPaths.contains(p)});
    return out;
}
}

QStringList ZimSearch::terms(const QString &query)
{
    QStringList out;
    for (const QString &w : words(query)) {
        if (stopWords().contains(w) || out.contains(w) || (w.size() < 2 && !w.at(0).isDigit()))
            continue;
        out << w;
    }
    return out.mid(0, MaxTerms);
}

QStringList ZimSearch::titleCandidates(const QString &query)
{
    const QString base = stripEnd(query);
    QString core = base;
    core.remove(lead());
    core = core.trimmed();
    if (core.isEmpty())
        core = base;
    QStringList out;
    for (const QString &c : QStringList{base, core, core.left(1).toUpper() + core.mid(1), titleCase(core)})
        if (!c.isEmpty() && !out.contains(c))
            out << c;
    return out;
}

bool ZimSearch::isQuestion(const QString &query)
{
    const QString base = stripEnd(query);
    QString core = base;
    core.remove(lead());
    return core != base && !(core.size() > 0 && core.at(0).isUpper());
}

QList<std::pair<QString, double>> ZimSearch::ngramTitles(const QString &query)
{
    static const QHash<int, double> weight = {{1, 0.8}, {2, 1.0}, {3, 1.2}, {4, 1.2}};  // a longer exact title is more specific
    const QStringList w = words(query);
    QList<std::pair<QString, double>> out;
    for (int n = 4; n >= 1; --n) {
        for (int i = 0; i + n <= w.size(); ++i) {
            const QStringList gram = w.mid(i, n);
            if (stopWords().contains(gram.first()) || stopWords().contains(gram.last()) || (n == 1 && gram.first().size() < 3))
                continue;  // "of ethanol" is not a title; neither is "the"
            out.append({gram.join(QLatin1Char(' ')), weight.value(n)});
        }
    }
    return out;
}

QString ZimSearch::stem(const QString &word)
{
    QString w = word;
    if (w.size() > 3 && w.endsWith(QLatin1Char('s')))
        w.chop(1);
    return w.left(6);
}

double ZimSearch::overlap(const QStringList &stems, const QString &text)
{
    if (stems.isEmpty())
        return 0.0;
    QSet<QString> have;
    for (const QString &w : words(text))
        have.insert(stem(w));
    int n = 0;
    for (const QString &s : stems)
        n += have.contains(s);
    return double(n) / double(stems.size());
}

void ZimSearch::prepare(const QList<qint64> &libraryIds)
{
    for (qint64 id : libraryIds)
        ZimArchives::get(id);
}

namespace
{
// Do the query's words appear in the article's title or introduction? Prefix-tolerant, so "polar" meets "polarity"
double aboutness(const QStringList &stems, const QString &title, const QString &intro)
{
    if (stems.isEmpty())
        return 1.0;
    QSet<QString> have;
    for (const QString &w : words(title + QLatin1Char(' ') + intro))
        have.insert(ZimSearch::stem(w));
    int n = 0;
    for (const QString &s : stems) {
        bool found = have.contains(s);
        for (auto it = have.cbegin(); !found && it != have.cend(); ++it)
            found = s.size() >= 4 && it->size() >= 4 && (it->startsWith(s) || s.startsWith(*it));
        n += found;
    }
    return double(n) / double(stems.size());
}

QList<ZimSearch::Article> candidates(const QString &query, const QList<qint64> &libraryIds, int limit)
{
    // One reference to each archive for the whole search: removing a library meanwhile drops it from the cache, and
    // looking it up again later in the search found nothing and crashed on it (found by the review, 2026-10-19)
    QHash<qint64, std::shared_ptr<zim::Archive>> archives;
    for (qint64 id : libraryIds)
        if (auto a = ZimArchives::opened(id))
            archives.insert(id, std::move(a));
    QList<ZimSearch::Found> found;
    for (qint64 id : libraryIds)
        found += libraryArticles(id, archives.value(id), query, limit);
    QStringList stems;
    for (const QString &t : ZimSearch::terms(query))
        stems << ZimSearch::stem(t);

    QList<ZimSearch::Article> out;
    for (ZimSearch::Found &f : found) {
        const auto archive = archives.value(f.libraryId);
        if (!archive)
            continue;
        QByteArray raw;
        try {
            const zim::Item item = archive->getEntryByPath(f.path.toStdString()).getItem(true);
            if (ZimReader::tooLarge(qint64(item.getSize()), true))
                continue;
            const zim::Blob blob = item.getData();
            raw = QByteArray(blob.data(), qsizetype(blob.size()));
        } catch (const std::exception &) {
            continue;
        }
        QList<Passage> passages = Extract::extract(raw).passages;
        if (passages.isEmpty())
            continue;  // not a readable Wikipedia-layout article (another kind of ZIM, a stub, a data table)
        // Rank fusion is per library, so each library's best article scores much the same, however loosely it matches:
        // "Ray Charles" contains "water" and "Polar Music Prize" and topped its library for "why is water polar", level
        // with "Chemical polarity". An article about the query names it in its title or opening; one that merely
        // mentions it does not.
        // An article the query names ("Ethanol" in "boiling point of ethanol") is its subject, whatever its opening says.
        f.score *= ZimSearch::AboutnessFloor + (f.named ? 1.0 : aboutness(stems, f.title, passages.first().text));
        out.append({f, std::move(passages)});
    }
    std::stable_sort(out.begin(), out.end(), [](const ZimSearch::Article &a, const ZimSearch::Article &b) {
        return a.found.score > b.found.score;
    });
    return out.mid(0, limit);
}
}

QList<ZimSearch::Found> ZimSearch::findArticles(const QString &query, const QList<qint64> &libraryIds, int limit)
{
    QList<Found> out;
    for (const Article &c : candidates(query, libraryIds, limit))
        out.append(c.found);
    return out;
}

QList<ZimSearch::Article> ZimSearch::readArticles(const QString &query, const QList<qint64> &libraryIds, int limit)
{
    return candidates(query, libraryIds, limit);
}

QList<ZimSearch::Hit> ZimSearch::search(const QString &query, const QList<qint64> &libraryIds, int limit)
{
    QStringList stems;
    for (const QString &t : terms(query))
        stems << stem(t);
    QList<Hit> out;
    for (const Article &c : candidates(query, libraryIds, limit)) {
        // the passage among the first dozen that has most of the query's words; the earliest one on a tie
        int best = 0;
        double bestOverlap = -1;
        for (int i = 0; i < std::min<qsizetype>(12, c.passages.size()); ++i) {
            const double o = overlap(stems, c.passages.at(i).text);
            if (o > bestOverlap) {
                best = i;
                bestOverlap = o;
            }
        }
        const QString text = c.passages.at(best).text.trimmed();
        out.append({c.found.libraryId, c.found.path, c.found.title, c.passages.at(best).section,
                    text.size() > 360 ? text.left(360) + QStringLiteral("…") : text, c.found.score, c.found.titleMatch});
    }
    return out;
}
