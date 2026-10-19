// SPDX-License-Identifier: GPL-3.0-or-later
#include "drafts.h"

#include "chats.h"
#include "extract.h"
#include "ollama.h"
#include "rag.h"
#include "zimreader.h"
#include "zimsearch.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>

// ---- pure parts ----

QString Drafts::articlePrompt(const QString &title, const QString &text)
{
    return QStringLiteral("Article: %1\n\n%2\n\nSummarize the key facts of this article in 3 to 4 plain sentences. "
                          "Use only the text above. Do not add anything that is not in it.")
        .arg(title, text);
}

QString Drafts::overviewPrompt(const QString &material, const QString &goal)
{
    // "Do NOT restate the question": Athenaeum's first overview just paraphrased the research goal and cited it
    return QStringLiteral("Material gathered for a research project:\n\n%1\n\nThe research question is: %2\n\n"
                          "Write 4 to 6 sentences stating what the material above actually says that bears on the research question. "
                          "Start with the most important finding. Do NOT restate or paraphrase the question or the project title. "
                          "If part of the question is not addressed by the material, say which part. Use only the material above; "
                          "do not write source numbers.")
        .arg(material, goal);
}

QString Drafts::chatPrompt(const QString &transcript)
{
    return QStringLiteral("This is a research conversation:\n\n%1\n\nSummarize it as concise Markdown notes with exactly these sections: "
                          "'## Questions explored', '## Key findings' (bullet points), '## Open questions' (bullet points). "
                          "Use only what the conversation says. Do not write source numbers.")
        .arg(transcript);
}

QString Drafts::transcript(const QList<std::pair<QString, QString>> &pairs)
{
    QStringList parts;
    for (const auto &[q, a] : pairs)
        parts << QStringLiteral("Q: %1\nA: %2").arg(q.trimmed(), Rag::tidyAnswer(a));
    const QString t = parts.join(QStringLiteral("\n\n"));
    return t.size() > TranscriptChars ? QStringLiteral("…") + t.right(TranscriptChars) : t;  // the most recent part
}

QString Drafts::citeOverview(const QString &overview, const QList<Vector> &sentenceVectors, const QList<int> &candidateSources,
                             const QList<Vector> &candidateVectors)
{
    const auto spans = Rag::splitSentences(overview);
    if (spans.size() != sentenceVectors.size() || candidateSources.size() != candidateVectors.size() || candidateVectors.isEmpty())
        return overview;
    QString out = overview;
    for (qsizetype k = spans.size() - 1; k >= 0; --k) {  // from the end, so earlier offsets stay valid
        const auto [a, b] = spans.at(k);
        double best = -1;
        int source = 0;
        for (int c = 0; c < candidateVectors.size(); ++c) {
            const double sim = Embedding::cosine(sentenceVectors.at(k), candidateVectors.at(c));
            if (sim > best) {
                best = sim;
                source = candidateSources.at(c);
            }
        }
        // a sentence saying the material does NOT cover something has nothing to cite
        if (best >= OverviewCiteMin && !Rag::isRefusal(overview.mid(a, b - a)))
            out.insert(b, QStringLiteral(" [%1]").arg(source));
    }
    return out;
}

