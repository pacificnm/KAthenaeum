// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "drafts.h"

#include "chats.h"
#include "extract.h"
#include "ollama.h"
#include "rag.h"
#include "zimreader.h"
#include "zimsearch.h"

#include <KLocalizedString>

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrentRun>

// ---- pure parts ----

// model prompts (articlePrompt, overviewPrompt, notesPrompt, transcript, planPrompt, sectionPrompt): stay English (the model
// is asked in English and its reply is parsed)
QString Drafts::articlePrompt(const QString &title, const QString &text)
{
    return QStringLiteral(
               "Article: %1\n\n%2\n\nSummarize the key facts of this article in 3 to 4 plain sentences. "
               "Use only the text above. Do not add anything that is not in it.")
        .arg(title, text);
}

QString Drafts::overviewPrompt(const QString &material, const QString &goal)
{
    // "Do NOT restate the question": Athenaeum's first overview just paraphrased the research goal and cited it
    return QStringLiteral(
               "Material gathered for a research project:\n\n%1\n\nThe research question is: %2\n\n"
               "Write 4 to 6 sentences stating what the material above actually says that bears on the research question. "
               "Start with the most important finding. Do NOT restate or paraphrase the question or the project title. "
               "If part of the question is not addressed by the material, say which part. Use only the material above; "
               "do not write source numbers.")
        .arg(material, goal);
}

