// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "embedding.h"
#include "zimsearch.h"

#include <QList>
#include <QString>
#include <QStringList>

// Answering a question from the libraries: the parts of Athenaeum's rag.py and zimsearch.passages that need no network,
// so they can be tested. The network steps (embedding, the streamed answer) are driven by the app; see CLAUDE.md, "Ask".
namespace Rag
{
constexpr int Articles = 6;         // articles read for a question
constexpr int PerArticle = 6;       // passages kept per article (by word overlap) before embedding
constexpr int MaxEmbed = 40;        // passages embedded per question: ~1.2 s with nomic-embed-text here
constexpr int Passages = 14;        // ranked passages handed to the prompt builder
constexpr int MaxSources = 5;       // distinct articles shown to the model, and to the user as sources
constexpr int ChunksPerSource = 2;
constexpr int ContextChars = 8000;  // ~2000 tokens of evidence: reading the prompt is what the user waits for
constexpr int NumCtx = 6144;
constexpr int NumPredict = 700;
constexpr double Temperature = 0.2;
constexpr int MaxEarlierQuestions = 3;  // named in a follow-up's prompt; earlier answers are not replayed (see userMessage)
constexpr double SupportMin = 0.68;  // below this no source clearly backs a sentence (nomic-embed-text; re-measure for another model)
constexpr double CiteTie = 0.03;     // a second source within this of the best one is cited too
constexpr int MaxClips = 3;          // saved excerpts shown to the model in a project chat
constexpr int ClipChars = 1200;      // per excerpt
constexpr double ClipMinSim = 0.45;  // below this an excerpt is not relevant enough
constexpr int LibrarySources = 3;    // a project chat that also searches the library: articles from outside it

// A passage that may go into the prompt
struct Candidate {
    qint64 libraryId = 0;
    QString path;
    QString title;
    QString section;
    QString text;
    double keywords = 0;  // share of the question's words in it
    int index = 0;        // its position in the article (0 = the introduction)
    double score = 0;
    Vector vector;        // empty when ranked without embeddings
};

// A numbered source in the prompt: one article and its best passages
struct Source {
    int n = 0;
    QString kind;  // empty: an article (or a section of one); "clip": a saved excerpt; "library": outside the project
    qint64 libraryId = 0;
    QString path;
    QString title;
    QString section;
    QString snippet;
    QString evidence;  // exactly what the model was shown for it (cut to its share of the budget)
    QList<Vector> vectors;
};

// A saved excerpt of a project, as the project chat sees it
struct Clip {
    qint64 id = 0;
    QString title;  // the article it came from (empty for a saved answer)
    QString section;
    QString text;
    qint64 libraryId = 0;
    QString path;
    Vector vector;  // empty when not embedded with the current model
};

struct Context {
    QList<Source> sources;
    QString text;  // the numbered sources, as they go into the prompt
};

struct Citation {
    int start = 0;  // the sentence, as offsets into the answer
    int end = 0;
    QList<int> sources;  // source numbers, best first; at most two
    double score = 0;    // similarity to the best source
    QString reason;      // empty when supported; "topic" (no source is close) or "number" (a figure no source has)
    QStringList numbers;
};

// The question as searched: a short follow-up ("and its boiling point?") carries no topic, so it borrows the previous one
QString retrievalQuery(const QString &question, const QStringList &earlierQuestions);

// Each article's best passages by word overlap (always its introduction), taken round-robin up to MaxEmbed. A chat
// about one article uses the same six: ranking all of a long article's passages by meaning was measured (Ray Charles,
// 11 questions, 2026-10-19) and helped one question while adding 1.1 s to every one.
QList<Candidate> pickPassages(const QString &query, const QList<ZimSearch::Article> &articles);

// The best `n` of the pool: word overlap and, when there are vectors, similarity to the question, fused by rank (RRF)
QList<Candidate> rank(QList<Candidate> pool, const Vector &question, int n = Passages);

// Up to MaxSources articles with their best passages, numbered, within ContextChars. `bySection`: the sources are
// sections of one article instead (a chat about that article), so a citation names "Career", not the article again.
// A project chat adds its saved excerpts (first) and, when it also searches the library, up to LibrarySources articles
// from outside the project as a separately labelled group.
Context buildContext(const QList<Candidate> &ranked, bool bySection = false, const QList<Clip> &clips = {},
                     const QList<Candidate> &library = {});

// The excerpts worth showing for this question: the best MaxClips at or above ClipMinSim. Excerpts with no vector (not
// embedded) are offered newest first, at most two.
QList<Clip> rankClips(const QList<Clip> &clips, const Vector &question);
// A project with many articles: the `n` whose passages share the most words with the question (ties: the order given)
QList<ZimSearch::Article> chooseArticles(const QString &query, const QList<ZimSearch::Article> &articles, int n = Articles);

QString systemPrompt();
// For a chat about one article: answer from it alone, and say so when it does not cover the question
QString articleSystemPrompt(const QString &title);
// For a project chat: its title and goal, and whether other library articles may be among the sources
QString projectSystemPrompt(const QString &title, const QString &goal, bool withLibrary);
// `earlierQuestions` (oldest first; the last MaxEarlierQuestions are used) say what "it" or "he" refers to in a
// follow-up. Earlier answers are deliberately not replayed as chat turns, as Athenaeum did: given its own previous
// answer, qwen3.5:2b copied it word for word instead of answering the new question (2 of 4 follow-ups; 0 of 4 with
// the questions named instead, measured 2026-10-19 with askbench).
QString userMessage(const Context &context, const QString &question, const QStringList &earlierQuestions = {});

// The answer without the source numbers the model wrote itself ("[3]", "Sources: [1], [3]"), which it does despite being
// told not to; the app's own citations replace them. Applied while the answer streams and before citations are matched.
QString tidyAnswer(const QString &answer);

// Sentences (and list items and lines) as offsets into the text; list markers are not part of them
QList<std::pair<int, int>> splitSentences(const QString &text);
// "I could not find…", "The sources do not state…"
bool isRefusal(const QString &sentence);
// The sentences worth citing: none when the answer opens by saying it found nothing, never a refusal sentence
QList<std::pair<int, int>> citableSentences(const QString &answer);
// Figures (more than one digit) in the sentence that appear in none of the evidence
QStringList ungroundedNumbers(const QString &sentence, const QString &evidence);

// Which sources back each sentence, from the sentences' vectors (one per span, same embedding model as the sources)
QList<Citation> attribute(const QString &answer, const QList<std::pair<int, int>> &spans, const QList<Vector> &sentenceVectors,
                          const QList<Source> &sources);

// The answer as Markdown with citation links after each sentence: [n](cite:n), and [?](check:i) for a flagged one
QString annotate(const QString &answer, const QList<Citation> &citations);
}
