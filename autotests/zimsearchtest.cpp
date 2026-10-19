// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"
#include "core/library.h"
#include "core/zimreader.h"
#include "core/zimsearch.h"

#include <zim/writer/creator.h>
#include <zim/writer/item.h>

// Ported from Athenaeum's tests/test_zimsearch.py. Builds a tiny Wikipedia-layout ZIM with a full-text index on the fly,
// so it needs no downloaded library.
class ZimSearchTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    qint64 m_id = 0;
    qint64 m_music = 0;  // a second library, about something else

    static std::string article(const std::string &title, const std::string &intro, const std::vector<std::pair<std::string, std::string>> &sections)
    {
        std::string body = "<p>" + intro + "</p>";
        for (const auto &[h, t] : sections)
            body += "<section><h2>" + h + "</h2><p>" + t + "</p></section>";
        return "<html><head><title>" + title + "</title></head><body><div class='mw-parser-output'>" + body + "</div></body></html>";
    }

    void buildZim(const QString &file)
    {
        std::vector<std::pair<std::string, std::string>> airship;
        for (int i = 1; i <= 8; ++i)
            airship.push_back({"Design " + std::to_string(i), "Design note " + std::to_string(i)
                                   + ": the zeppelin type of airship keeps a rigid internal frame, and zeppelin builders in section " + std::to_string(i)
                                   + " describe gas cells, ballast and fins at length."});
        const std::vector<std::tuple<std::string, std::string, std::string>> pages = {
            {"Photosynthesis", "Photosynthesis",
             article("Photosynthesis", "Photosynthesis is the process by which green plants convert sunlight, water and carbon dioxide into sugar and oxygen.",
                     {{"Chlorophyll", "Chlorophyll is the green pigment in the chloroplasts of leaves that absorbs sunlight, mostly red and blue light."},
                      {"History", "In 1779 Jan Ingenhousz showed that plants need sunlight to purify air, which led to the modern understanding of the process."}})},
            {"Volcano", "Volcano",
             article("Volcano", "A volcano is an opening in the crust of a planet through which molten rock, ash and gases escape from below the surface.",
                     {{"Eruptions", "Eruptions happen when pressure from dissolved gases in magma rises until the magma breaks through the overlying rock."}})},
            {"Glacier", "Glacier",
             article("Glacier", "A glacier is a large slow-moving mass of ice formed from compacted snow over many years on land.",
                     {{"Movement", "Glaciers flow under their own weight and carve valleys into the rock below them as they travel downhill."}})},
            {"Photosynthetic_bacteria", "Photosynthetic bacteria",
             article("Photosynthetic bacteria", "Photosynthetic bacteria are microbes that capture light energy, some without producing oxygen.", {})},
            {"River", "River",
             article("River", "A river is a stream of water that carves valleys and canyons as it flows downhill towards the sea.",
                     {{"Course", "Rivers carve their valleys over thousands of years, and a glacier can carve them deeper still."}})},
            {"Airships", "Airships", article("Airships", "An airship is a lighter-than-air craft that can steer and move through the air under its own power.", airship)},
        };
        zim::writer::Creator c;
        c.configIndexing(true, "eng");
        c.startZimCreation(file.toStdString());
        c.setMainPath("Photosynthesis");
        for (const auto &[k, v] : std::vector<std::pair<std::string, std::string>>{
                 {"Name", "t"}, {"Title", "Test"}, {"Creator", "x"}, {"Publisher", "x"}, {"Date", "2026-01-01"}, {"Description", "d"}, {"Language", "eng"}, {"Scraper", "mwoffliner test"}})
            c.addMetadata(k, v);
        for (const auto &[path, title, html] : pages)
            c.addItem(zim::writer::StringItem::create(path, std::string("text/html"), title, zim::writer::Hints{{zim::writer::HintKeys::FRONT_ARTICLE, 1}}, html));
        c.addRedirection("Plants_and_sunlight", "Plants and sunlight", "Photosynthesis", {{zim::writer::HintKeys::FRONT_ARTICLE, 1}});
        // mwoffliner's other kind of redirect: a tiny HTML page forwarding to a section of another article
        c.addItem(zim::writer::StringItem::create(std::string("Eruption_(volcano)"), std::string("text/html"), std::string("Eruption (volcano)"),
                                                  zim::writer::Hints{{zim::writer::HintKeys::FRONT_ARTICLE, 1}},
                                                  std::string("<html><head><title>Eruption (volcano)</title><meta http-equiv=\"refresh\" content=\"0;URL='./Volcano#Eruptions'\" />"
                                                              "</head><body><a href=\"./Volcano#Eruptions\">Eruption (volcano)</a></body></html>")));
        c.finishZimCreation();
    }

    static void buildMusicZim(const QString &file)
    {
        zim::writer::Creator c;
        c.configIndexing(true, "eng");
        c.startZimCreation(file.toStdString());
        c.setMainPath("Morning_song");
        for (const auto &[k, v] : std::vector<std::pair<std::string, std::string>>{
                 {"Name", "m"}, {"Title", "Music"}, {"Date", "2026-01-01"}, {"Language", "eng"}, {"Scraper", "mwoffliner test"}})
            c.addMetadata(k, v);
        // like "Ray Charles" for "why is water polar": every word of the query is in it, far from its opening
        c.addItem(zim::writer::StringItem::create(std::string("Morning_song"), std::string("text/html"), std::string("Morning song"),
                                                  zim::writer::Hints{{zim::writer::HintKeys::FRONT_ARTICLE, 1}},
                                                  article("Morning song", "Morning song is a ballad recorded in 1961 by a blues singer from Georgia.",
                                                          {{"Awards", "The singer later won a prize in Stockholm for the record, which describes how sunlight warms "
                                                                      "the window and turns sugar in the tea, and how plants on the sill lean towards the light."}})));
        c.finishZimCreation();
    }

    static QStringList paths(const QList<ZimSearch::Found> &found)
    {
        QStringList out;
        for (const auto &f : found)
            out << f.path;
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        const QString file = m_dir.filePath(QStringLiteral("test_2026-01.zim"));
        buildZim(file);
        QVERIFY(Database::open(m_dir.filePath(QStringLiteral("t.sqlite")), nullptr));
        QString error;
        const auto id = Library::add(file, &error);
        QVERIFY2(id, qPrintable(error));
        m_id = *id;
        const QString music = m_dir.filePath(QStringLiteral("music_2026-01.zim"));
        buildMusicZim(music);
        m_music = *Library::add(music);
        ZimSearch::prepare({m_id, m_music});
    }

    void termsKeepContentWordsOnly()
    {
        QCOMPARE(ZimSearch::terms(QStringLiteral("Why does the sky look blue?")), (QStringList{QStringLiteral("sky"), QStringLiteral("look"), QStringLiteral("blue")}));
        QCOMPARE(ZimSearch::terms(QStringLiteral("What's the PH of  lemon juice, juice?")), (QStringList{QStringLiteral("ph"), QStringLiteral("lemon"), QStringLiteral("juice")}));
        QCOMPARE(ZimSearch::terms(QStringLiteral("a an the of")), QStringList());
        QCOMPARE(ZimSearch::terms(QStringLiteral("type 2 diabetes")), (QStringList{QStringLiteral("type"), QStringLiteral("2"), QStringLiteral("diabetes")}));
        QStringList many;
        for (int i = 0; i < 30; ++i)
            many << QStringLiteral("word%1").arg(i);
        QCOMPARE(ZimSearch::terms(many.join(QLatin1Char(' '))).size(), ZimSearch::MaxTerms);
        QCOMPARE(ZimSearch::terms(QStringLiteral("how do vaccines work")), QStringList{QStringLiteral("vaccines")});
    }

    void overlapMatchesPluralsAndIgnoresUnrelatedText()
    {
        QStringList stems;
        for (const QString &t : ZimSearch::terms(QStringLiteral("kidneys diabetes")))
            stems << ZimSearch::stem(t);
        QCOMPARE(ZimSearch::overlap(stems, QStringLiteral("The kidney filters blood. Diabetic patients...")), 1.0);
        QCOMPARE(ZimSearch::overlap(stems, QStringLiteral("Volcanoes erupt.")), 0.0);
        QCOMPARE(ZimSearch::overlap({}, QStringLiteral("anything")), 0.0);
    }

    void titleCandidatesUndoQuestionWordsAndCasing()
    {
        QCOMPARE(ZimSearch::titleCandidates(QStringLiteral("who was Ray Charles?")).mid(0, 2), (QStringList{QStringLiteral("who was Ray Charles"), QStringLiteral("Ray Charles")}));
        QVERIFY(ZimSearch::titleCandidates(QStringLiteral("hypertension")).contains(QStringLiteral("Hypertension")));
        QVERIFY(ZimSearch::titleCandidates(QStringLiteral("what is a noble gas")).contains(QStringLiteral("Noble Gas")));
        QCOMPARE(ZimSearch::titleCandidates(QStringLiteral("   ")), QStringList());
    }

    void wordRunsOfAQuestionAreTriedAsTitles()
    {
        QHash<QString, double> grams;
        for (const auto &[g, w] : ZimSearch::ngramTitles(QStringLiteral("what is the boiling point of ethanol")))
            grams.insert(g, w);
        QCOMPARE(grams.value(QStringLiteral("boiling point")), 1.0);
        QCOMPARE(grams.value(QStringLiteral("ethanol")), 0.8);
        QCOMPARE(grams.value(QStringLiteral("point of ethanol")), 1.2);
        QVERIFY(!grams.contains(QStringLiteral("of ethanol")) && !grams.contains(QStringLiteral("the")) && !grams.contains(QStringLiteral("what")));
        QVERIFY(ZimSearch::ngramTitles(QStringLiteral("a an the")).isEmpty());
    }

    void questionsAreToldFromNamesAndTopics()
    {
        QVERIFY(ZimSearch::isQuestion(QStringLiteral("why is the sky blue")));
        QVERIFY(ZimSearch::isQuestion(QStringLiteral("what is a noble gas?")));
        QVERIFY(!ZimSearch::isQuestion(QStringLiteral("noble gases")));
        QVERIFY(!ZimSearch::isQuestion(QStringLiteral("hypertension")));
        QVERIFY(!ZimSearch::isQuestion(QStringLiteral("who was Ray Charles")));  // the rest is a name: a title lookup
    }

    void exactTitleAndRedirectsFindTheArticle()
    {
        const auto res = ZimSearch::search(QStringLiteral("Photosynthesis"), {m_id}, 10);
        QCOMPARE(res.first().path, QStringLiteral("Photosynthesis"));
        QVERIFY(res.first().titleMatch);
        QVERIFY(!ZimSearch::search(QStringLiteral("chlorophyll pigment"), {m_id}, 10).first().titleMatch);  // a text match is not a title match
        // an alias redirects to the real article and is not listed twice
        const QStringList p = paths(ZimSearch::findArticles(QStringLiteral("Plants and sunlight"), {m_id}, 10));
        QCOMPARE(p.first(), QStringLiteral("Photosynthesis"));
        QCOMPARE(p.count(QStringLiteral("Photosynthesis")), 1);
    }

    void theThingsAQuestionNamesAreFound()
    {
        const QStringList p = paths(ZimSearch::findArticles(QStringLiteral("how is a glacier different from a volcano"), {m_id}, 10));
        QVERIFY(p.contains(QStringLiteral("Glacier")) && p.contains(QStringLiteral("Volcano")));
    }

    void aQuestionFindsTheArticleByItsTitleHoweverItIsCased()
    {
        QCOMPARE(ZimSearch::findArticles(QStringLiteral("what is a glacier?"), {m_id}, 10).first().path, QStringLiteral("Glacier"));
        QCOMPARE(ZimSearch::findArticles(QStringLiteral("volcano"), {m_id}, 10).first().path, QStringLiteral("Volcano"));
        QVERIFY((QStringList{QStringLiteral("Photosynthetic_bacteria"), QStringLiteral("Photosynthesis")})
                    .contains(ZimSearch::findArticles(QStringLiteral("photosynthetic"), {m_id}, 10).first().path));
    }

    void aNaturalQuestionIsWidenedInsteadOfMatchingNothing()
    {
        // no single article has both words, so every-word matching alone finds nothing
        const QStringList p = paths(ZimSearch::findArticles(QStringLiteral("why do sunlight eruptions matter"), {m_id}, 10));
        QVERIFY(p.contains(QStringLiteral("Photosynthesis")) && p.contains(QStringLiteral("Volcano")));
    }

    void unknownWordsFindNothingAndDoNotFail()
    {
        QVERIFY(ZimSearch::findArticles(QStringLiteral("zzzqqq xxyyzz"), {m_id}, 10).isEmpty());
        QVERIFY(ZimSearch::findArticles(QStringLiteral("???"), {m_id}, 10).isEmpty());
        QVERIFY(ZimSearch::findArticles(QStringLiteral("anything"), {987654}, 10).isEmpty());  // not installed
    }

    void resultsShowTheMatchingPassage()
    {
        const auto top = ZimSearch::search(QStringLiteral("chlorophyll pigment"), {m_id}, 10).first();
        QCOMPARE(top.path, QStringLiteral("Photosynthesis"));
        QCOMPARE(top.libraryId, m_id);
        QVERIFY(top.snippet.contains(QLatin1String("pigment")));
        QCOMPARE(top.section, QStringLiteral("Chlorophyll"));  // the best passage, not simply the introduction
    }

    void anArticleAboutTheQueryBeatsOneThatOnlyMentionsIt()
    {
        // Both articles contain every word, and each is its library's best match, so rank fusion alone puts them level
        // (the music library is listed first, so it would win the tie). Photosynthesis is about the query; the song only
        // mentions it.
        const auto found = ZimSearch::findArticles(QStringLiteral("how does sunlight make sugar in plants"), {m_music, m_id}, 10);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().path, QStringLiteral("Photosynthesis"));
        int song = -1, photo = -1;
        for (int i = 0; i < found.size(); ++i) {
            if (found[i].path == QLatin1String("Morning_song"))
                song = i;
            if (found[i].path == QLatin1String("Photosynthesis"))
                photo = i;
        }
        QVERIFY(song == -1 || song > photo);
    }

    void anArticleTheQueryNamesStaysFirstEvenIfItsOpeningDoesNot()
    {
        // The question names "Glacier", whose introduction does not say "carve" or "valleys"; "River" opens with both
        const auto found = ZimSearch::findArticles(QStringLiteral("how does a glacier carve valleys"), {m_id}, 10);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().path, QStringLiteral("Glacier"));
        QVERIFY(found.first().named);
    }

    void htmlRedirectPagesAreFollowedToTheSection()
    {
        QCOMPARE(ZimReader::htmlRedirectTarget("<meta http-equiv=\"refresh\" content=\"0;URL='./Cry_(song)#Version'\" />", QStringLiteral("Cry")),
                 QStringLiteral("Cry_(song)#Version"));
        QCOMPARE(ZimReader::htmlRedirectTarget("<p>An ordinary page</p>", QStringLiteral("X")), QString());
        const auto c = ZimReader::content(m_id, QStringLiteral("Eruption_(volcano)"));
        QVERIFY(c);
        QCOMPARE(c->path, QStringLiteral("Volcano"));
        QCOMPARE(c->fragment, QStringLiteral("Eruptions"));
        QVERIFY(c->data.contains("molten rock"));
        // A to Z lists the article under its own title, not the forwarding page
        for (const ZimEntry &e : ZimReader::titles(m_id, QStringLiteral("E"), 0, 100).entries)
            QVERIFY(e.path != QLatin1String("Eruption_(volcano)"));
        // and search lists the article, not the forwarding page
        const auto found = ZimSearch::findArticles(QStringLiteral("Eruption (volcano)"), {m_id}, 10);
        for (const auto &f : found)
            QVERIFY(f.path != QLatin1String("Eruption_(volcano)"));
    }

    void cleanupTestCase() { Database::close(); }
};

QTEST_GUILESS_MAIN(ZimSearchTest)
#include "zimsearchtest.moc"
