// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rag.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class OllamaClient;
class QNetworkReply;

// The article a chat is about; libraryId 0 = the whole collection
struct ArticleScope {
    qint64 libraryId = 0;
    QString path;
    QString title;
    bool isSet() const { return libraryId > 0 && !path.isEmpty(); }
};

struct AnswerRequest {
    QString question;
    QStringList earlierQuestions;  // earlier questions of the chat, oldest first (for follow-ups)
    QList<qint64> libraryIds;
    QString answerModel;
    QString embeddingModel;
    ArticleScope article;  // when set, answer from this article alone (libraryIds is then ignored)
};

struct AnswerStats {
    int findMs = 0;        // finding and reading articles, picking passages
    int embedMs = 0;       // embedding the question and the passages
    int firstTokenMs = 0;  // from the start until the first word (what the user waits for)
    int citeMs = 0;
    int totalMs = 0;
    int tokens = 0;
    double tokensPerSecond = 0;
    bool truncated = false;  // the model hit NumPredict
    int passages = 0;        // passages ranked
};

struct AnswerResult {
    QString status;  // done | stopped | error
    QString answer;
    QList<Rag::Citation> citations;
    QString error;
    AnswerStats stats;
};

// One question, from finding articles to cited sentences. Runs on the UI thread; the heavy reading runs in a worker.
// Steps: find and read articles (worker) -> embed question and passages -> rank -> build the prompt -> stream the answer
// -> embed the answer's sentences and match them to the sources. Without the embedding model the passages are ranked by
// words alone (`degraded`) and there are no citations; without the answer model the sources are still shown.
class Answerer : public QObject
{
    Q_OBJECT

public:
    enum Phase { Finding, Ranking, Reading, Writing, Checking };
    Q_ENUM(Phase)

    Answerer(OllamaClient *client, QObject *parent = nullptr);
    ~Answerer() override;

    // Opens the libraries' archives (database access), so call it on the UI thread
    void start(const AnswerRequest &request);
    // Ends the answer now: finished() follows with status "stopped" and what was written so far
    void stop();
    bool running() const { return m_running; }

    static QString noSourcesAnswer();
    static QString noTextAnswer();  // an article with no prose to answer from

Q_SIGNALS:
    void phaseChanged(Answerer::Phase phase, int count);
    void sourcesReady(const QList<Rag::Source> &sources, bool degraded, const QString &note);
    void textAdded(const QString &text);
    void finished(const AnswerResult &result);

private:
    void rankAndAsk(QList<Rag::Candidate> pool);
    void ask();
    void check();
    void finish(const QString &status, const QString &error = {});

    QPointer<OllamaClient> m_client;
    QPointer<QNetworkReply> m_reply;
    AnswerRequest m_request;
    QString m_query;  // what is searched: the question, or a short follow-up with the question before it
    quint64 m_generation = 0;
    bool m_running = false;
    bool m_degraded = false;
    QElapsedTimer m_clock;
    Rag::Context m_context;
    AnswerResult m_result;
};
