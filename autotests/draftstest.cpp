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
        half[3] = 1.2f;  // cosine with unit(0) ~0.64: below the threshold
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
        QVERIFY2(md.startsWith(QStringLiteral("*Generated 2026-10-19 from 2 articles and 1 saved excerpt. Written by a small AI: check it against the sources.*\n\n"
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
        QCOMPARE(Drafts::sourcesDiscussed({{1, QStringLiteral("Water"), QStringLiteral("Water")}, {1, QStringLiteral("Water"), QStringLiteral("Water")},
                                           {0, QString(), QStringLiteral("Saved answer")}, {2, QStringLiteral("Ice"), QStringLiteral("Ice")}}),
                 QStringLiteral("## Sources discussed\n\n- [Water](zim://lib1/Water)\n- [Ice](zim://lib2/Ice)\n"));
        QCOMPARE(Drafts::sourcesDiscussed({}), QString());
    }
};

QTEST_GUILESS_MAIN(DraftsTest)
#include "draftstest.moc"
