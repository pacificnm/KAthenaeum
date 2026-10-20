// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "projects.h"
#include "rag.h"
#include "zimsearch.h"

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
// ---- a report written to the user's instructions ----
// The model plans sections from the instructions, then writes each from its own sources (the project's articles and
// excerpts, and the library when asked), so a long report is several short prompts a small model can handle. Source
// numbers are added by the app, sentence by sentence, as in Ask.
struct ReportRequest {
    QString title;
    QString instructions;
    int sections = 5;  // at most; the plan may have fewer
    int words = 250;  // per section, roughly
    bool alsoLibrary = false;
};
struct PlannedSection {
    QString heading;
    QString brief;  // what it covers
};
constexpr int MaxReportArticles = 30;  // project articles read for a report
constexpr int ReportLibraryArticles = 6;  // per section, when the library is searched too
QString planPrompt(const ReportRequest &request, const QString &goal, const QStringList &sourceTitles);
// The outline the model wrote, one section per line ("1. Heading: what it covers"; "## Heading — …"; "- **Heading** - …")
QList<PlannedSection> parsePlan(const QString &text, int maxSections);
QString sectionPrompt(const ReportRequest &request, const PlannedSection &section, const QStringList &otherHeadings,
                      const QString &sources);
// The text without its last sentence when that was cut off (the model ran out of tokens); finished text is kept as is
QString dropUnfinishedSentence(const QString &text);
// What a section's sources are searched with: its heading and brief, the title, and the instructions' first sentence
QString sectionQuery(const ReportRequest &request, const PlannedSection &section);
// The section with "[n]" after each sentence its sources back, n being the report-wide number of the source
QString citeSection(const QString &text, const QList<Rag::Citation> &citations, const QList<int> &reportNumbers);
// Renumbers the report's "[n]" by first appearance (1, 2, 3…); `order` gets the old numbers in their new order
QString renumberCitations(const QString &markdown, QList<int> *order);
struct ReportSource {
    ArticleRef article;  // libraryId 0: a saved excerpt that came from no article (a saved answer)
    QString label;       // the whole entry when set (a saved excerpt); otherwise the entry is a link to the article
};
QString reportMarkdown(const ReportRequest &request, const QList<std::pair<QString, QString>> &sections, const QList<ReportSource> &references,
                       const QDate &today);

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
    // `libraries`: searched when the request asks for the whole library too (reads the database: call on the UI thread)
    void writeReport(qint64 projectId, const Drafts::ReportRequest &request, const QList<qint64> &libraries, const Models &models);
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
    void planReport();
    void writeSection();
    void sectionSources(const QList<Rag::Candidate> &own, const QList<Rag::Candidate> &library);
    void sectionWritten(const QString &text);
    void finishReport();

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

    // a report in progress
    Drafts::ReportRequest m_request;
    QList<qint64> m_libraries;
    QList<ZimSearch::Article> m_read;  // the project's articles, read once
    QList<Rag::Clip> m_reportClips;
    QList<Drafts::PlannedSection> m_plan;
    int m_section = 0;
    Rag::Context m_context;  // the section being written
    QList<std::pair<QString, QString>> m_written;  // (heading, body with report-wide [n])
    QList<Drafts::ReportSource> m_reportSources;  // report-wide numbering: index + 1
};
