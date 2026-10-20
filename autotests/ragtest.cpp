// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "core/embedding.h"
#include "core/rag.h"

// Ported from Athenaeum's tests/test_rag_logic.py and the passage tests of test_zimsearch.py, plus KAthenaeum's own
// cases (the widened refusal detector, citation chips).
class RagTest : public QObject
{
    Q_OBJECT

    static QStringList spans(const QString &text)
    {
        QStringList out;
        for (const auto sentences = Rag::splitSentences(text); const auto &[a, b] : sentences)
            out << text.mid(a, b - a);
        return out;
    }

    static ZimSearch::Article article(const QString &title, const QList<Passage> &passages, qint64 libraryId = 1)
    {
        ZimSearch::Article a;
        a.found.libraryId = libraryId;
        a.found.path = title;
        a.found.title = title;
        a.passages = passages;
        return a;
    }

    static Rag::Candidate candidate(const QString &title, const QString &text = QStringLiteral("body text of the chunk"),
                                    const QString &section = {})
    {
        Rag::Candidate c;
        c.libraryId = 1;
        c.path = title;
        c.title = title;
        c.section = section;
        c.text = text;
        return c;
    }

    static Vector unit(int axis, int dims = 8)
    {
        Vector v(dims, 0.0f);
        v[axis] = 1.0f;
        return v;
    }

private Q_SLOTS:
    // ---- embedding models ----

    void embeddingModelsGetTheirOwnTaskPrefixes()
    {
        QCOMPARE(Embedding::formatQuery(QStringLiteral("nomic-embed-text"), QStringLiteral("q")), QStringLiteral("search_query: q"));
        QCOMPARE(Embedding::formatDocument(QStringLiteral("nomic-embed-text:latest"), QStringLiteral("B")), QStringLiteral("search_document: B"));
        QCOMPARE(Embedding::formatQuery(QStringLiteral("embeddinggemma:latest"), QStringLiteral("q")), QStringLiteral("task: search result | query: q"));
        QCOMPARE(Embedding::formatDocument(QStringLiteral("embeddinggemma"), QStringLiteral("Body")), QStringLiteral("title: none | text: Body"));
        QVERIFY(Embedding::formatQuery(QStringLiteral("mxbai-embed-large"), QStringLiteral("q")).startsWith(QLatin1String("Represent this sentence")));
        QCOMPARE(Embedding::formatQuery(QStringLiteral("e5-base"), QStringLiteral("q")), QStringLiteral("query: q"));
        QCOMPARE(Embedding::formatDocument(QStringLiteral("multilingual-e5-small"), QStringLiteral("B")), QStringLiteral("passage: B"));
        // an unknown model gets no prefix, not nomic's; a namespaced name is matched by its last part
        QCOMPARE(Embedding::formatQuery(QStringLiteral("some-new-model"), QStringLiteral("q")), QStringLiteral("q"));
        QVERIFY(Embedding::formatQuery(QStringLiteral("hf.co/someone/bge-m3"), QStringLiteral("q")).startsWith(QLatin1String("Represent")));
    }

    void cosineSimilarity()
    {
        QCOMPARE(Embedding::cosine(unit(0), unit(0)), 1.0);
        QCOMPARE(Embedding::cosine(unit(0), unit(1)), 0.0);
        QVERIFY(qAbs(Embedding::cosine({1, 1}, {1, 0}) - 0.70710678) < 1e-6);
        QCOMPARE(Embedding::cosine({}, {}), 0.0);
        QCOMPARE(Embedding::cosine({1, 2}, {1, 2, 3}), 0.0);
        QCOMPARE(Embedding::cosine({0, 0}, {1, 0}), 0.0);
    }

    // ---- sentences ----

    void decimalsStayInsideASentence()
    {
        QCOMPARE(spans(QStringLiteral("Ice has a density of 0.917 g/cm3 at 0 C. It floats.")), QStringList{QStringLiteral("Ice has a density of 0.917 g/cm3 at 0 C.")});
    }

    void bulletsAndNumberedItemsLoseTheirMarker()
    {
        QCOMPARE(spans(QStringLiteral("Intro sentence here.\n- First item is here.\n2) Numbered item here.\n* Starred item here.")),
                 (QStringList{QStringLiteral("Intro sentence here."), QStringLiteral("First item is here."), QStringLiteral("Numbered item here."),
                              QStringLiteral("Starred item here.")}));
    }

