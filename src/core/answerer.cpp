// SPDX-License-Identifier: GPL-3.0-or-later
#include "answerer.h"

#include "extract.h"
#include "ollama.h"
#include "zimreader.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QtConcurrent>

Answerer::Answerer(OllamaClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

Answerer::~Answerer()
{
    OllamaClient::cancel(m_reply);
}

QString Answerer::noSourcesAnswer()
{
    return QStringLiteral("I couldn't find anything relevant to that in the library.");
}

QString Answerer::noTextAnswer()
{
    return QStringLiteral("This page has no article text I can answer from.");
}

void Answerer::start(const AnswerRequest &request)
{
    if (m_running)
        stop();
    const quint64 generation = ++m_generation;
    m_request = request;
    m_request.question = request.question.trimmed();
    m_running = true;
    m_degraded = false;
    m_context = {};
    m_result = {};
    m_clock.start();

    m_query = Rag::retrievalQuery(m_request.question, request.earlierQuestions);

    Q_EMIT phaseChanged(Finding, 0);
    auto *watcher = new QFutureWatcher<QList<Rag::Candidate>>(this);
    connect(watcher, &QFutureWatcher<QList<Rag::Candidate>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_result.stats.findMs = int(m_clock.elapsed());
        rankAndAsk(watcher->result());
    });
    if (request.article.isSet()) {
        // one article: read here (the archive may need opening, which reads the database), cut up in the worker
        const auto content = ZimReader::content(request.article.libraryId, request.article.path);
        if (!content) {
            watcher->deleteLater();
            finish(QStringLiteral("error"), QStringLiteral("“%1” cannot be read: its library may have been removed.").arg(request.article.title));
            return;
        }
        watcher->setFuture(QtConcurrent::run([query = m_query, article = request.article, data = content->data, path = content->path] {
            ZimSearch::Article a;
            a.found.libraryId = article.libraryId;
            a.found.path = path;
            a.found.title = article.title;
            a.passages = Extract::extract(data).passages;
            return Rag::pickPassages(query, {a});
        }));
        return;
    }
    ZimSearch::prepare(request.libraryIds);  // opens the archives here: the worker cannot read the database
    watcher->setFuture(QtConcurrent::run([query = m_query, ids = request.libraryIds] {
        return Rag::pickPassages(query, ZimSearch::readArticles(query, ids, Rag::Articles));
    }));
}

void Answerer::rankAndAsk(QList<Rag::Candidate> pool)
{
    if (pool.isEmpty()) {
        Q_EMIT sourcesReady({}, false, {});
        m_result.answer = m_request.article.isSet() ? noTextAnswer() : noSourcesAnswer();
        Q_EMIT textAdded(m_result.answer);
        finish(QStringLiteral("done"));
        return;
    }
    Q_EMIT phaseChanged(Ranking, int(pool.size()));
    const QString model = m_request.embeddingModel;
    QStringList input{Embedding::formatQuery(model, m_query)};
    for (const Rag::Candidate &c : std::as_const(pool))
        input << Embedding::formatDocument(model, c.text.left(3000));
    const quint64 generation = m_generation;
    m_reply = m_client->embed(model, input, this, [this, pool, generation](const QList<Vector> &vectors, const QString &error) mutable {
        if (generation != m_generation)
            return;
        m_result.stats.embedMs = int(m_clock.elapsed()) - m_result.stats.findMs;
        Vector question;
        QString note;
        if (error.isEmpty()) {
            question = vectors.first();
            for (int i = 0; i < pool.size(); ++i)
                pool[i].vector = vectors.at(i + 1);
        } else {
            m_degraded = true;
            note = error;
        }
        const QList<Rag::Candidate> ranked = Rag::rank(pool, question);
        m_result.stats.passages = int(ranked.size());
        m_context = Rag::buildContext(ranked, m_request.article.isSet());
        Q_EMIT sourcesReady(m_context.sources, m_degraded, note);
        ask();
    });
}

