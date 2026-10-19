// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "extract.h"

#include <QList>
#include <QString>
#include <QStringList>

// Finding articles in libraries that have no index, through the ZIM itself: a port of Athenaeum's zimsearch.py, same
// rules and weights (see CLAUDE.md, "How a question is answered"). Search results and Ask both start here.
namespace ZimSearch
{
constexpr int RrfK = 60;
constexpr double TitleWeight = 3.0;   // an exact title is the strongest signal
constexpr double TitlePrefix = 1.5;   // a title starting with the query's words
constexpr double LeaveOneOut = 0.6;   // "every word but one", against 1.0 for every word
constexpr double SingleWord = 0.3;
constexpr int PerLibrary = 10;        // articles taken from each Xapian query
constexpr int AndMin = 5;             // fewer distinct articles than this and the query is widened to single words
constexpr int MaxTerms = 8;
constexpr int MaxNgramHits = 6;
constexpr double AboutnessFloor = 0.25;  // score x (floor + share of the query's words in the title and introduction)

// ---- the words of a query (pure functions) ----

// The words worth searching for, in order, no repeats, stop words dropped ("why is the sky blue" -> sky, blue)
QStringList terms(const QString &query);
// Spellings that might be an article's exact title (ZIM titles are case-sensitive): as typed, without question words, cased
QStringList titleCandidates(const QString &query);
// A question in words ("why is the sky blue") as opposed to a name or topic ("noble gases", "Ray Charles")
bool isQuestion(const QString &query);
// Runs of 1 to 4 words that could be a title, longest first, with a weight: "boiling point", "ethanol"
QList<std::pair<QString, double>> ngramTitles(const QString &query);
// Crude stem: kidney/kidneys, diabetes/diabetic
QString stem(const QString &word);
// Share of the stems found in the text
double overlap(const QStringList &stems, const QString &text);

// ---- finding articles (thread-safe once ZimSearch::prepare has run on the UI thread) ----

struct Found {
    qint64 libraryId = 0;
    QString path;
    QString title;
    double score = 0;
    bool titleMatch = false;  // the query named this article exactly
    bool named = false;       // its title is the query or a run of its words ("Ethanol" in "boiling point of ethanol")
};

struct Hit {
    qint64 libraryId = 0;
    QString path;
    QString title;
    QString section;  // where the snippet comes from, empty for the introduction
    QString snippet;
    double score = 0;
    bool titleMatch = false;
};

// An article found for a query, with its prose passages (read once: search snippets and Ask both use them)
struct Article {
    Found found;
    QList<Passage> passages;
};

// Opens the libraries' archives (reads the database, so on the UI thread) before a search runs on a worker thread
void prepare(const QList<qint64> &libraryIds);

// Best articles across the libraries: rank-fused per library (so a big library does not drown a small one), then
// re-ranked by whether the article is about the query (its title and introduction name the query's words). Articles
// with no readable prose are left out.
QList<Found> findArticles(const QString &query, const QList<qint64> &libraryIds, int limit);
// The same, with each article's passages
QList<Article> readArticles(const QString &query, const QList<qint64> &libraryIds, int limit);

// The same, with the best-matching passage of each article as a snippet; articles that cannot be read are left out
QList<Hit> search(const QString &query, const QList<qint64> &libraryIds, int limit);
}
