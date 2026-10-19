// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/chats.h"
#include "core/database.h"
#include "core/projects.h"

// Ported from the project cases of Athenaeum's tests (lifecycles, cascades, idempotent adds, export)
class ProjectsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static int count(const QString &sql)
    {
        QSqlQuery q(sql);
        return q.next() ? q.value(0).toInt() : -1;
    }

private Q_SLOTS:
    void init()
    {
        QString error;
        QVERIFY2(Database::open(m_dir.filePath(QStringLiteral("%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), &error),
                 qPrintable(error));
        // two installed libraries for the articles to point at
        QVERIFY(QSqlQuery().exec(QStringLiteral("INSERT INTO libraries (id, path, title) VALUES (1, '/x/chem.zim', 'Chemistry'), (2, '/x/ray.zim', 'Ray Charles')")));
    }
    void cleanup() { Database::close(); }

    void aProjectHasATitleAndAGoal()
    {
        QCOMPARE(Projects::create(QStringLiteral("   ")), 0);  // no title, no project
        const qint64 id = Projects::create(QStringLiteral("  Water  "), QStringLiteral(" Why is water a good solvent? "));
        QVERIFY(id > 0);
        auto p = Projects::byId(id);
        QCOMPARE(p->title, QStringLiteral("Water"));
        QCOMPARE(p->description, QStringLiteral("Why is water a good solvent?"));
        QVERIFY(Projects::update(id, QStringLiteral("Water and ice"), QString()));
        QCOMPARE(Projects::byId(id)->title, QStringLiteral("Water and ice"));
        QVERIFY(!Projects::update(id, QString(), QString()));
        QVERIFY(!Projects::byId(id + 100));
    }

    void addingAnArticleTwiceKeepsOneAndItsNote()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        const ArticleRef water{1, QStringLiteral("Water"), QStringLiteral("Water")};
        const qint64 a = Projects::addArticle(p, water, QStringLiteral("the main one"));
        QVERIFY(a > 0);
        QCOMPARE(Projects::addArticle(p, water), a);  // same item, note kept
        QCOMPARE(Projects::items(p, QStringLiteral("article")).size(), 1);
        QCOMPARE(Projects::items(p).first().note, QStringLiteral("the main one"));
        QCOMPARE(Projects::addArticle(p, water, QStringLiteral("changed")), a);  // a new note replaces it
        QCOMPARE(Projects::items(p).first().note, QStringLiteral("changed"));
        QCOMPARE(Projects::addArticle(p, {2, QStringLiteral("Water"), QStringLiteral("Water")}) != a, true);  // another library's Water
        QCOMPARE(Projects::containing(1, QStringLiteral("Water")), QList<qint64>{p});
        QCOMPARE(Projects::addArticle(p + 100, water), 0);  // no such project
    }

    void anExcerptKeepsItsTextSourcesAndEmbedding()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        const QList<ArticleRef> refs{{1, QStringLiteral("Water"), QStringLiteral("Water")}, {1, QStringLiteral("Ice"), QStringLiteral("Ice")}};
        const qint64 c = Projects::addClip(p, QStringLiteral("  Water is polar.  "), refs, QStringLiteral("Polarity"), QStringLiteral("key"));
        QVERIFY(c > 0);
        QCOMPARE(Projects::addClip(p, QStringLiteral("   "), refs), 0);
        auto clip = Projects::items(p, QStringLiteral("clip")).first();
        QCOMPARE(clip.text, QStringLiteral("Water is polar."));
        QCOMPARE(clip.refs.size(), 2);
        QCOMPARE(clip.refs[1].title, QStringLiteral("Ice"));
        QCOMPARE(clip.article.path, QStringLiteral("Water"));  // the first source, for lookups
        QVERIFY(clip.embedding.isEmpty());
        QVERIFY(Projects::setEmbedding(c, {0.5f, -1.25f, 3.0f}, QStringLiteral("nomic-embed-text:latest")));
        clip = Projects::items(p, QStringLiteral("clip")).first();
        QCOMPARE(clip.embedding, (Vector{0.5f, -1.25f, 3.0f}));
        QCOMPARE(clip.embedModel, QStringLiteral("nomic-embed-text"));
        // a saved answer with no single source
        QVERIFY(Projects::addClip(p, QStringLiteral("An answer."), {}) > 0);
    }

    void vectorsSurviveTheBlob()
    {
        const Vector v{1.0f, -0.0f, 3.25e-7f, 12345.5f};
        QCOMPARE(Embedding::fromBlob(Embedding::toBlob(v)), v);
        QVERIFY(Embedding::fromBlob(QByteArray("abc")).isEmpty());  // not a whole number of floats
        QVERIFY(Embedding::fromBlob({}).isEmpty());
    }

    void notesAndRemovalStayInTheirProject()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        const qint64 other = Projects::create(QStringLiteral("Other"));
        const qint64 a = Projects::addArticle(p, {1, QStringLiteral("Water"), QStringLiteral("Water")});
        QVERIFY(!Projects::setNote(other, a, QStringLiteral("x")));  // not that project's item
        QVERIFY(Projects::setNote(p, a, QStringLiteral(" a note ")));
        QCOMPARE(Projects::items(p).first().note, QStringLiteral("a note"));
        QVERIFY(!Projects::removeItem(other, a));
        QVERIFY(Projects::removeItem(p, a));
        QVERIFY(Projects::items(p).isEmpty());
    }

    void chatsAttachedOrStartedInAProject()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        const qint64 general = Chats::create(QStringLiteral("General"));
        const qint64 own = Chats::create(QStringLiteral("Mine"), 0, {}, {}, p);
        const qint64 item = Projects::attachChat(p, general);
        QVERIFY(item > 0);
        QCOMPARE(Projects::attachChat(p, general), item);  // once
        QCOMPARE(Projects::attachChat(p, 999), 0);         // no such chat
        QCOMPARE(Projects::items(p, QStringLiteral("chat")).first().chatTitle, QStringLiteral("General"));
        QCOMPARE(Chats::inProject(p).size(), 1);
        QCOMPARE(Chats::inProject(p).first().id, own);
        QCOMPARE(Projects::byId(p)->chats, 2);
        // a chat started in the project is not attached to it as well (it was listed and counted twice; the review)
        QCOMPARE(Projects::attachChat(p, own), 0);
        QCOMPARE(Projects::byId(p)->chats, 2);
        // deleting a chat detaches it
        QVERIFY(Chats::remove(general));
        QVERIFY(Projects::items(p, QStringLiteral("chat")).isEmpty());
    }

    void deletingAProjectKeepsItsChats()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        const qint64 own = Chats::create(QStringLiteral("Mine"), 0, {}, {}, p);
        Projects::addArticle(p, {1, QStringLiteral("Water"), QStringLiteral("Water")});
        Projects::createDocument(p, QStringLiteral("Notes"));
        QVERIFY(Projects::remove(p));
        QCOMPARE(count(QStringLiteral("SELECT count(*) FROM project_items")), 0);
        QCOMPARE(count(QStringLiteral("SELECT count(*) FROM project_documents")), 0);
        const auto chat = Chats::byId(own);
        QVERIFY(chat);
        QCOMPARE(chat->projectId, 0);
    }

    void removingALibraryRemovesItsArticlesButKeepsExcerpts()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        Projects::addArticle(p, {1, QStringLiteral("Water"), QStringLiteral("Water")});
        Projects::addArticle(p, {2, QStringLiteral("Ray_Charles"), QStringLiteral("Ray Charles")});
        Projects::addClip(p, QStringLiteral("Water is polar."), {{1, QStringLiteral("Water"), QStringLiteral("Water")}});
        Projects::forgetLibrary(1);
        const auto articles = Projects::items(p, QStringLiteral("article"));
        QCOMPARE(articles.size(), 1);
        QCOMPARE(articles.first().article.libraryId, 2);
        QCOMPARE(Projects::items(p, QStringLiteral("clip")).size(), 1);
    }

    void anArticleIsMarkedWithEveryExcerptSavedFromIt()
    {
        const qint64 a = Projects::create(QStringLiteral("Alpha"));
        const qint64 b = Projects::create(QStringLiteral("Beta"));
        const ArticleRef water{1, QStringLiteral("Water"), QStringLiteral("Water")};
        Projects::addClip(a, QStringLiteral("Water is polar."), {water}, {}, QStringLiteral("key"));
        Projects::addClip(b, QStringLiteral("water  is polar."), {water});           // the same passage, saved again
        Projects::addClip(b, QStringLiteral("Ice floats."), {water});
        Projects::addClip(b, QStringLiteral("Elsewhere."), {{1, QStringLiteral("Ice"), QStringLiteral("Ice")}});
        const auto h = Projects::highlightsFor(1, QStringLiteral("Water"));
        QCOMPARE(h.size(), 2);
        QCOMPARE(h[0].text, QStringLiteral("Water is polar."));
        QCOMPARE(h[0].label, QStringLiteral("Saved in “Alpha”: key\nSaved in “Beta”"));
        QCOMPARE(h[1].text, QStringLiteral("Ice floats."));
        QVERIFY(Projects::highlightsFor(2, QStringLiteral("Water")).isEmpty());
    }

    void documentsAreSavedAndListedNewestFirst()
    {
        const qint64 p = Projects::create(QStringLiteral("P"));
        const qint64 d1 = Projects::createDocument(p, QStringLiteral("  "));
        QCOMPARE(Projects::document(d1)->title, QStringLiteral("Untitled"));
        const qint64 d2 = Projects::createDocument(p, QStringLiteral("Draft"), QStringLiteral("# Hi"));
        QTest::qWait(5);
        QVERIFY(Projects::saveDocument(d1, QStringLiteral("Outline"), QStringLiteral("- one")));
        const auto docs = Projects::documents(p);
        QCOMPARE(docs.size(), 2);
        QCOMPARE(docs[0].id, d1);
        QCOMPARE(docs[0].content, QStringLiteral("- one"));
        QCOMPARE(docs[1].id, d2);
        QVERIFY(Projects::removeDocument(d2));
        QVERIFY(!Projects::document(d2));
        QVERIFY(!Projects::saveDocument(d2, QStringLiteral("x"), QStringLiteral("y")));
        QCOMPARE(Projects::createDocument(p + 100, QStringLiteral("x")), 0);
    }

    void linksAndQuotesForDocuments()
    {
        QCOMPARE(Projects::articleLink({2, QStringLiteral("Ray_(film)"), QStringLiteral("Ray (film)")}), QStringLiteral("[Ray (film)](zim://lib2/Ray_%28film%29)"));
        QCOMPARE(Projects::articleLink({1, QStringLiteral("A"), QStringLiteral("[weird] title")}), QStringLiteral("[(weird) title](zim://lib1/A)"));
        // a title with %1 in it is text, not a placeholder (chained arg() filled it in)
        QCOMPARE(Projects::articleLink({7, QStringLiteral("Rate_of_5%1_growth"), QStringLiteral("Rate of 5%1 growth")}),
                 QStringLiteral("[Rate of 5%1 growth](zim://lib7/Rate_of_5%251_growth)"));
        ProjectItem clip;
        clip.text = QStringLiteral("Water is polar.\nIt bends.");
        clip.refs = {{1, QStringLiteral("Water"), QStringLiteral("Water")}};
        clip.section = QStringLiteral("Polarity");
        QCOMPARE(Projects::quote(clip), QStringLiteral("> Water is polar.\n> It bends.\n>\n> — Water (Polarity)"));
    }

    void exportedDocumentsNameTheirSources()
    {
        const QString md = QStringLiteral("See [the film](zim://lib2/Ray_%28film%29) and [Water](zim://lib1/Water), again [water](zim://lib1/Water). "
                                          "A [web link](https://example.org) stays.");
        QCOMPARE(Projects::exportMarkdown(md),
                 QStringLiteral("See the film and Water, again water. A [web link](https://example.org) stays.\n\n"
                                "## References\n\n- Ray (film). Ray Charles.\n- Water. Chemistry.\n"));
        QCOMPARE(Projects::exportMarkdown(QStringLiteral("No links.")), QStringLiteral("No links."));
    }

    void thePreviewShowsTheNotesOfCitedSources()
    {
        ProjectItem water;
        water.kind = QStringLiteral("article");
        water.article = {1, QStringLiteral("Ray_(film)"), QStringLiteral("Ray (film)")};
        water.note = QStringLiteral("the film");
        ProjectItem ice;
        ice.kind = QStringLiteral("article");
        ice.article = {1, QStringLiteral("Ice"), QStringLiteral("Ice")};  // no note
        ProjectItem clip;
        clip.kind = QStringLiteral("clip");
        clip.text = QStringLiteral("Water, being a polar molecule, undergoes strong hydrogen bonding.");
        clip.note = QStringLiteral("key point");
        const QString md = QStringLiteral("See [Ray (film)](zim://lib1/Ray_%28film%29) and [Ice](zim://lib1/Ice).\n\n"
                                          "> Water, being a polar molecule,\n> undergoes strong hydrogen bonding.\n>\n> — Water\n\nAfter.\n\n"
                                          "> Some other quotation entirely, not saved anywhere at all.");
        QCOMPARE(Projects::previewMarkdown(md, {water, ice, clip}),
                 QStringLiteral("See [Ray (film)](zim://lib1/Ray_%28film%29) *(note: the film)* and [Ice](zim://lib1/Ice).\n\n"
                                "> Water, being a polar molecule,\n> undergoes strong hydrogen bonding.\n>\n> — Water\n\n*Your note: key point*\n\nAfter.\n\n"
                                "> Some other quotation entirely, not saved anywhere at all."));
        // a lightly trimmed quote still counts; a short one does not
        QVERIFY(Projects::previewMarkdown(QStringLiteral("> being a polar molecule, undergoes strong hydrogen"), {clip}).contains(QLatin1String("Your note")));
        QVERIFY(!Projects::previewMarkdown(QStringLiteral("> polar molecule"), {clip}).contains(QLatin1String("Your note")));
        // a note the document already shows (a summary draft writes them out) is not repeated
        const QString summary = QStringLiteral("### 1. [Ray (film)](zim://lib1/Ray_%28film%29)\n\n> **Your note:** the film");
        QCOMPARE(Projects::previewMarkdown(summary, {water}), summary);
    }

    void aWholeProjectExportsAsOneFile()
    {
        const qint64 p = Projects::create(QStringLiteral("Water"), QStringLiteral("Why is water a good solvent?"));
        Projects::createDocument(p, QStringLiteral("First"), QStringLiteral("About [Water](zim://lib1/Water)."));
        QTest::qWait(5);
        Projects::createDocument(p, QStringLiteral("Second"), QStringLiteral("More."));
        Projects::addArticle(p, {1, QStringLiteral("Water"), QStringLiteral("Water")}, QStringLiteral("start here"));
        Projects::addClip(p, QStringLiteral("Water is polar."), {{1, QStringLiteral("Water"), QStringLiteral("Water")}}, {}, QStringLiteral("key"));
        const QString out = Projects::exportProject(p);
        QVERIFY2(out.startsWith(QLatin1String("# Water\n\nWhy is water a good solvent?\n\n---\n\n## First\n\nAbout Water.\n\n## References\n\n- Water. Chemistry.")), qPrintable(out));
        QVERIFY(out.indexOf(QLatin1String("## First")) < out.indexOf(QLatin1String("## Second")));
        QVERIFY(out.contains(QStringLiteral("## Sources\n\n- Water (Chemistry) — start here")));
        QVERIFY(out.contains(QStringLiteral("## Saved excerpts\n\n> Water is polar.\n>\n> — Water\n\nNote: key")));
        QVERIFY(out.endsWith(QLatin1Char('\n')));
        QCOMPARE(Projects::slug(QStringLiteral("Why is water polar?")), QStringLiteral("why-is-water-polar"));
        QCOMPARE(Projects::slug(QStringLiteral("???"), QStringLiteral("project")), QStringLiteral("project"));
    }
};

QTEST_GUILESS_MAIN(ProjectsTest)
#include "projectstest.moc"