QString Drafts::summaryMarkdown(const QString &projectTitle, const QString &overview, const QList<SourceSummary> &sources,
                                const QList<ProjectItem> &clips, int articlesInProject, const QDate &today)
{
    Q_UNUSED(projectTitle)
    QStringList md;
    const QString articles = sources.size() == 1 ? QStringLiteral("1 article") : QStringLiteral("%1 articles").arg(sources.size());
    const QString excerpts = clips.size() == 1 ? QStringLiteral("1 saved excerpt") : QStringLiteral("%1 saved excerpts").arg(clips.size());
    md << QStringLiteral("*Generated %1 from %2 and %3. Written by a small AI: check it against the sources.*")
              .arg(today.toString(Qt::ISODate), articles, excerpts)
       << QString();
    if (articlesInProject > MaxArticles)
        md << QStringLiteral("> Only the first %1 of %2 articles were summarized.").arg(MaxArticles).arg(articlesInProject) << QString();
    if (!overview.trimmed().isEmpty())
        md << QStringLiteral("## Overview") << QString() << overview.trimmed() << QString();
    if (!sources.isEmpty()) {
        md << QStringLiteral("## Sources") << QString();
        for (int i = 0; i < sources.size(); ++i) {
            const SourceSummary &s = sources.at(i);
            md << QStringLiteral("### %1. %2").arg(i + 1).arg(Projects::articleLink(s.article)) << QString() << s.summary.trimmed() << QString();
            if (!s.note.isEmpty())
                md << QStringLiteral("> **Your note:** ") + s.note << QString();
        }
    }
    if (!clips.isEmpty()) {
        md << QStringLiteral("## Saved excerpts") << QString();
        for (auto it = clips.crbegin(); it != clips.crend(); ++it) {  // oldest first
            md << Projects::quote(*it);
            if (!it->note.isEmpty())
                md << QString() << QStringLiteral("**Note:** ") + it->note;
            md << QString();
        }
    }
    if (!sources.isEmpty()) {
        md << QStringLiteral("## References") << QString();
        for (int i = 0; i < sources.size(); ++i)
            md << QStringLiteral("%1. %2").arg(i + 1).arg(Projects::articleLink(sources.at(i).article));
        md << QString();
    }
    return md.join(QLatin1Char('\n'));
}

QString Drafts::demoteHeadings(const QString &markdown)
{
    QStringList lines = markdown.split(QLatin1Char('\n'));
    bool fenced = false;
    for (QString &l : lines) {
        if (l.startsWith(QLatin1String("```")))
            fenced = !fenced;
        if (!fenced && l.startsWith(QLatin1String("# ")))
            l.prepend(QLatin1Char('#'));
    }
    return lines.join(QLatin1Char('\n'));
}

QString Drafts::sourcesDiscussed(const QList<ArticleRef> &sources)
{
    QSet<QString> seen;
    QStringList lines;
    for (const ArticleRef &r : sources) {
        const QString key = QString::number(r.libraryId) + QLatin1Char('/') + r.path;
        if (r.libraryId <= 0 || r.path.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        lines << QStringLiteral("- ") + Projects::articleLink(r);
    }
    return lines.isEmpty() ? QString() : QStringLiteral("## Sources discussed\n\n") + lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// ---- running a draft ----

Drafter::Drafter(OllamaClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

Drafter::~Drafter()
{
    OllamaClient::cancel(m_reply);
}

void Drafter::complete(const QString &prompt, int numPredict, std::function<void(const QString &, const QString &)> done, bool stream)
{
    QJsonObject body{{QStringLiteral("model"), m_models.answer},
                     {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                         {QStringLiteral("content"), prompt}}}},
                     {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), Rag::Temperature},
                                                             {QStringLiteral("num_ctx"), Rag::NumCtx},
                                                             {QStringLiteral("num_predict"), numPredict}}}};
    if (m_think)
        body.insert(QStringLiteral("think"), false);
    auto text = std::make_shared<QString>();
    const quint64 generation = m_generation;
    m_reply = m_client->chat(
        body, this,
        [this, text, stream, generation](const QString &piece) {
            if (generation != m_generation)
                return;
            *text += piece;
            if (stream)
                Q_EMIT textAdded(piece);
        },
        [this, text, done, generation](const QJsonObject &, const QString &error) {
            if (generation != m_generation)
                return;
            m_reply.clear();
            done(text->trimmed(), error);
        });
}