QString Drafts::chatPrompt(const QString &transcript)
{
    return QStringLiteral(
               "This is a research conversation:\n\n%1\n\nSummarize it as concise Markdown notes with exactly these sections: "
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
    return t.size() > TranscriptChars ? QStringLiteral("…") + t.right(TranscriptChars) : t; // the most recent part
}

QString
Drafts::citeOverview(const QString &overview, const QList<Vector> &sentenceVectors, const QList<int> &candidateSources, const QList<Vector> &candidateVectors)
{
    const auto spans = Rag::splitSentences(overview);
    if (spans.size() != sentenceVectors.size() || candidateSources.size() != candidateVectors.size() || candidateVectors.isEmpty())
        return overview;
    QString out = overview;
    for (qsizetype k = spans.size() - 1; k >= 0; --k) { // from the end, so earlier offsets stay valid
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

QString Drafts::summaryMarkdown(const QString &projectTitle,
                                const QString &overview,
                                const QList<SourceSummary> &sources,
                                const QList<ProjectItem> &clips,
                                int articlesInProject,
                                const QDate &today)
{
    Q_UNUSED(projectTitle)
    QStringList md;
    // two counts in one sentence: each is a plural phrase of its own, put into the sentence
    const QString articles = i18ncp("@info used as %2 in \"Generated %1 from %2 and %3\"", "%1 article", "%1 articles", sources.size());
    const QString excerpts = i18ncp("@info used as %3 in \"Generated %1 from %2 and %3\"", "%1 saved excerpt", "%1 saved excerpts", clips.size());
    md << i18nc("@info keep the asterisks (Markdown italics); %1 is a date, %2 a number of articles, %3 a number of saved excerpts",
                "*Generated %1 from %2 and %3. Written by a small AI: check it against the sources.*",
                today.toString(Qt::ISODate),
                articles,
                excerpts)
       << QString();
    if (articlesInProject > MaxArticles)
        md << QStringLiteral("> ")
                + i18ncp("@info %2 is how many articles were summarized",
                         "Only the first %2 of %1 article was summarized.",
                         "Only the first %2 of %1 articles were summarized.",
                         articlesInProject,
                         MaxArticles)
           << QString();
    if (!overview.trimmed().isEmpty())
        md << QStringLiteral("## ") + i18nc("@title heading in a document the app writes", "Overview") << QString() << overview.trimmed() << QString();
    if (!sources.isEmpty()) {
        md << QStringLiteral("## ") + i18nc("@title heading in a document the app writes", "Sources") << QString();
        for (int i = 0; i < sources.size(); ++i) {
            const SourceSummary &s = sources.at(i);
            md << QStringLiteral("### %1. %2").arg(i + 1).arg(Projects::articleLink(s.article)) << QString() << s.summary.trimmed() << QString();
            if (!s.note.isEmpty())
                md << QStringLiteral("> ") + i18nc("@info keep the asterisks (Markdown bold)", "**Your note:** %1", s.note) << QString();
        }
    }
    if (!clips.isEmpty()) {
        md << QStringLiteral("## ") + i18nc("@title heading in a document the app writes", "Saved excerpts") << QString();
        for (auto it = clips.crbegin(); it != clips.crend(); ++it) { // oldest first
            md << Projects::quote(*it);
            if (!it->note.isEmpty())
                md << QString() << i18nc("@info keep the asterisks (Markdown bold)", "**Note:** %1", it->note);
            md << QString();
        }
    }
    if (!sources.isEmpty()) {
        md << QStringLiteral("## ") + i18nc("@title heading in a document the app writes", "References") << QString();
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
    return lines.isEmpty() ? QString()
                           : QStringLiteral("## ") + i18nc("@title heading in a document the app writes", "Sources discussed") + QStringLiteral("\n\n")
            + lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// ---- a report written to the user's instructions ----

QString Drafts::planPrompt(const ReportRequest &request, const QString &goal, const QStringList &sourceTitles)
{
    QString prompt = QStringLiteral("You are planning a written report.\n\nTitle: ") + request.title.trimmed()
        + QStringLiteral("\n\nThe user's instructions:\n") + request.instructions.trimmed() + QStringLiteral("\n\n");
    if (!goal.trimmed().isEmpty())
        prompt += QStringLiteral("The research project's goal: ") + goal.trimmed() + QStringLiteral("\n\n");
    if (!sourceTitles.isEmpty())
        prompt += QStringLiteral("Sources available: ") + sourceTitles.mid(0, 40).join(QStringLiteral("; ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral(
                  "Write an outline of at most %1 sections that does what the instructions ask, in the order they ask for. "
                  "One section per line, in this form:\nSection heading: one sentence on what the section covers\n"
                  "Write nothing else: no introduction, no numbering, no bullet marks.")
                  .arg(request.sections);
    return prompt;
}

QList<Drafts::PlannedSection> Drafts::parsePlan(const QString &text, int maxSections)
{
    static const QRegularExpression marks(QStringLiteral("^(#+|[-*\\x{2022}]|\\d+[.)]|section\\s+\\d+[:.]?)\\s*"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression split(QStringLiteral("\\s*(:|\\s[\\x{2014}\\x{2013}-]\\s)\\s*"));
    QList<PlannedSection> out;
    QSet<QString> seen;
    for (const auto qLatin1Char = text.split(QLatin1Char('\n')); QString line : qLatin1Char) {
        line = line.trimmed();
        for (int i = 0; i < 3; ++i) // "1. **Heading**" has two marks
            line.remove(marks);
        line.remove(QStringLiteral("**"));
        line = line.trimmed();
        if (line.isEmpty() || (line.endsWith(QLatin1Char(':')) && line.size() < 30)) // "Outline:"
            continue;
        PlannedSection s;
        const QRegularExpressionMatch m = split.match(line);
        if (m.hasMatch() && m.capturedStart() > 0) {
            s.heading = line.left(m.capturedStart()).trimmed();
            s.brief = line.mid(m.capturedEnd()).trimmed();
        } else {
            s.heading = line;
        }
        if (s.heading.size() > 100 || s.heading.compare(QLatin1String("outline"), Qt::CaseInsensitive) == 0)
            continue;
        if (seen.contains(s.heading.toLower()))
            continue;
        seen.insert(s.heading.toLower());
        out.append(s);
        if (out.size() >= maxSections)
            break;
    }
    return out;
}

QString Drafts::sectionPrompt(const ReportRequest &request, const PlannedSection &section, const QStringList &otherHeadings, const QString &sources)
{
    QString prompt = QStringLiteral("You are writing one section of a report titled \"") + request.title.trimmed()
        + QStringLiteral("\".\n\nThe user's instructions for the report:\n") + request.instructions.trimmed() + QStringLiteral("\n\nThis section: \"")
        + section.heading + QStringLiteral("\"");
    if (!section.brief.isEmpty())
        prompt += QStringLiteral(" (") + section.brief + QStringLiteral(")");
    prompt += QStringLiteral("\n\n");
    if (!otherHeadings.isEmpty())
        prompt +=
            QStringLiteral("The report's other sections (leave their topics to them): ") + otherHeadings.join(QStringLiteral("; ")) + QStringLiteral("\n\n");
    prompt += QStringLiteral("Sources:\n\n") + sources
        + QStringLiteral(
              "\n\nWrite this section's text in Markdown, about %1 words: paragraphs, and bullet points or a table only where they help. "
              "Follow the user's instructions on tone and detail. Use only facts from the sources above, and copy figures and "
              "dates exactly as they are written there; if the sources do not cover something this section needs, say so in one "
              "sentence. Stay on this section's topic: no introduction, and no summary or conclusion unless this section is the "
              "conclusion. Do not write the section heading, other headings, or source numbers.")
              .arg(request.words);
    return prompt;
}

QString Drafts::dropUnfinishedSentence(const QString &text)
{
    // a section that ran out of tokens stops mid-sentence ("built over at least 400,000 years of andesite,")
    static const QRegularExpression finished(QStringLiteral("[.!?:)\"\\x{201D}*|]\\s*$"));
    if (text.isEmpty() || finished.match(text).hasMatch())
        return text;
    const qsizetype lastBreak = text.lastIndexOf(QLatin1Char('\n'));
    static const QRegularExpression sentenceEnd(QStringLiteral("[.!?](\"|\\x{201D})?\\s"));
    qsizetype cut = -1;
    for (auto it = sentenceEnd.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() > lastBreak)
            cut = m.capturedStart() + m.capturedLength(0) - 1;
    }
    if (cut > 0)
        return text.left(cut).trimmed();
    return lastBreak > 0 ? text.left(lastBreak).trimmed() : text; // the whole last line was unfinished
}

QString Drafts::sectionQuery(const ReportRequest &request, const PlannedSection &section)
{
    // the instructions' first sentence says what the report is about ("Portland's bridges over the Willamette River"):
    // without it, a section headed "The Newest Crossing" finds bridges in Portland, Maine
    static const QRegularExpression sentenceEnd(QStringLiteral("[.!?\\n]"));
    QString first = request.instructions.trimmed();
    const qsizetype end = first.indexOf(sentenceEnd);
    if (end > 0)
        first = first.left(end);
    return QString(section.heading + QLatin1Char(' ') + section.brief + QLatin1Char(' ') + request.title + QLatin1Char(' ') + first.left(200)).simplified();
}

QString Drafts::citeSection(const QString &text, const QList<Rag::Citation> &citations, const QList<int> &reportNumbers)
{
    QString out = text;
    QList<Rag::Citation> sorted = citations;
    std::sort(sorted.begin(), sorted.end(), [](const Rag::Citation &a, const Rag::Citation &b) {
        return a.end > b.end; // from the end, so earlier offsets stay valid
    });
    for (const Rag::Citation &c : std::as_const(sorted)) {
        if (c.end > out.size())
            continue;
        // a mark after a table row or a heading would break it
        const qsizetype lineStart = out.lastIndexOf(QLatin1Char('\n'), qMax(0, c.start - 1)) + 1;
        const QString line = out.mid(lineStart, c.start - lineStart + 2).trimmed();
        if (line.startsWith(QLatin1Char('|')) || line.startsWith(QLatin1Char('#')))
            continue;
        // a label ("**Key facts**") is not a claim
        const QString sentence = out.mid(c.start, c.end - c.start);
        if (sentence.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() < 5 && c.reason == QLatin1String("topic"))
            continue;
        if (!c.reason.isEmpty()) { // no source clearly backs it, or a figure no source has: check it
            out.insert(c.end, QStringLiteral(" [?]"));
            continue;
        }
        if (c.sources.isEmpty())
            continue;
        QString marks;
        for (int n : c.sources)
            if (n >= 1 && n <= reportNumbers.size() && !marks.contains(QStringLiteral("[%1]").arg(reportNumbers.at(n - 1))))
                marks += QStringLiteral(" [%1]").arg(reportNumbers.at(n - 1));
        out.insert(c.end, marks);
    }
    return out;
}

QString Drafts::renumberCitations(const QString &markdown, QList<int> *order)
{
    static const QRegularExpression mark(QStringLiteral("\\[(\\d+)\\]"));
    QHash<int, int> renumbered;
    QString out;
    qsizetype last = 0;
    for (auto it = mark.globalMatch(markdown); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const int old = m.captured(1).toInt();
        if (!renumbered.contains(old)) {
            renumbered.insert(old, int(renumbered.size()) + 1);
            if (order)
                order->append(old);
        }
        out += markdown.mid(last, m.capturedStart() - last) + QStringLiteral("[%1]").arg(renumbered.value(old));
        last = m.capturedEnd();
    }
    return out + markdown.mid(last);
}

QString Drafts::reportMarkdown(const ReportRequest &request,
                               const QList<std::pair<QString, QString>> &sections,
                               const QList<ReportSource> &references,
                               const QDate &today)
{
    QStringList md;
    md << i18nc("@info keep the asterisks (Markdown italics) and [n], [?] as they are; %1 is a date",
                "*Written by a small AI on %1, following the instructions below. Check it against the sources: [n] names the source "
                "a sentence matches, and [?] marks one that no source clearly backs, or a figure none of them contains.*",
                today.toString(Qt::ISODate))
       << QString();
    QStringList quoted;
    for (const auto qLatin1Char = request.instructions.trimmed().split(QLatin1Char('\n')); const QString &l : qLatin1Char)
        quoted << QStringLiteral("> ") + l;
    md << QStringLiteral("> ") + i18nc("@title keep the asterisks (Markdown bold)", "**Instructions:**") << QStringLiteral(">") << quoted << QString();
    for (const auto &[heading, body] : sections)
        md << QStringLiteral("## ") + heading << QString() << body.trimmed() << QString();
    if (!references.isEmpty()) {
        md << QStringLiteral("## ") + i18nc("@title heading in a document the app writes", "References") << QString();
        for (int i = 0; i < references.size(); ++i) {
            const ReportSource &r = references.at(i);
            md << QStringLiteral("%1. %2").arg(i + 1).arg(!r.label.isEmpty()            ? r.label
                                                              : r.article.libraryId > 0 ? Projects::articleLink(r.article)
                                                                                        : QString());
        }
        md << QString();
    }
    return md.join(QLatin1Char('\n'));
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
    const QJsonObject body = Rag::completionRequest(m_models.answer, prompt, numPredict, m_think);
    auto text = std::make_shared<QString>();
    const quint64 generation = m_generation;
    m_reply = m_client->chat(
        body,
        this,
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
        finish(QStringLiteral("error"), i18n("This project no longer exists."));
        return;
    }
    m_project = *project;
    m_articles = Projects::items(projectId, QStringLiteral("article"));
    std::reverse(m_articles.begin(), m_articles.end()); // in the order they were added
    m_clips = Projects::items(projectId, QStringLiteral("clip"));
    if (m_articles.isEmpty() && m_clips.isEmpty()) {
        finish(QStringLiteral("error"), i18n("Add some articles or excerpts to this project first."));
        return;
    }
    m_title = i18nc("@title document title, %1 is a project title", "Summary: %1", m_project.title);
    const int total = int(qMin<qsizetype>(m_articles.size(), Drafts::MaxArticles)) + 1;
    Q_EMIT progress(0, total, i18nc("@info:progress", "Reading the articles"));

    QList<qint64> libraries;
    QList<ArticleRef> refs;
    for (const auto mid = m_articles.mid(0, Drafts::MaxArticles); const ProjectItem &a : mid) {
        refs << a.article;
        if (!libraries.contains(a.article.libraryId))
            libraries << a.article.libraryId;
    }
    ZimSearch::prepare(libraries); // archives opened here: opening reads the database
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
                for (const auto extract = Extract::extract(content->data).passages; const Passage &p : extract)
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
    Q_EMIT progress(m_next + 1, total, i18nc("@info:progress", "Summarizing “%1”", article.title));
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
        finish(QStringLiteral("error"), i18n("None of the project's articles could be read."));
        return;
    }
    Q_EMIT progress(total, total, i18nc("@info:progress", "Writing the overview"));
    QStringList digest;
    for (const Drafts::SourceSummary &s : std::as_const(m_summaries))
        digest << s.article.title + QStringLiteral(": ") + s.summary;
    QStringList quotes; // model prompt material: stays English
    for (const auto mid = m_clips.mid(0, 6); const ProjectItem &c : mid)
        quotes << QStringLiteral("Saved excerpt (%1): %2")
                      .arg(c.refs.value(0).title.isEmpty() ? QStringLiteral("note") : c.refs.value(0).title, c.text.trimmed().left(600));
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
        m_reply = m_client->embed(m_models.embedding,
                                  input,
                                  this,
                                  [this, generation, overview, sources, candidates, done](const QList<Vector> &vectors, const QString &error) {
                                      if (generation != m_generation)
                                          return;
                                      if (!error.isEmpty()) { // citations are a bonus
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
        finish(QStringLiteral("error"), i18n("This chat no longer exists."));
        return;
    }
    QList<std::pair<QString, QString>> pairs;
    QList<ArticleRef> sources;
    QString question;
    for (const auto messages = Chats::messages(chatId); const MessageRecord &m : messages) {
        if (m.role == QLatin1String("user")) {
            question = m.content;
            continue;
        }
        if (m.content.trimmed().isEmpty() || question.isEmpty())
            continue;
        pairs.append({question, m.content});
        question.clear();
        for (const auto array = QJsonDocument::fromJson(m.sources).array(); const QJsonValue &v : array) {
            const QJsonObject s = v.toObject();
            if (s.value(QLatin1String("kind")).toString() != QLatin1String("clip"))
                sources << ArticleRef{qint64(s.value(QLatin1String("libraryId")).toDouble()),
                                      s.value(QLatin1String("path")).toString(),
                                      s.value(QLatin1String("title")).toString()};
        }
    }
    if (pairs.isEmpty()) {
        finish(QStringLiteral("error"), i18n("That chat has no answers to summarize yet."));
        return;
    }
    m_title = i18nc("@title document title, %1 is a chat title", "Chat summary: %1", chat->title);
    Q_EMIT progress(1, 1, i18nc("@info:progress", "Writing notes on the chat"));
    m_client->capabilities(models.answer, this, [this, generation, pairs, sources](const QStringList &capabilities, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        m_think = capabilities.contains(QStringLiteral("thinking"));
        complete(
            Drafts::chatPrompt(Drafts::transcript(pairs)),
            600,
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

// ---- a report ----

void Drafter::writeReport(qint64 projectId, const Drafts::ReportRequest &request, const QList<qint64> &libraries, const Models &models)
{
    stop();
    const quint64 generation = ++m_generation;
    m_models = models;
    m_running = true;
    m_projectId = projectId;
    m_request = request;
    m_libraries = libraries;
    m_read.clear();
    m_reportClips.clear();
    m_plan.clear();
    m_section = 0;
    m_written.clear();
    m_reportSources.clear();
    m_title = request.title.trimmed();
    const auto project = Projects::byId(projectId);
    if (!project) {
        finish(QStringLiteral("error"), i18n("This project no longer exists."));
        return;
    }
    if (m_title.isEmpty() || request.instructions.trimmed().isEmpty()) {
        finish(QStringLiteral("error"), i18n("Give the report a title and say what it should cover."));
        return;
    }
    m_project = *project;
    m_articles = Projects::items(projectId, QStringLiteral("article"));
    std::reverse(m_articles.begin(), m_articles.end());
    m_articles = m_articles.mid(0, Drafts::MaxReportArticles);
    const QString model = Embedding::normalizeName(models.embedding);
    for (const auto qStringLiteral = Projects::items(projectId, QStringLiteral("clip")); const ProjectItem &i : qStringLiteral) {
        Rag::Clip c;
        c.id = i.id;
        c.title = i.refs.value(0).title;
        c.section = i.section;
        c.text = i.text;
        c.libraryId = i.article.libraryId;
        c.path = i.article.path;
        if (i.embedModel == model)
            c.vector = i.embedding;
        m_reportClips << c;
    }
    if (m_articles.isEmpty() && m_reportClips.isEmpty())
        m_request.alsoLibrary = true; // nothing in the project yet: the library is all there is
    if (m_request.alsoLibrary && m_libraries.isEmpty() && m_articles.isEmpty() && m_reportClips.isEmpty()) {
        finish(QStringLiteral("error"), i18n("There is nothing to write from: add articles to the project, or install a library."));
        return;
    }
    Q_EMIT progress(0, m_request.sections + 1, i18nc("@info:progress", "Reading the project's articles"));

    QList<qint64> open = m_request.alsoLibrary ? m_libraries : QList<qint64>();
    QList<ArticleRef> refs;
    for (const ProjectItem &a : std::as_const(m_articles)) {
        refs << a.article;
        if (!open.contains(a.article.libraryId))
            open << a.article.libraryId;
    }
    ZimSearch::prepare(open); // archives opened here: opening reads the database
    m_client->capabilities(models.answer, this, [this, generation, refs](const QStringList &capabilities, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        m_think = capabilities.contains(QStringLiteral("thinking"));
        auto *watcher = new QFutureWatcher<QList<ZimSearch::Article>>(this);
        connect(watcher, &QFutureWatcher<QList<ZimSearch::Article>>::finished, this, [this, watcher, generation] {
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            m_read = watcher->result();
            planReport();
        });
        watcher->setFuture(QtConcurrent::run([refs] {
            QList<ZimSearch::Article> out;
            for (const ArticleRef &r : refs) {
                const auto content = ZimReader::content(r.libraryId, r.path);
                if (!content)
                    continue;
                ZimSearch::Article a;
                a.found.libraryId = r.libraryId;
                a.found.path = content->path;
                a.found.title = r.title;
                a.passages = Extract::extract(content->data).passages;
                if (!a.passages.isEmpty())
                    out << a;
            }
            return out;
        }));
    });
}

void Drafter::planReport()
{
    Q_EMIT progress(0, m_request.sections + 1, i18nc("@info:progress", "Planning the sections"));
    QStringList titles;
    for (const ZimSearch::Article &a : std::as_const(m_read))
        titles << a.found.title;
    for (const Rag::Clip &c : std::as_const(m_reportClips))
        if (!c.title.isEmpty() && !titles.contains(c.title))
            titles << c.title;
    complete(Drafts::planPrompt(m_request, m_project.description, titles), 300, [this](const QString &text, const QString &error) {
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        m_plan = Drafts::parsePlan(Rag::tidyAnswer(text), m_request.sections);
        if (m_plan.isEmpty()) // no usable outline: one section doing what was asked
            m_plan = {{m_request.title.trimmed(), QString()}};
        writeSection();
    });
}

void Drafter::writeSection()
{
    if (m_section >= m_plan.size()) {
        finishReport();
        return;
    }
    const Drafts::PlannedSection section = m_plan.at(m_section);
    Q_EMIT progress(m_section + 1, int(m_plan.size()) + 1, i18nc("@info:progress %1 is a section heading", "Writing “%1”", section.heading));
    const QString query = Drafts::sectionQuery(m_request, section);
    const QList<Rag::Candidate> own = Rag::pickPassages(query, Rag::chooseArticles(query, m_read));
    if (!m_request.alsoLibrary || m_libraries.isEmpty()) {
        sectionSources(own, {});
        return;
    }
    // and from the library: articles outside the project, read in the worker
    QSet<QString> mine;
    for (const ZimSearch::Article &a : std::as_const(m_read))
        mine.insert(QString::number(a.found.libraryId) + QLatin1Char('/') + a.found.path);
    const quint64 generation = m_generation;
    auto *watcher = new QFutureWatcher<QList<Rag::Candidate>>(this);
    connect(watcher, &QFutureWatcher<QList<Rag::Candidate>>::finished, this, [this, watcher, generation, own] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        sectionSources(own, watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([query, libraries = m_libraries, mine] {
        QList<ZimSearch::Article> outside;
        for (const auto readArticles = ZimSearch::readArticles(query, libraries, Drafts::ReportLibraryArticles); const ZimSearch::Article &a : readArticles)
            if (!mine.contains(QString::number(a.found.libraryId) + QLatin1Char('/') + a.found.path))
                outside << a;
        return Rag::pickPassages(query, outside).mid(0, Rag::MaxEmbed / 2);
    }));
}

void Drafter::sectionSources(const QList<Rag::Candidate> &ownPool, const QList<Rag::Candidate> &libraryPool)
{
    const Drafts::PlannedSection section = m_plan.at(m_section);
    const QString query = Drafts::sectionQuery(m_request, section);
    // one request: the section's query, the passages, and the excerpts with no vector from this model yet
    QStringList input{Embedding::formatQuery(m_models.embedding, query)};
    for (const Rag::Candidate &c : ownPool)
        input << Embedding::formatDocument(m_models.embedding, c.text.left(3000));
    for (const Rag::Candidate &c : libraryPool)
        input << Embedding::formatDocument(m_models.embedding, c.text.left(3000));
    QList<int> unembedded;
    for (int i = 0; i < m_reportClips.size(); ++i)
        if (m_reportClips.at(i).vector.isEmpty()) {
            unembedded << i;
            input << Embedding::formatDocument(m_models.embedding, m_reportClips.at(i).text.left(3000));
        }
    const quint64 generation = m_generation;
    m_reply = m_client->embed(
        m_models.embedding,
        input,
        this,
        [this, generation, ownPool = QList<Rag::Candidate>(ownPool), libraryPool = QList<Rag::Candidate>(libraryPool), unembedded](
            const QList<Vector> &vectors,
            const QString &error) mutable {
            if (generation != m_generation)
                return;
            Vector question;
            if (error.isEmpty() && vectors.size() == 1 + ownPool.size() + libraryPool.size() + unembedded.size()) {
                int k = 0;
                question = vectors.at(k++);
                for (Rag::Candidate &c : ownPool)
                    c.vector = vectors.at(k++);
                for (Rag::Candidate &c : libraryPool)
                    c.vector = vectors.at(k++);
                for (int i : std::as_const(unembedded)) {
                    m_reportClips[i].vector = vectors.at(k);
                    Projects::setEmbedding(m_reportClips.at(i).id, vectors.at(k++), Embedding::normalizeName(m_models.embedding));
                }
            }
            m_context = Rag::buildContext(Rag::rank(ownPool, question), false, Rag::rankClips(m_reportClips, question), Rag::rank(libraryPool, question));
            const Drafts::PlannedSection section = m_plan.at(m_section);
            Q_EMIT textAdded((m_section ? QStringLiteral("\n\n## ") : QStringLiteral("## ")) + section.heading + QStringLiteral("\n\n"));
            if (m_context.sources.isEmpty()) {
                const QString none = i18nc("@info keep the asterisks (Markdown italics)", "*The sources have nothing on this.*");
                Q_EMIT textAdded(none);
                m_written.append({section.heading, none});
                ++m_section;
                writeSection();
                return;
            }
            QStringList others;
            for (const Drafts::PlannedSection &s : std::as_const(m_plan))
                if (s.heading != section.heading)
                    others << s.heading;
            complete(
                Drafts::sectionPrompt(m_request, section, others, m_context.text),
                qBound(400, m_request.words * 3 + 200, 1200),
                [this](const QString &text, const QString &error) {
                    if (!error.isEmpty()) {
                        finish(QStringLiteral("error"), error);
                        return;
                    }
                    sectionWritten(text);
                },
                true);
        });
}

void Drafter::sectionWritten(const QString &raw)
{
    // the model's own headings inside a section become subsections; one repeating the section's heading at the top goes
    QStringList lines = Rag::tidyAnswer(raw).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && (lines.first().trimmed().isEmpty() || lines.first().trimmed().startsWith(QLatin1Char('#'))))
        lines.removeFirst();
    for (QString &l : lines)
        if (l.startsWith(QLatin1String("# ")) || l.startsWith(QLatin1String("## ")))
            l = QStringLiteral("### ") + l.section(QLatin1Char(' '), 1);
    const QString text = Drafts::dropUnfinishedSentence(lines.join(QLatin1Char('\n')).trimmed());
    const Drafts::PlannedSection section = m_plan.at(m_section);
    // the report-wide number of each of this section's sources
    QList<int> numbers;
    for (const Rag::Source &src : std::as_const(m_context.sources)) {
        Drafts::ReportSource r;
        r.article = {src.libraryId, src.path, src.title};
        if (src.kind == QLatin1String("clip"))
            r.label = src.libraryId > 0
                ? i18nc("@item a reference, %1 is a link to the article a saved excerpt came from", "%1 (saved excerpt)", Projects::articleLink(r.article))
                : i18nc("@item a reference, %1 is the start of a saved excerpt", "Saved excerpt: “%1”", src.snippet.left(80).simplified());
        int found = 0;
        for (int i = 0; i < m_reportSources.size(); ++i) {
            const Drafts::ReportSource &o = m_reportSources.at(i);
            if (o.article.libraryId == r.article.libraryId && o.article.path == r.article.path && (r.article.libraryId > 0 || o.label == r.label))
                found = i + 1;
        }
        if (!found) {
            m_reportSources.append(r);
            found = int(m_reportSources.size());
        }
        numbers << found;
    }
    const auto spans = Rag::citableSentences(text);
    auto done = [this, section, text, numbers](const QList<Rag::Citation> &citations) {
        m_written.append({section.heading, Drafts::citeSection(text, citations, numbers)});
        ++m_section;
        writeSection();
    };
    if (spans.isEmpty()) {
        done({});
        return;
    }
    QStringList input;
    for (const auto &[a, b] : spans)
        input << Embedding::formatDocument(m_models.embedding, text.mid(a, b - a));
    const quint64 generation = m_generation;
    m_reply = m_client->embed(m_models.embedding, input, this, [this, generation, text, spans, done](const QList<Vector> &vectors, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty() || vectors.size() != spans.size()) { // citations are a bonus
            done({});
            return;
        }
        done(Rag::attribute(text, spans, vectors, m_context.sources));
    });
}

void Drafter::finishReport()
{
    // number the sources in the order the report first cites them, and list only those
    QStringList bodies;
    for (const auto &[heading, body] : std::as_const(m_written))
        bodies << body;
    const QString separator = QStringLiteral("\n\u0001\n"); // no section holds it
    QList<int> order;
    const QStringList renumbered = Drafts::renumberCitations(bodies.join(separator), &order).split(separator);
    QList<std::pair<QString, QString>> sections;
    for (int i = 0; i < m_written.size(); ++i)
        sections.append({m_written.at(i).first, renumbered.value(i)});
    QList<Drafts::ReportSource> references;
    for (int old : std::as_const(order))
        if (old >= 1 && old <= m_reportSources.size())
            references << m_reportSources.at(old - 1);
    m_markdown = Drafts::reportMarkdown(m_request, sections, references, QDate::currentDate());
    finish(QStringLiteral("done"));
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
