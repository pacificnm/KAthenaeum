// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "core/drafts.h"

// The pure parts of AI drafts (Athenaeum's research.py): prompts, transcript, overview citations, the summary document
class DraftsTest : public QObject
{
    Q_OBJECT

    static Vector unit(int axis)
    {
        Vector v(4, 0.0f);
        v[axis] = 1.0f;
        return v;
    }

private Q_SLOTS:
    void theOverviewPromptForbidsRestatingTheQuestion()
    {
        const QString p = Drafts::overviewPrompt(QStringLiteral("Water: polar."), QStringLiteral("Why is water a solvent?"));
        QVERIFY(p.contains(QLatin1String("The research question is: Why is water a solvent?")));
        QVERIFY(p.contains(QLatin1String("Do NOT restate or paraphrase the question")));
        QVERIFY(p.contains(QLatin1String("do not write source numbers")));
        QVERIFY(Drafts::articlePrompt(QStringLiteral("Water"), QStringLiteral("text")).startsWith(QLatin1String("Article: Water\n\ntext\n\n")));
        QVERIFY(Drafts::chatPrompt(QStringLiteral("Q: a")).contains(QLatin1String("'## Key findings' (bullet points)")));
    }

    void theTranscriptKeepsTheMostRecentPartAndNoMarkers()
    {
        QCOMPARE(Drafts::transcript({{QStringLiteral("Why?"), QStringLiteral("Because [1].")}, {QStringLiteral("And?"), QStringLiteral("So.")}}),
                 QStringLiteral("Q: Why?\nA: Because.\n\nQ: And?\nA: So."));
        QList<std::pair<QString, QString>> many;
        for (int i = 0; i < 200; ++i)
            many.append({QStringLiteral("Question %1?").arg(i), QStringLiteral("An answer that is long enough to add up quickly, number %1.").arg(i)});
        const QString t = Drafts::transcript(many);
        QCOMPARE(t.size(), Drafts::TranscriptChars + 1);
        QVERIFY(t.startsWith(QStringLiteral("…")));
        QVERIFY(t.endsWith(QLatin1String("number 199.")));
    }

    void overviewSentencesAreNumberedByTheirClosestSource()
    {
        const QString overview = QStringLiteral("Water is polar and bends. Ice floats on liquid water. The material does not cover salt at all.");
        // sentence 1 near source 2, sentence 2 near nothing, sentence 3 is a refusal (close to source 1, but not cited)
        Vector half = unit(0);
        half[3] = 1.2f; // cosine with unit(0) ~0.64: below the threshold
        const QString cited = Drafts::citeOverview(overview, {unit(1), half, unit(0)}, {1, 2}, {unit(0), unit(1)});
        QCOMPARE(cited, QStringLiteral("Water is polar and bends. [2] Ice floats on liquid water. The material does not cover salt at all."));
        // a mismatch in counts is not guessed at
        QCOMPARE(Drafts::citeOverview(overview, {unit(1)}, {1}, {unit(0)}), overview);
    }

    void theSummaryDocument()
    {
        Drafts::SourceSummary water{{1, QStringLiteral("Water"), QStringLiteral("Water")}, QStringLiteral("Water is polar."), QStringLiteral("start here")};
        Drafts::SourceSummary ice{{1, QStringLiteral("Ice_(film)"), QStringLiteral("Ice (film)")}, QStringLiteral("A film."), QString()};
        ProjectItem clip;
        clip.text = QStringLiteral("Water bends.");
        clip.refs = {{1, QStringLiteral("Water"), QStringLiteral("Water")}};
        clip.note = QStringLiteral("key");
        const QString md = Drafts::summaryMarkdown(QStringLiteral("P"), QStringLiteral("Water is polar. [1]"), {water, ice}, {clip}, 20, QDate(2026, 10, 19));
        QVERIFY2(md.startsWith(QStringLiteral(
                     "*Generated 2026-10-19 from 2 articles and 1 saved excerpt. Written by a small AI: check it against the sources.*\n\n"
                     "> Only the first 15 of 20 articles were summarized.\n\n## Overview\n\nWater is polar. [1]\n\n## Sources\n\n"
                     "### 1. [Water](zim://lib1/Water)\n\nWater is polar.\n\n> **Your note:** start here\n\n"
                     "### 2. [Ice (film)](zim://lib1/Ice_%28film%29)\n\nA film.\n\n## Saved excerpts\n\n> Water bends.\n>\n> — Water\n\n**Note:** key\n\n"
                     "## References\n\n1. [Water](zim://lib1/Water)\n2. [Ice (film)](zim://lib1/Ice_%28film%29)\n")),
                 qPrintable(md));
        // nothing summarized: no empty sections
        const QString bare = Drafts::summaryMarkdown(QStringLiteral("P"), QString(), {}, {clip}, 0, QDate(2026, 10, 19));
        QVERIFY(!bare.contains(QLatin1String("## Overview")) && !bare.contains(QLatin1String("## Sources")) && !bare.contains(QLatin1String("## References")));
        QVERIFY(bare.contains(QLatin1String("from 0 articles and 1 saved excerpt")));
    }

