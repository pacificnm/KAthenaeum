// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "projects.h"

#include <QObject>
#include <QPointer>

class OllamaClient;
class QNetworkReply;

// AI-drafted documents for a research project, ported from Athenaeum's research.py: a summary of the project's
// articles and excerpts, or notes from one chat. The output is Markdown for the user to edit. Source numbers in an
// overview are added by the app (embedding match), never by the model.
namespace Drafts
{
constexpr int MaxArticles = 15;           // each is a model call; a run stays at a couple of minutes
constexpr int ArticleChars = 3500;        // how much of an article the model reads: the lead and first sections
constexpr double OverviewCiteMin = 0.70;  // an overview sentence needs this similarity to a summary or excerpt for a [n]
constexpr int TranscriptChars = 5500;

QString articlePrompt(const QString &title, const QString &text);
// `material`: the article summaries and saved excerpts; `goal`: the research question (or the title)
QString overviewPrompt(const QString &material, const QString &goal);
QString chatPrompt(const QString &transcript);
// Questions and answers as "Q: …\nA: …", the most recent part kept when it is too long
QString transcript(const QList<std::pair<QString, QString>> &pairs);

// [n] after each overview sentence whose best candidate (a source summary or excerpt, with its source number) is
// close enough; a sentence saying the material does not cover something gets none. One vector per sentence of
// Rag::splitSentences(overview), one per candidate.
QString citeOverview(const QString &overview, const QList<Vector> &sentenceVectors, const QList<int> &candidateSources,
                     const QList<Vector> &candidateVectors);

struct SourceSummary {
    ArticleRef article;
    QString summary;
    QString note;
};
// The summary document: overview, each source with its summary and the user's note, saved excerpts, references
QString summaryMarkdown(const QString &projectTitle, const QString &overview, const QList<SourceSummary> &sources,
                        const QList<ProjectItem> &clips, int articlesInProject, const QDate &today);
// Top-level headings ("# Key findings") become sections ("## Key findings"): the document's title is the only top level.
// qwen3.5:2b wrote "#" although the prompt asks for "##" (2026-10-19).
QString demoteHeadings(const QString &markdown);
// "## Sources discussed" for chat notes: the articles the chat's answers drew on, once each (excerpts left out)
QString sourcesDiscussed(const QList<ArticleRef> &sources);
}

// Runs a draft against Ollama. One at a time; stop() ends it and nothing is kept.
class Drafter : public QObject
{
    Q_OBJECT

public:
    explicit Drafter(OllamaClient *client, QObject *parent = nullptr);
    ~Drafter() override;

    struct Models {
        QString answer;
        QString embedding;
    };

    // Reads the project's articles (opening archives: call on the UI thread)
    void summarizeProject(qint64 projectId, const Models &models);
    void summarizeChat(qint64 chatId, const Models &models);
    void stop();
    bool running() const { return m_running; }

Q_SIGNALS:
    void progress(int step, int total, const QString &label);
    void textAdded(const QString &text);  // chat notes stream in
    // status: done | stopped | error. On done, `title` and `markdown` make the new document.
    void finished(const QString &status, const QString &title, const QString &markdown, const QString &error);

private:
    void complete(const QString &prompt, int numPredict, std::function<void(const QString &text, const QString &error)> done,
                  bool stream = false);
    void summarizeNext();
    void writeOverview();
    void finish(const QString &status, const QString &error = {});

    QPointer<OllamaClient> m_client;
    QPointer<QNetworkReply> m_reply;
    Models m_models;
    quint64 m_generation = 0;
    bool m_running = false;
    bool m_think = false;  // the answer model has "thinking": send think false

    // a project summary in progress
    qint64 m_projectId = 0;
    ProjectRecord m_project;
    QList<ProjectItem> m_articles;
    QList<ProjectItem> m_clips;
    QList<std::pair<ArticleRef, QString>> m_texts;  // (article, readable text) in project order
    QList<Drafts::SourceSummary> m_summaries;
    int m_next = 0;
    QString m_title;
    QString m_markdown;
};
