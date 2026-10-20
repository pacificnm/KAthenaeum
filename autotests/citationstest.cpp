// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "core/citations.h"
#include "core/library.h"

// Citing an article in the styles schools ask for. The forms follow each guide's pattern for a wiki article (MLA 9,
// APA 7, Chicago 17 bibliography), with the offline copy said and dated.
class CitationsTest : public QObject
{
    Q_OBJECT

    static LibraryRecord wikipedia()
    {
        LibraryRecord r;
        r.name = QStringLiteral("wikipedia_en_all");
        r.title = QStringLiteral("Wikipedia");
        r.creator = QStringLiteral("Wikipedia");
        r.date = QStringLiteral("2026-08-25");
        return r;
    }

private Q_SLOTS:
    void aWikipediaArticleIsCitedAsTheWebsiteItCopies()
    {
        const auto s = Citations::source(wikipedia(), QStringLiteral("Portland,_Oregon"), QStringLiteral("Portland, Oregon"));
        QCOMPARE(s.site, QStringLiteral("Wikipedia"));
        QCOMPARE(s.publisher, QStringLiteral("Wikimedia Foundation"));
        QCOMPARE(s.url, QStringLiteral("https://en.wikipedia.org/wiki/Portland,_Oregon"));
        QCOMPARE(s.copied, QDate(2026, 8, 25));
        const QDate today(2026, 10, 20);

        QCOMPARE(Citations::format(Citations::Style::Mla, s, today),
                 QStringLiteral("“Portland, Oregon.” Wikipedia, Wikimedia Foundation, 25 Aug. 2026, en.wikipedia.org/wiki/Portland,_Oregon. "
                                "Offline copy via Kiwix. Accessed 20 Oct. 2026."));
        QCOMPARE(Citations::format(Citations::Style::Mla, s, today, true).left(36), QStringLiteral("“Portland, Oregon.” *Wikipedia*, Wik"));
        QCOMPARE(Citations::format(Citations::Style::Apa, s, today),
                 QStringLiteral("Portland, Oregon. (2026, August 25). In Wikipedia. Offline copy via Kiwix. "
                                "Retrieved October 20, 2026, from https://en.wikipedia.org/wiki/Portland,_Oregon"));
        QCOMPARE(Citations::format(Citations::Style::Chicago, s, today, true), // Chicago sets a website's name in roman
                 QStringLiteral("Wikipedia. “Portland, Oregon.” Wikimedia Foundation. Offline copy of August 25, 2026, via Kiwix. "
                                "Accessed October 20, 2026. https://en.wikipedia.org/wiki/Portland,_Oregon."));
        QVERIFY(Citations::html(Citations::Style::Apa, s, today).startsWith(QLatin1String("Portland, Oregon. (2026, August 25). In <i>Wikipedia</i>.")));
    }

    void addressesAndLanguages()
    {
        LibraryRecord simple = wikipedia();
        simple.name = QStringLiteral("wiktionary_en-simple_all");
        QCOMPARE(Citations::source(simple, QStringLiteral("run"), QStringLiteral("run")).url, QStringLiteral("https://simple.wiktionary.org/wiki/run"));
        QCOMPARE(Citations::source(simple, QStringLiteral("run"), QStringLiteral("run")).site, QStringLiteral("Wiktionary"));
        // parentheses and commas kept as Wikipedia writes them; anything else that needs it encoded
        QCOMPARE(Citations::source(wikipedia(), QStringLiteral("Ray_(film)"), {}).url, QStringLiteral("https://en.wikipedia.org/wiki/Ray_(film)"));
        QCOMPARE(Citations::source(wikipedia(), QStringLiteral("Café_au_lait"), {}).url, QStringLiteral("https://en.wikipedia.org/wiki/Caf%C3%A9_au_lait"));
        QCOMPARE(Citations::source(wikipedia(), QStringLiteral("AC/DC"), {}).url, QStringLiteral("https://en.wikipedia.org/wiki/AC/DC"));
        QCOMPARE(Citations::source(wikipedia(), QStringLiteral("What?_(song)"), {}).url, QStringLiteral("https://en.wikipedia.org/wiki/What%3F_(song)"));
    }