    void topLevelHeadingsBecomeSections()
    {
        QCOMPARE(Drafts::demoteHeadings(QStringLiteral("# Questions explored\n* a\n## Key findings\n#not a heading\n```\n# code\n```")),
                 QStringLiteral("## Questions explored\n* a\n## Key findings\n#not a heading\n```\n# code\n```"));
    }

    void sourcesDiscussedOnceEach()
    {
        QCOMPARE(Drafts::sourcesDiscussed({{1, QStringLiteral("Water"), QStringLiteral("Water")},
                                           {1, QStringLiteral("Water"), QStringLiteral("Water")},
                                           {0, QString(), QStringLiteral("Saved answer")},
                                           {2, QStringLiteral("Ice"), QStringLiteral("Ice")}}),
                 QStringLiteral("## Sources discussed\n\n- [Water](zim://lib1/Water)\n- [Ice](zim://lib2/Ice)\n"));
        QCOMPARE(Drafts::sourcesDiscussed({}), QString());
    }

    // ---- a report written to the user's instructions ----

    void outlinesAreReadInTheFormsModelsWriteThem()
    {
        const QString plan = QStringLiteral(
            "Outline:\n"
            "1. Mount Mazama: the volcano before the eruption\n"
            "2) **The eruption** - what happened 7,700 years ago\n"
            "## The Caldera \u2014 how the lake filled\n"
            "- Water clarity\n"
            "* Section 5: Research: studies since\n"
            "\n"
            "1. Mount Mazama: said twice\n"
            "Conclusion");
        const auto sections = Drafts::parsePlan(plan, 6);
        QCOMPARE(sections.size(), 6);
        QCOMPARE(sections[0].heading, QStringLiteral("Mount Mazama"));
        QCOMPARE(sections[0].brief, QStringLiteral("the volcano before the eruption"));
        QCOMPARE(sections[1].heading, QStringLiteral("The eruption"));
        QCOMPARE(sections[1].brief, QStringLiteral("what happened 7,700 years ago"));
        QCOMPARE(sections[2].heading, QStringLiteral("The Caldera"));
        QCOMPARE(sections[3].heading, QStringLiteral("Water clarity"));
        QCOMPARE(sections[3].brief, QString());
        QCOMPARE(sections[4].heading, QStringLiteral("Research"));
        QCOMPARE(sections[5].heading, QStringLiteral("Conclusion")); // the repeated heading was dropped
        QCOMPARE(Drafts::parsePlan(plan, 2).size(), 2);
        QVERIFY(Drafts::parsePlan(QString(), 5).isEmpty());
    }

    void theSectionPromptCarriesTheInstructions()
    {
        Drafts::ReportRequest r;
        r.title = QStringLiteral("Crater Lake");
        r.instructions = QStringLiteral("Formal tone. Explain the geology for a general reader.");
        r.words = 300;
        const QString p = Drafts::sectionPrompt(r,
                                                {QStringLiteral("The eruption"), QStringLiteral("what happened")},
                                                {QStringLiteral("Mount Mazama"), QStringLiteral("Water clarity")},
                                                QStringLiteral("[1] Crater Lake\n..."));
        QVERIFY(p.contains(r.instructions));
        QVERIFY(p.contains(QStringLiteral("\"The eruption\" (what happened)")));
        QVERIFY(p.contains(QStringLiteral("Mount Mazama; Water clarity")));
        QVERIFY(p.contains(QStringLiteral("about 300 words")));
        QVERIFY(p.contains(QStringLiteral("Use only facts from the sources")));
        QVERIFY(Drafts::planPrompt(r, QStringLiteral("goal"), {QStringLiteral("Crater Lake")}).contains(QStringLiteral("Sources available: Crater Lake")));
        r.instructions = QStringLiteral("A briefing on Portland's bridges over the Willamette River. Plain language.");
        QCOMPARE(Drafts::sectionQuery(r, {QStringLiteral("The Newest Crossing"), QStringLiteral("the most recent bridge")}),
                 QStringLiteral("The Newest Crossing the most recent bridge Crater Lake A briefing on Portland's bridges over the Willamette River"));
    }