void Answerer::ask()
{
    Q_EMIT phaseChanged(Reading, int(m_context.sources.size()));
    const quint64 generation = m_generation;
    const QString model = m_request.answerModel;
    // Thinking models spend the token budget on hidden reasoning and can return nothing: switch it off where the model
    // has it (not every Ollama accepts the flag for models without it)
    m_client->capabilities(model, this, [this, generation, model](const QStringList &capabilities, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        const QString system = m_request.article.isSet() ? Rag::articleSystemPrompt(m_request.article.title) : Rag::systemPrompt();
        QJsonArray messages{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}}};
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), Rag::userMessage(m_context, m_request.question, m_request.earlierQuestions)}});
        QJsonObject body{{QStringLiteral("model"), model},
                         {QStringLiteral("messages"), messages},
                         {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), Rag::Temperature},
                                                                 {QStringLiteral("num_ctx"), Rag::NumCtx},
                                                                 {QStringLiteral("num_predict"), Rag::NumPredict}}}};
        if (capabilities.contains(QStringLiteral("thinking")))
            body.insert(QStringLiteral("think"), false);
        m_reply = m_client->chat(
            body, this,
            [this, generation](const QString &text) {
                if (generation != m_generation)
                    return;
                if (m_result.answer.isEmpty()) {
                    m_result.stats.firstTokenMs = int(m_clock.elapsed());
                    Q_EMIT phaseChanged(Writing, 0);
                }
                m_result.answer += text;
                Q_EMIT textAdded(text);
            },
            [this, generation](const QJsonObject &last, const QString &error) {
                if (generation != m_generation)
                    return;
                m_reply.clear();
                if (!error.isEmpty()) {
                    finish(QStringLiteral("error"), error);
                    return;
                }
                const double evalSeconds = last.value(QLatin1String("eval_duration")).toDouble() / 1e9;
                m_result.stats.tokens = last.value(QLatin1String("eval_count")).toInt();
                m_result.stats.tokensPerSecond = evalSeconds > 0 ? m_result.stats.tokens / evalSeconds : 0;
                m_result.stats.truncated = last.value(QLatin1String("done_reason")).toString() == QLatin1String("length");
                check();
            });
    });
}

void Answerer::check()
{
    m_result.answer = Rag::tidyAnswer(m_result.answer);
    const auto spans = Rag::citableSentences(m_result.answer);
    if (m_degraded || spans.isEmpty()) {
        finish(QStringLiteral("done"));
        return;
    }
    Q_EMIT phaseChanged(Checking, int(spans.size()));
    const int before = int(m_clock.elapsed());
    QStringList input;
    for (const auto &[a, b] : spans)
        input << Embedding::formatDocument(m_request.embeddingModel, m_result.answer.mid(a, b - a));
    const quint64 generation = m_generation;
    m_reply = m_client->embed(m_request.embeddingModel, input, this, [this, spans, generation, before](const QList<Vector> &vectors, const QString &error) {
        if (generation != m_generation)
            return;
        m_result.stats.citeMs = int(m_clock.elapsed()) - before;
        // citations are a bonus: never fail the answer over them
        if (error.isEmpty())
            m_result.citations = Rag::attribute(m_result.answer, spans, vectors, m_context.sources);
        else
            qWarning("Citations skipped: %s", qUtf8Printable(error));
        finish(QStringLiteral("done"));
    });
}

void Answerer::stop()
{
    if (!m_running)
        return;
    OllamaClient::cancel(m_reply);
    finish(QStringLiteral("stopped"));
}

void Answerer::finish(const QString &status, const QString &error)
{
    ++m_generation;  // whatever is still on its way belongs to this answer
    m_reply.clear();
    m_running = false;
    m_result.status = status;
    m_result.error = error;
    m_result.stats.totalMs = int(m_clock.elapsed());
    Q_EMIT finished(m_result);
}