    void anotherLibraryIsCitedAsTheCollectionItIs()
    {
        LibraryRecord r;
        r.name = QStringLiteral("gutenberg_en_lcc-ph");
        r.title = QStringLiteral("Project Gutenberg Library");
        r.creator = QStringLiteral("gutenberg.org");
        const auto s = Citations::source(r, QStringLiteral("A/Hamlet"), QStringLiteral("Hamlet"));
        QVERIFY(s.url.isEmpty());
        QCOMPARE(Citations::format(Citations::Style::Mla, s, QDate(2026, 10, 20)),
                 QStringLiteral("“Hamlet.” Project Gutenberg Library, gutenberg.org. Offline copy via Kiwix. Accessed 20 Oct. 2026."));
        QCOMPARE(Citations::format(Citations::Style::Apa, s, QDate(2026, 10, 20)), // no date: n.d.
                 QStringLiteral("Hamlet. (n.d.). In Project Gutenberg Library. gutenberg.org. Offline copy via Kiwix. Retrieved October 20, 2026."));
    }

    void titlesKeepTheirOwnPunctuation()
    {
        Citations::Source s;
        s.title = QStringLiteral("What Is Life?");
        s.site = QStringLiteral("Wikipedia");
        QVERIFY(Citations::format(Citations::Style::Mla, s, {}).startsWith(QStringLiteral("“What Is Life?” Wikipedia.")));
        QVERIFY(Citations::format(Citations::Style::Apa, s, {}).startsWith(QLatin1String("What Is Life? (n.d.).")));
        // a title with an asterisk does not turn the rest italic in Markdown
        s.site = QStringLiteral("M*A*S*H");
        QVERIFY(Citations::format(Citations::Style::Mla, s, {}, true).contains(QLatin1String("*M\\*A\\*S\\*H*")));
        // nothing from a ZIM becomes markup in the HTML copy
        s.title = QStringLiteral("<img src=x>");
        QVERIFY(!Citations::html(Citations::Style::Mla, s, {}).contains(QLatin1String("<img")));
    }

    void aListIsAlphabeticalWithoutRepeats()
    {
        const QDate today(2026, 10, 20);
        const QList<Citations::Source> sources = {Citations::source(wikipedia(), QStringLiteral("Water"), QStringLiteral("Water")),
                                                  Citations::source(wikipedia(), QStringLiteral("acid"), QStringLiteral("acid")),
                                                  Citations::source(wikipedia(), QStringLiteral("Water"), QStringLiteral("Water"))};
        const QStringList mla = Citations::sorted(Citations::Style::Mla, sources, today);
        QCOMPARE(mla.size(), 2);
        QVERIFY(mla[0].startsWith(QStringLiteral("“acid.”"))); // the opening quotation mark does not count, nor case
        QVERIFY(mla[1].startsWith(QStringLiteral("“Water.”")));
        const QStringList chicago = Citations::sorted(Citations::Style::Chicago, sources, today);
        QVERIFY(chicago[0].contains(QStringLiteral("“acid.”"))); // all start "Wikipedia.": then by title
    }

    void styleNames()
    {
        QCOMPARE(Citations::style(QStringLiteral("apa")), Citations::Style::Apa);
        QCOMPARE(Citations::style(QStringLiteral("nonsense")), Citations::Style::Mla);
        QCOMPARE(Citations::name(Citations::Style::Chicago), QStringLiteral("chicago"));
        QCOMPARE(Citations::listHeading(Citations::Style::Mla), QStringLiteral("Works Cited"));
        QCOMPARE(Citations::listHeading(Citations::Style::Apa), QStringLiteral("References"));
        QCOMPARE(Citations::listHeading(Citations::Style::Chicago), QStringLiteral("Bibliography"));
    }
};

QTEST_GUILESS_MAIN(CitationsTest)
#include "citationstest.moc"
