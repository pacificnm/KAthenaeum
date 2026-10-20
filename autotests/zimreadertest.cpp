// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"
#include "core/history.h"
#include "core/library.h"
#include "core/zimreader.h"

// Needs KATHENAEUM_TEST_ZIM (a Wikipedia ZIM); every test is skipped without it.
class ZimReaderTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    qint64 m_id = 0;

private Q_SLOTS:
    void initTestCase()
    {
        const QString zim = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (zim.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run these tests");
        QVERIFY(Database::open(m_dir.filePath(QStringLiteral("t.sqlite")), nullptr));
        const auto id = Library::add(zim);
        QVERIFY(id);
        m_id = *id;
    }

    void aToZListsEveryArticleExactlyOnce()
    {
        // every non-redirect article lands in exactly one letter, and paging loses or repeats nothing
        QStringList letters{QStringLiteral("#")};
        for (char c = 'A'; c <= 'Z'; ++c)
            letters << QString(QLatin1Char(c));
        QSet<QString> seen;
        int total = 0;
        for (const QString &letter : std::as_const(letters)) {
            int from = 0, pages = 0;
            while (from >= 0) {
                const auto page = ZimReader::titles(m_id, letter, from, 7);
                QVERIFY(page.entries.size() <= 7);
                for (const ZimEntry &e : page.entries) {
                    QVERIFY2(!seen.contains(e.path), qPrintable(e.path));
                    seen.insert(e.path);
                    if (letter != QLatin1String("#"))
                        QCOMPARE(e.title.left(1).toUpper(), letter);
                    QVERIFY(!e.title.endsWith(QLatin1String("(disambiguation)")));
                }
                total += int(page.entries.size());
                QVERIFY(page.next == -1 || page.next > from);
                from = page.next;
                QVERIFY(++pages < 100000);
            }
        }
        // compare with a plain walk over every title
        int articles = 0;
        for (const QString &letter : std::as_const(letters))
            articles += int(ZimReader::titles(m_id, letter, 0, 1000000).entries.size());
        QCOMPARE(total, articles);
        QVERIFY(total > 0);
        // the ZIM's own front page (mainPage -> index, a list of titles) is not an article
        QVERIFY(ZimReader::hasEntry(m_id, QStringLiteral("index")));
        QVERIFY(!seen.contains(QStringLiteral("index")));
    }

    void randomArticlesAreArticles()
    {
        for (int i = 0; i < 20; ++i) {
            const auto e = ZimReader::randomArticle(m_id);
            QVERIFY(e);
            const auto c = ZimReader::content(m_id, e->path);
            QVERIFY(c);
            QCOMPARE(c->path, e->path);  // not a redirect
            QVERIFY(c->mimetype.startsWith(QLatin1String("text/html")));
        }
    }

    void contentFollowsRedirectsAndRefusesUnknownPaths()
    {
        QVERIFY(!ZimReader::content(m_id, QStringLiteral("No_such_article_here")));
        QVERIFY(!ZimReader::hasEntry(m_id, QStringLiteral("No_such_article_here")));
        QVERIFY(!ZimReader::content(999999, QStringLiteral("Water")));  // no such library
        const auto e = ZimReader::randomArticle(m_id);
        QVERIFY(ZimReader::hasEntry(m_id, e->path));
    }

    void prefixesAndSuggestions()
    {
        const auto e = ZimReader::randomArticle(m_id);
        const QString start = e->title.left(3);
        const auto list = ZimReader::withPrefix(m_id, start, 5);
        QVERIFY(!list.isEmpty());
        for (const ZimEntry &x : list)
            QVERIFY(x.title.startsWith(start));
        const auto suggestions = ZimReader::suggest(m_id, e->title, 5);
        QVERIFY(!suggestions.isEmpty());
        QVERIFY(ZimReader::suggest(m_id, QStringLiteral("   "), 5).isEmpty());
    }

    void prefixPagingMatchesThePrefixList()
    {
        const auto e = ZimReader::randomArticle(m_id);
        const QString prefix = e->title.left(2);
        QList<ZimEntry> paged;
        for (int from = 0; from >= 0;) {
            const auto page = ZimReader::titlesWithPrefix(m_id, prefix, from, 3);
            paged += page.entries;
            from = page.next;
        }
        const auto all = ZimReader::withPrefix(m_id, prefix, 1000000);
        QCOMPARE(paged.size(), all.size());
        for (int i = 0; i < all.size(); ++i)
            QCOMPARE(paged[i].path, all[i].path);
        QVERIFY(ZimReader::titlesWithPrefix(m_id, QStringLiteral("\u00ff\u00ffNothing starts like this"), 0, 10).entries.isEmpty());
    }

    void historyKeepsTheLatestFirst()
    {
        History::record(m_id, QStringLiteral("A"), QStringLiteral("Alpha"));
        History::record(m_id, QStringLiteral("B"), QStringLiteral("Beta"));
        History::record(m_id, QStringLiteral("A"), QStringLiteral("Alpha again"));
        const auto recent = History::recent(m_id, 10);
        QCOMPARE(recent.size(), 2);
        QCOMPARE(recent[0].path, QStringLiteral("A"));
        QCOMPARE(recent[0].title, QStringLiteral("Alpha again"));
        QVERIFY(Library::remove(m_id, false));
        QVERIFY(History::recent(m_id, 10).isEmpty());  // goes with its library
    }
};

QTEST_GUILESS_MAIN(ZimReaderTest)
#include "zimreadertest.moc"