void Drafter::summarizeProject(qint64 projectId, const Models &models)
{
    stop();
    const quint64 generation = ++m_generation;
    m_models = models;
    m_running = true;
    m_projectId = projectId;
    m_texts.clear();
    m_summaries.clear();
    m_next = 0;
    const auto project = Projects::byId(projectId);
    if (!project) {
        finish(QStringLiteral("error"), QStringLiteral("This project no longer exists."));
        return;
    }
    m_project = *project;
    m_articles = Projects::items(projectId, QStringLiteral("article"));
    std::reverse(m_articles.begin(), m_articles.end());  // in the order they were added
    m_clips = Projects::items(projectId, QStringLiteral("clip"));
    if (m_articles.isEmpty() && m_clips.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("Add some articles or excerpts to this project first."));
        return;
    }
    m_title = QStringLiteral("Summary: ") + m_project.title;
    const int total = int(qMin<qsizetype>(m_articles.size(), Drafts::MaxArticles)) + 1;
    Q_EMIT progress(0, total, QStringLiteral("Reading the articles"));

    QList<qint64> libraries;
    QList<ArticleRef> refs;
    for (const ProjectItem &a : m_articles.mid(0, Drafts::MaxArticles)) {
        refs << a.article;
        if (!libraries.contains(a.article.libraryId))
            libraries << a.article.libraryId;
    }
    ZimSearch::prepare(libraries);  // archives opened here: opening reads the database
    m_client->capabilities(models.answer, this, [this, generation, refs](const QStringList &capabilities, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        m_think = capabilities.contains(QStringLiteral("thinking"));
        auto *watcher = new QFutureWatcher<QList<std::pair<ArticleRef, QString>>>(this);
        connect(watcher, &QFutureWatcher<QList<std::pair<ArticleRef, QString>>>::finished, this, [this, watcher, generation] {
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            m_texts = watcher->result();
            summarizeNext();
        });
        // read straight from the ZIM: the lead and first sections, as prose
        watcher->setFuture(QtConcurrent::run([refs] {
            QList<std::pair<ArticleRef, QString>> out;
            for (const ArticleRef &r : refs) {
                const auto content = ZimReader::content(r.libraryId, r.path);
                if (!content)
                    continue;
                QStringList texts;
                for (const Passage &p : Extract::extract(content->data).passages)
                    texts << p.text;
                const QString text = texts.join(QStringLiteral("\n\n")).left(Drafts::ArticleChars);
                if (!text.trimmed().isEmpty())
                    out.append({r, text});
            }
            return out;
        }));
    });
}

void Drafter::summarizeNext()
{
    const int total = int(qMin<qsizetype>(m_articles.size(), Drafts::MaxArticles)) + 1;
    if (m_next >= m_texts.size()) {
        writeOverview();
        return;
    }
    const auto [article, text] = m_texts.at(m_next);
    Q_EMIT progress(m_next + 1, total, QStringLiteral("Summarizing “%1”").arg(article.title));
    complete(Drafts::articlePrompt(article.title, text), 200, [this, article](const QString &summary, const QString &error) {
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        QString note;
        for (const ProjectItem &a : std::as_const(m_articles))
            if (a.article.libraryId == article.libraryId && a.article.path == article.path)
                note = a.note;
        m_summaries.append({article, Rag::tidyAnswer(summary), note});
        ++m_next;
        summarizeNext();
    });
}