    void sectionsAreCitedWithReportWideNumbers()
    {
        const QString text = QStringLiteral("The lake is deep. It formed in a caldera. Nobody knows exactly why it is so blue.");
        QList<Rag::Citation> c(3);
        c[0] = {0, 17, {1}, 0.8, {}, {}};
        c[1] = {18, 41, {2, 1}, 0.8, {}, {}};
        c[2] = {42, 81, {}, 0.5, QStringLiteral("topic"), {}}; // flagged: to check
        // this section's sources 1 and 2 are the report's 4 and 7
        QCOMPARE(Drafts::citeSection(text, c, {4, 7}),
                 QStringLiteral("The lake is deep. [4] It formed in a caldera. [7] [4] Nobody knows exactly why it is so blue. [?]"));
        // a short label is not flagged as unsupported
        QList<Rag::Citation> label{{0, 13, {}, 0.5, QStringLiteral("topic"), {}}};
        QCOMPARE(Drafts::citeSection(QStringLiteral("**Key facts**"), label, {}), QStringLiteral("**Key facts**"));
        // never after a table row or a heading (it would break them)
        const QString table = QStringLiteral("| Depth | 594 m |\n### Facts\nDeep.");
        QList<Rag::Citation> t(3);
        t[0] = {2, 17, {1}, 0.8, {}, {}};
        t[1] = {22, 27, {1}, 0.8, {}, {}};
        t[2] = {28, 33, {1}, 0.8, {}, {}};
        QCOMPARE(Drafts::citeSection(table, t, {1}), QStringLiteral("| Depth | 594 m |\n### Facts\nDeep. [1]"));
    }

    void anUnfinishedLastSentenceIsDropped()
    {
        QCOMPARE(Drafts::dropUnfinishedSentence(QStringLiteral("It is deep. It was built over 400,000 years of andesite,")), QStringLiteral("It is deep."));
        QCOMPARE(Drafts::dropUnfinishedSentence(QStringLiteral("**Context**\nMount Mazama was built over")), QStringLiteral("**Context**"));
        QCOMPARE(Drafts::dropUnfinishedSentence(QStringLiteral("All done.")), QStringLiteral("All done."));
        QCOMPARE(Drafts::dropUnfinishedSentence(QStringLiteral("| a | b |")), QStringLiteral("| a | b |"));
        QCOMPARE(Drafts::dropUnfinishedSentence(QStringLiteral("- **Bold item**")), QStringLiteral("- **Bold item**"));
    }

    void citationsAreRenumberedByFirstUse()
    {
        QList<int> order;
        QCOMPARE(Drafts::renumberCitations(QStringLiteral("A [7]. B [3] [7]. C [12]."), &order), QStringLiteral("A [1]. B [2] [1]. C [3]."));
        QCOMPARE(order, (QList<int>{7, 3, 12}));
    }

    void theReportDocument()
    {
        Drafts::ReportRequest r;
        r.title = QStringLiteral("Crater Lake");
        r.instructions = QStringLiteral("Two lines\nof instructions");
        const QString md = Drafts::reportMarkdown(
            r,
            {{QStringLiteral("Origins"), QStringLiteral("Text [1].")}, {QStringLiteral("Today"), QStringLiteral("More [2].")}},
            {{{1, QStringLiteral("Crater_Lake"), QStringLiteral("Crater Lake")}, {}}, {{0, {}, {}}, QStringLiteral("Saved excerpt: “deep blue”")}},
            QDate(2026, 10, 19));
        QVERIFY(md.contains(QStringLiteral("> **Instructions:**\n>\n> Two lines\n> of instructions")));
        QVERIFY(md.contains(QStringLiteral("## Origins\n\nText [1].\n\n## Today")));
        QVERIFY(md.contains(QStringLiteral("## References\n\n1. [Crater Lake](zim://lib1/Crater_Lake)\n2. Saved excerpt: “deep blue”")));
        QVERIFY(md.startsWith(QStringLiteral("*Written by a small AI on 2026-10-19")));
    }
};

QTEST_GUILESS_MAIN(DraftsTest)
#include "draftstest.moc"