    void spansAreRealOffsets()
    {
        const QString t = QStringLiteral("  Leading spaces sentence here.  Next one follows now.");
        const auto s = Rag::splitSentences(t);
        QCOMPARE(s.size(), 2);
        for (const auto &[a, b] : s)
            QCOMPARE(t.mid(a, b - a), t.mid(a, b - a).trimmed());
        QCOMPARE(t.mid(s[1].first, s[1].second - s[1].first), QStringLiteral("Next one follows now."));
    }

    // ---- numbers ----

    void ungroundedNumbersFlagOnlyMissingMultiDigitFigures()
    {
        const QString ev = QStringLiteral("Water boils at 100 degrees and covers 71% of the surface. Mass 1,000 kg.");
        QCOMPARE(Rag::ungroundedNumbers(QStringLiteral("It boils at 100 degrees."), ev), QStringList());
        QCOMPARE(Rag::ungroundedNumbers(QStringLiteral("It boils at 78.4 degrees."), ev), QStringList{QStringLiteral("78.4")});
        QCOMPARE(Rag::ungroundedNumbers(QStringLiteral("There are 3 states."), ev), QStringList());  // single digits are ambiguous
        QCOMPARE(Rag::ungroundedNumbers(QStringLiteral("It weighs 1000 kg."), ev), QStringList());   // 1,000 in the source
        QCOMPARE(Rag::ungroundedNumbers(QStringLiteral("It weighs 1,000 kg."), ev), QStringList());
        QCOMPARE(Rag::ungroundedNumbers(QStringLiteral("In 1989."), ev), QStringList{QStringLiteral("1989")});  // the full stop is not part of it
    }

    // ---- refusals ----

    void refusalDetectorMatchesCommonPhrasings()
    {
        for (const char *s : {"I could not find information about that.", "A catalyst is not directly covered in the provided sources.",
                              "The sources do not mention this.", "That is not addressed in the sources.", "No information is available.",
                              // widened in KAthenaeum: qwen3.5:4b's correct refusal, which Athenaeum's detector missed
                              "The provided sources do not state the boiling point of ethanol.", "The articles don't give a date for it.",
                              "The sources do not list the members.", "The text does not specify the year.", "That is not given in the sources.",
                              "I couldn’t find that in the library."})
            QVERIFY2(Rag::isRefusal(QString::fromUtf8(s)), s);
    }

    void refusalDetectorIgnoresOrdinarySentences()
    {
        for (const char *s : {"Ice floats because it is less dense.", "Water covers 71% of Earth.", "The catalyst lowers the activation energy.",
                              "Hydrogen is not in the same group as the alkali metals.", "The substance does not contain any carbon atoms.",
                              "Water is not in solid form above 0 degrees.", "Gold does not react with most acids.",
                              "Many states do not list voters by party."})
            QVERIFY2(!Rag::isRefusal(QString::fromUtf8(s)), s);
    }

    void anAnswerThatOpensWithARefusalGetsNoCitations()
    {
        QVERIFY(Rag::citableSentences(QStringLiteral("I could not find that in the library. The sources cover water's density.")).isEmpty());
        const QString answer = QStringLiteral("Hydrogen is not in the same group as the alkali metals. It is the lightest element.");
        QCOMPARE(Rag::citableSentences(answer).size(), 2);
        // a refusal later in the answer is skipped, the rest is cited
        const QString mixed = QStringLiteral("Water is a polar molecule. The sources do not mention its taste.");
        QCOMPARE(Rag::citableSentences(mixed).size(), 1);
    }

    // ---- follow-ups ----