void Drafter::writeOverview()
{
    const int total = int(qMin<qsizetype>(m_articles.size(), Drafts::MaxArticles)) + 1;
    const QDate today = QDate::currentDate();
    if (m_summaries.isEmpty() && m_clips.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("None of the project's articles could be read."));
        return;
    }
    Q_EMIT progress(total, total, QStringLiteral("Writing the overview"));
    QStringList digest;
    for (const Drafts::SourceSummary &s : std::as_const(m_summaries))
        digest << s.article.title + QStringLiteral(": ") + s.summary;
    QStringList quotes;
    for (const ProjectItem &c : m_clips.mid(0, 6))
        quotes << QStringLiteral("Saved excerpt (%1): %2").arg(c.refs.value(0).title.isEmpty() ? QStringLiteral("note") : c.refs.value(0).title,
                                                              c.text.trimmed().left(600));
    const QString material = QString(digest.join(QStringLiteral("\n\n")) + QStringLiteral("\n\n") + quotes.join(QStringLiteral("\n\n"))).trimmed().left(5800);
    const QString goal = m_project.description.trimmed().isEmpty() ? m_project.title : m_project.description.trimmed();
    complete(Drafts::overviewPrompt(material, goal), 340, [this, today](const QString &text, const QString &error) {
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        const QString overview = Rag::tidyAnswer(text);
        // candidates for the overview's source numbers: each summary, and excerpts from a summarized article
        QList<int> sources;
        QStringList input;
        for (int i = 0; i < m_summaries.size(); ++i) {
            sources << i + 1;
            input << Embedding::formatDocument(m_models.embedding, m_summaries.at(i).summary);
        }
        for (const ProjectItem &c : std::as_const(m_clips))
            for (int i = 0; i < m_summaries.size(); ++i)
                if (c.article.libraryId == m_summaries.at(i).article.libraryId && c.article.path == m_summaries.at(i).article.path) {
                    sources << i + 1;
                    input << Embedding::formatDocument(m_models.embedding, c.text.trimmed().left(600));
                }
        const auto spans = Rag::splitSentences(overview);
        const int candidates = int(input.size());
        for (const auto &[a, b] : spans)
            input << Embedding::formatDocument(m_models.embedding, overview.mid(a, b - a));
        auto done = [this, today, overview](const QString &cited) {
            m_markdown = Drafts::summaryMarkdown(m_project.title, cited, m_summaries, m_clips, int(m_articles.size()), today);
            finish(QStringLiteral("done"));
        };
        if (candidates == 0 || spans.isEmpty()) {
            done(overview);
            return;
        }
        const quint64 generation = m_generation;
        m_reply = m_client->embed(m_models.embedding, input, this,
                                  [this, generation, overview, sources, candidates, done](const QList<Vector> &vectors, const QString &error) {
                                      if (generation != m_generation)
                                          return;
                                      if (!error.isEmpty()) {  // citations are a bonus
                                          qWarning("Overview citations skipped: %s", qUtf8Printable(error));
                                          done(overview);
                                          return;
                                      }
                                      done(Drafts::citeOverview(overview, vectors.mid(candidates), sources, vectors.mid(0, candidates)));
                                  });
    });
}

void Drafter::summarizeChat(qint64 chatId, const Models &models)
{
    stop();
    const quint64 generation = ++m_generation;
    m_models = models;
    m_running = true;
    const auto chat = Chats::byId(chatId);
    if (!chat) {
        finish(QStringLiteral("error"), QStringLiteral("This chat no longer exists."));
        return;
    }
    QList<std::pair<QString, QString>> pairs;
    QList<ArticleRef> sources;
    QString question;
    for (const MessageRecord &m : Chats::messages(chatId)) {
        if (m.role == QLatin1String("user")) {
            question = m.content;
            continue;
        }
        if (m.content.trimmed().isEmpty() || question.isEmpty())
            continue;
        pairs.append({question, m.content});
        question.clear();
        for (const QJsonValue &v : QJsonDocument::fromJson(m.sources).array()) {
            const QJsonObject s = v.toObject();
            if (s.value(QLatin1String("kind")).toString() != QLatin1String("clip"))
                sources << ArticleRef{qint64(s.value(QLatin1String("libraryId")).toDouble()), s.value(QLatin1String("path")).toString(),
                                      s.value(QLatin1String("title")).toString()};
        }
    }
    if (pairs.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("That chat has no answers to summarize yet."));
        return;
    }
    m_title = QStringLiteral("Chat summary: ") + chat->title;
    Q_EMIT progress(1, 1, QStringLiteral("Writing notes on the chat"));
    m_client->capabilities(models.answer, this, [this, generation, pairs, sources](const QStringList &capabilities, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        m_think = capabilities.contains(QStringLiteral("thinking"));
        complete(
            Drafts::chatPrompt(Drafts::transcript(pairs)), 600,
            [this, sources](const QString &text, const QString &error) {
                if (!error.isEmpty()) {
                    finish(QStringLiteral("error"), error);
                    return;
                }
                const QString tail = Drafts::sourcesDiscussed(sources);
                m_markdown = Drafts::demoteHeadings(Rag::tidyAnswer(text)) + (tail.isEmpty() ? QStringLiteral("\n") : QStringLiteral("\n\n") + tail);
                finish(QStringLiteral("done"));
            },
            true);
    });
}

void Drafter::stop()
{
    if (!m_running)
        return;
    OllamaClient::cancel(m_reply);
    finish(QStringLiteral("stopped"));
}

void Drafter::finish(const QString &status, const QString &error)
{
    ++m_generation;
    m_reply.clear();
    m_running = false;
    Q_EMIT finished(status, m_title, status == QLatin1String("done") ? m_markdown : QString(), error);
}