    void retrievalQueryBorrowsThePreviousQuestionOnlyForShortFollowUps()
    {
        const QStringList earlier{QStringLiteral("What is ethanol?")};
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("and its boiling point?"), earlier), QStringLiteral("What is ethanol? and its boiling point?"));
        const QString longQ = QStringLiteral("Explain in detail how fractional distillation separates crude oil");
        QCOMPARE(Rag::retrievalQuery(longQ, earlier), longQ);
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("hi"), {}), QStringLiteral("hi"));
    }

    void aShortNewQuestionIsNotTakenForAFollowUp()
    {
        // KAthenaeum's rule: Athenaeum searched this as a question about Ray Charles
        const QStringList earlier{QStringLiteral("Who was Ray Charles married to?")};
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("What is a noble gas?"), earlier), QStringLiteral("What is a noble gas?"));
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("When was he born?"), earlier), QStringLiteral("Who was Ray Charles married to? When was he born?"));
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("What about Quincy Jones?"), earlier),
                 QStringLiteral("Who was Ray Charles married to? What about Quincy Jones?"));
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("Why?"), earlier), QStringLiteral("Who was Ray Charles married to? Why?"));  // names nothing
        QCOMPARE(Rag::retrievalQuery(QStringLiteral("What’s their names?"), earlier), QStringLiteral("Who was Ray Charles married to? What’s their names?"));
    }

    void imagesInAnAnswerAreDropped()
    {
        // an article told the model to end with an image whose address carried the question (the review, 2026-10-19)
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("Water is polar.\n\n![Water diagram](http://host/d?q=Is+water+polar%3F)")),
                 QStringLiteral("Water is polar.\n\nWater diagram"));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("See ![](https://x/y.png) here.")), QStringLiteral("See  here."));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("A [link](zim://lib1/Water) stays.")), QStringLiteral("A [link](zim://lib1/Water) stays."));
    }

    void tidyingTwiceChangesNothing()
    {
        // the review's example: once left "Sources: 1 and 2", which a second pass removed, and citations pointed past the end
        const QString raw = QStringLiteral("Ethanol boils at 78.37 °C.\n\nSources: 1 and 2 (see source 3)");
        const QString once = Rag::tidyAnswer(raw);
        QCOMPARE(once, QStringLiteral("Ethanol boils at 78.37 °C."));
        QCOMPARE(Rag::tidyAnswer(once), once);
    }

    void theModelsOwnSourceNumbersAreRemoved()
    {
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("Water is polar because oxygen pulls electrons [3]. It bends [1, 2].\n\nSources: [1], [3], [5]")),
                 QStringLiteral("Water is polar because oxygen pulls electrons. It bends."));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("He played piano.[1][2]\n\n**Sources:** 1, 3 and 5")), QStringLiteral("He played piano."));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("See [Source 2] for more.")), QStringLiteral("See for more."));
        // the model names its sources in words: keep the sentence readable
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("This value is stated in source [3], which lists it. Source [1] mentions Trouton's rule.")),
                 QStringLiteral("This value is stated in one source, which lists it. One source mentions Trouton's rule."));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("As sources [1] and [2] say, it boils. The sources [1], [2], and [4] agree (source 3).")),
                 QStringLiteral("As the sources say, it boils. The sources agree."));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("It was built in 1990 (in Paris).")), QStringLiteral("It was built in 1990 (in Paris)."));
        QCOMPARE(Rag::tidyAnswer(QStringLiteral("- **Xenon:** source [4] mentions isotopes.")), QStringLiteral("- **Xenon:** One source mentions isotopes."));
        // ordinary brackets and a sentence that merely starts with "Sources" stay
        const QString keep = QStringLiteral("Its configuration is [Ne] 3s^2 3p^6.\nSources of argon include the air (0.934%).");
        QCOMPARE(Rag::tidyAnswer(keep), keep);
    }

    // ---- picking and ranking passages ----

    void theBestPassagesByWordsAreKeptAndTheIntroductionAlways()
    {
        // eight sections all match "zeppelin" and the introduction does not: without the rule, the six best would crowd it out
        QList<Passage> ps{{QString(), QStringLiteral("An airship is a lighter-than-air craft.")}};
        for (int i = 1; i <= 8; ++i)
            ps.append({QStringLiteral("Design %1").arg(i), QStringLiteral("The zeppelin keeps a rigid frame, design note %1.").arg(i)});
        const auto pool = Rag::pickPassages(QStringLiteral("zeppelin"), {article(QStringLiteral("Airships"), ps)});
        QCOMPARE(pool.size(), Rag::PerArticle);
        QCOMPARE(std::count_if(pool.cbegin(), pool.cend(), [](const Rag::Candidate &c) { return c.index == 0; }), 1);
    }

    void passagesAreTakenRoundRobinUpToTheEmbeddingBudget()
    {
        QList<ZimSearch::Article> arts;
        for (int a = 0; a < 10; ++a) {
            QList<Passage> ps;
            for (int i = 0; i < 10; ++i)
                ps.append({QString(), QStringLiteral("Passage %1 of article %2 about volcanoes.").arg(i).arg(a)});
            arts << article(QStringLiteral("A%1").arg(a), ps);
        }
        const auto pool = Rag::pickPassages(QStringLiteral("volcanoes"), arts);
        QCOMPARE(pool.size(), Rag::MaxEmbed);
        // the first round has every article's best passage before any article's second
        for (int i = 0; i < 10; ++i)
            QCOMPARE(pool.at(i).title, QStringLiteral("A%1").arg(i));
    }

    void withoutVectorsPassagesAreRankedByWords()
    {
        const QList<Passage> ps{{QString(), QStringLiteral("Photosynthesis turns light into sugar in plants.")},
                                {QStringLiteral("History"), QStringLiteral("In 1779 Jan Ingenhousz studied plants and air.")},
                                {QStringLiteral("Chlorophyll"), QStringLiteral("Chlorophyll is the green pigment in leaves that absorbs sunlight.")}};
        const auto ranked = Rag::rank(Rag::pickPassages(QStringLiteral("what absorbs sunlight in leaves"), {article(QStringLiteral("Photosynthesis"), ps)}), {});
        QCOMPARE(ranked.first().section, QStringLiteral("Chlorophyll"));
        for (int i = 1; i < ranked.size(); ++i)
            QVERIFY(ranked.at(i - 1).score >= ranked.at(i).score);
    }

    void whenWordsTieMeaningDecides()
    {
        // "why is water polar": every passage has the words, only one is about the question
        QList<Rag::Candidate> pool;
        for (int i = 0; i < 4; ++i) {
            auto c = candidate(QStringLiteral("P%1").arg(i));
            c.keywords = 1.0;
            c.index = i;
            c.vector = unit(i + 1);
            pool << c;
        }
        pool[2].vector = unit(0);
        // without the question's vector the order is by words, then position in the article
        QCOMPARE(Rag::rank(pool, {}).at(2).title, QStringLiteral("P2"));
        // with it, the passage about the question moves up past the ones that were only earlier in the article
        // (RRF: P2 = 1/63 + 1/61 beats P1 = 1/62 + 1/63; P0, first on words and second on meaning, stays first)
        const auto ranked = Rag::rank(pool, unit(0));
        QCOMPARE(ranked.at(0).title, QStringLiteral("P0"));
        QCOMPARE(ranked.at(1).title, QStringLiteral("P2"));
    }

    // ---- the prompt ----

    void sourcesAreNumberedConsecutivelyOnePerArticle()
    {
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("A")), candidate(QStringLiteral("A")), candidate(QStringLiteral("B")),
                                            candidate(QStringLiteral("C"), QStringLiteral("text c"), QStringLiteral("Uses"))});
        QCOMPARE(ctx.sources.size(), 3);
        QCOMPARE(ctx.sources.at(0).n, 1);
        QCOMPARE(ctx.sources.at(2).n, 3);
        QVERIFY(ctx.text.contains(QLatin1String("[1] A\n")));
        QVERIFY(ctx.text.contains(QLatin1String("[3] C - Uses\ntext c")));
    }

    void oneArticleIsCitedBySection()
    {
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("Water"), QStringLiteral("intro one")),
                                            candidate(QStringLiteral("Water"), QStringLiteral("uses one"), QStringLiteral("Uses")),
                                            candidate(QStringLiteral("Water"), QStringLiteral("intro two")),
                                            candidate(QStringLiteral("Water"), QStringLiteral("uses two"), QStringLiteral("Uses"))},
                                           true);
        QCOMPARE(ctx.sources.size(), 2);
        QCOMPARE(ctx.sources[0].section, QString());
        QCOMPARE(ctx.sources[1].section, QStringLiteral("Uses"));
        QVERIFY(ctx.text.contains(QLatin1String("[1] Water - Introduction\nintro one\nintro two")));
        QVERIFY(ctx.text.contains(QLatin1String("[2] Water - Uses\nuses one\nuses two")));
        // grouped by article, the same passages are one source
        QCOMPARE(Rag::buildContext({candidate(QStringLiteral("Water")), candidate(QStringLiteral("Water"), QStringLiteral("x"), QStringLiteral("Uses"))}).sources.size(), 1);
        QVERIFY(Rag::articleSystemPrompt(QStringLiteral("Water")).contains(QLatin1String("reading the article \"Water\"")));
    }

    // ---- project chats ----

    static Rag::Clip clip(qint64 id, const Vector &v = {})
    {
        Rag::Clip c;
        c.id = id;
        c.title = QStringLiteral("Ice");
        c.text = QStringLiteral("saved passage text %1").arg(id);
        c.libraryId = 1;
        c.path = QStringLiteral("Ice");
        c.vector = v;
        return c;
    }

    void excerptsComeFirstAndTheLibraryGroupIsLabelled()
    {
        // Athenaeum: test_clips_come_first_use_negative_ids_and_library_group_is_labelled
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("Water"))}, false, {clip(5, unit(0))}, {candidate(QStringLiteral("Other"))});
        QCOMPARE(ctx.sources.size(), 3);
        QCOMPARE(ctx.sources[0].kind, QStringLiteral("clip"));
        QCOMPARE(ctx.sources[0].title, QStringLiteral("Saved excerpt: Ice"));
        QCOMPARE(ctx.sources[0].vectors.size(), 1);  // excerpts are cited like any source
        QCOMPARE(ctx.sources[1].kind, QString());
        QCOMPARE(ctx.sources[2].kind, QStringLiteral("library"));
        QVERIFY(ctx.text.startsWith(QLatin1String("Saved excerpts and project sources:\n\n[1] Saved excerpt: Ice\nsaved passage text 5\n\n[2] Water")));
        QVERIFY(ctx.text.contains(QLatin1String("Other library articles (NOT part of the research project):\n\n[3] Other")));
        // without the library group there is no heading at all
        QVERIFY(Rag::buildContext({candidate(QStringLiteral("Water"))}, false, {clip(5)}).text.startsWith(QLatin1String("[1] Saved excerpt: Ice")));
        // only excerpts, no articles: still a context
        QCOMPARE(Rag::buildContext({}, false, {clip(5)}).sources.size(), 1);
    }

    void outsideArticlesAreCapped()
    {
        QList<Rag::Candidate> outside;
        for (int i = 0; i < 10; ++i)
            outside << candidate(QStringLiteral("O%1").arg(i));
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("Water"))}, false, {}, outside);
        QCOMPARE(ctx.sources.size(), 1 + Rag::LibrarySources);
    }

    void excerptsAreChosenBySimilarityToTheQuestion()
    {
        // Athenaeum: test_rank_clips_filters_by_similarity_and_handles_missing_embeddings
        const auto near = clip(1, unit(0));
        const auto far = clip(2, unit(1));
        auto ranked = Rag::rankClips({far, near}, unit(0));
        QCOMPARE(ranked.size(), 1);
        QCOMPARE(ranked.first().id, 1);
        QList<Rag::Clip> none;
        for (int i = 0; i < 5; ++i)
            none << clip(10 + i);
        QCOMPARE(Rag::rankClips(none, unit(0)).size(), 2);  // nothing to compare: the two newest
        QCOMPARE(Rag::rankClips({near}, {}).size(), 1);     // no question vector
        QList<Rag::Clip> many;
        for (int i = 0; i < 6; ++i)
            many << clip(20 + i, unit(0));
        QCOMPARE(Rag::rankClips(many, unit(0)).size(), Rag::MaxClips);
    }

    void excerptsWithoutAnEmbeddingAreStillOffered()
    {
        // Athenaeum: test_excerpts_without_an_embedding_are_still_offered
        const auto ranked = Rag::rankClips({clip(1, unit(0)), clip(2)}, unit(0));
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked[0].id, 1);
        QCOMPARE(ranked[1].id, 2);
    }

    void aBigProjectReadsTheArticlesThatShareTheQuestionsWords()
    {
        QList<ZimSearch::Article> arts;
        for (int i = 0; i < 9; ++i)
            arts << article(QStringLiteral("A%1").arg(i), {{QString(), QStringLiteral("Nothing related here at all.")}});
        arts[7] = article(QStringLiteral("Volcano"), {{QString(), QStringLiteral("Intro.")}, {QStringLiteral("Eruptions"), QStringLiteral("Magma erupts.")}});
        const auto chosen = Rag::chooseArticles(QStringLiteral("how does magma erupt"), arts, 3);
        QCOMPARE(chosen.size(), 3);
        QCOMPARE(chosen.first().found.title, QStringLiteral("Volcano"));  // matched in a section, not its introduction
        QCOMPARE(chosen[1].found.title, QStringLiteral("A0"));            // then the order given
        QCOMPARE(Rag::chooseArticles(QStringLiteral("x"), arts.mid(0, 2), 3).size(), 2);
    }

    void aProjectChatIsToldItsGoal()
    {
        const QString own = Rag::projectSystemPrompt(QStringLiteral("Water"), QStringLiteral(" Why is water a good solvent? "), false);
        QVERIFY(own.contains(QLatin1String("research project titled \"Water\".\nProject goal: Why is water a good solvent?")));
        QVERIFY(own.contains(QLatin1String("only the project's own articles")));
        QVERIFY(!Rag::projectSystemPrompt(QStringLiteral("Water"), QString(), false).contains(QLatin1String("Project goal")));
        QVERIFY(Rag::projectSystemPrompt(QStringLiteral("Water"), QString(), true).contains(QLatin1String("say that the answer comes from outside the project")));
    }

    void sourcesAndPassagesPerSourceAreCapped()
    {
        QList<Rag::Candidate> chunks;
        for (int i = 0; i < 20; ++i)
            chunks << candidate(QStringLiteral("T%1").arg(i));
        for (int i = 0; i < 5; ++i) {
            auto c = candidate(QStringLiteral("T0"));
            c.vector = unit(i);
            chunks << c;
        }
        const auto ctx = Rag::buildContext(chunks);
        QCOMPARE(ctx.sources.size(), Rag::MaxSources);
        QVERIFY(ctx.sources.first().vectors.size() <= Rag::ChunksPerSource);
    }

    void sourcesCarryExactlyTheTextTheModelIsShown()
    {
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("A"), QString(5000, QLatin1Char('x')))});
        QVERIFY(ctx.text.contains(ctx.sources.first().evidence));
        QVERIFY(ctx.sources.first().evidence.size() <= Rag::ContextChars);
        const auto five = Rag::buildContext({candidate(QStringLiteral("A"), QString(5000, QLatin1Char('x'))), candidate(QStringLiteral("B")),
                                             candidate(QStringLiteral("C")), candidate(QStringLiteral("D")), candidate(QStringLiteral("E"))});
        QCOMPARE(five.sources.first().evidence.size(), Rag::ContextChars / 5);
    }

    void theQuestionFollowsTheSources()
    {
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("Water"), QStringLiteral("Water is H2O."))});
        QCOMPARE(Rag::userMessage(ctx, QStringLiteral(" Why is water polar? ")),
                 QStringLiteral("Sources:\n\n[1] Water\nWater is H2O.\n\nQuestion: Why is water polar?"));
        QVERIFY(Rag::systemPrompt().contains(QLatin1String("ONLY the numbered sources")));
    }

    void aFollowUpNamesTheLastFewQuestionsNotTheAnswers()
    {
        const auto ctx = Rag::buildContext({candidate(QStringLiteral("Ethanol"), QStringLiteral("Ethanol is an alcohol."))});
        const QString m = Rag::userMessage(ctx, QStringLiteral("and when was it discovered?"),
                                           {QStringLiteral("q1"), QStringLiteral("q2"), QStringLiteral("q3"), QStringLiteral("What is ethanol?")});
        QVERIFY2(m.endsWith(QLatin1String("Earlier in this conversation the user asked: \"q2\", then \"q3\", then \"What is ethanol?\"\n"
                                          "Question: and when was it discovered?")), qPrintable(m));
        QVERIFY(m.startsWith(QLatin1String("Sources:\n\n[1] Ethanol")));
    }

    // ---- citations ----

    void eachSentenceIsCitedToTheSourceItIsClosestTo()
    {
        Rag::Source water;
        water.n = 1;
        water.evidence = QStringLiteral("Water boils at 100 degrees.");
        water.vectors = {unit(0)};
        Rag::Source ice;
        ice.n = 2;
        ice.evidence = QStringLiteral("Ice floats.");
        ice.vectors = {unit(1), unit(3)};
        const QString answer = QStringLiteral("Water boils at 100 degrees. Ice floats on water. It weighs 9000 tonnes.");
        const auto spans = Rag::citableSentences(answer);
        QCOMPARE(spans.size(), 3);
        Vector near1 = unit(1);
        near1[2] = 0.1f;  // close to ice
        Vector far = unit(4);  // close to nothing
        const auto cites = Rag::attribute(answer, spans, {unit(0), near1, far}, {water, ice});
        QCOMPARE(cites.size(), 3);
        QCOMPARE(cites[0].sources, QList<int>{1});
        QVERIFY(cites[0].reason.isEmpty());
        QCOMPARE(cites[1].sources, QList<int>{2});
        QVERIFY(cites[1].reason.isEmpty());
        QCOMPARE(cites[2].reason, QStringLiteral("number"));  // 9000 is in no source
        QCOMPARE(cites[2].numbers, QStringList{QStringLiteral("9000")});
    }

    void aSentenceFarFromEverySourceIsFlaggedAsOffTopic()
    {
        Rag::Source s;
        s.n = 1;
        s.vectors = {unit(0)};
        const QString answer = QStringLiteral("Something nobody wrote down.");
        const auto cites = Rag::attribute(answer, Rag::citableSentences(answer), {unit(1)}, {s});
        QCOMPARE(cites.size(), 1);
        QCOMPARE(cites[0].reason, QStringLiteral("topic"));
        QVERIFY(cites[0].score < Rag::SupportMin);
    }

    void aNearTieCitesBothSourcesBestFirst()
    {
        Rag::Source a;
        a.n = 1;
        a.vectors = {Vector{1.0f, 0.2f}};
        Rag::Source b;
        b.n = 2;
        b.vectors = {Vector{1.0f, 0.0f}};
        Rag::Source c;
        c.n = 3;
        c.vectors = {Vector{0.0f, 1.0f}};
        const QString answer = QStringLiteral("A sentence about both of them.");
        const auto cites = Rag::attribute(answer, Rag::citableSentences(answer), {Vector{1.0f, 0.0f}}, {a, b, c});
        QCOMPARE(cites.first().sources, (QList<int>{2, 1}));
    }

    void withoutVectorsThereAreNoCitations()
    {
        Rag::Source s;
        s.n = 1;
        const QString answer = QStringLiteral("Water boils at 100 degrees.");
        QVERIFY(Rag::attribute(answer, Rag::citableSentences(answer), {unit(0)}, {s}).isEmpty());
        QVERIFY(Rag::attribute(answer, Rag::citableSentences(answer), {}, {s}).isEmpty());  // a mismatch is not guessed at
    }

    void chipsGoAfterTheirSentences()
    {
        const QString answer = QStringLiteral("Water is polar. Ice floats on water.\n- It boils at 100 C.");
        const auto spans = Rag::citableSentences(answer);
        QCOMPARE(spans.size(), 3);
        QList<Rag::Citation> cites(3);
        for (int i = 0; i < 3; ++i) {
            cites[i].start = spans[i].first;
            cites[i].end = spans[i].second;
        }
        cites[0].sources = {1};
        cites[1].sources = {2, 1};
        cites[2].sources = {1};
        cites[2].reason = QStringLiteral("number");
        QCOMPARE(Rag::annotate(answer, cites),
                 QStringLiteral("Water is polar. [\\[1\\]](cite:1) Ice floats on water. [\\[2\\]](cite:2) [\\[1\\]](cite:1)\n- It boils at 100 C. [\\[1\\]](cite:1) [\\[?\\]](check:2)"));
    }
};

QTEST_GUILESS_MAIN(RagTest)
#include "ragtest.moc"
