// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"
#include "core/extract.h"
#include "core/library.h"
#include "core/zimreader.h"

// Ported from Athenaeum's tests/test_extract.py: HTML -> passages. The rules search snippets and Ask rest on.
class ExtractTest : public QObject
{
    Q_OBJECT

    static QByteArray page(const QString &body)
    {
        return QStringLiteral("<html><head><title>T</title></head><body><div class='mw-parser-output'>%1</div></body></html>").arg(body).toUtf8();
    }
    static QString all(const ExtractedArticle &a)
    {
        QStringList t;
        for (const Passage &p : a.passages)
            t << p.text;
        return t.join(QLatin1Char(' '));
    }
    static QStringList sections(const ExtractedArticle &a)
    {
        QStringList s;
        for (const Passage &p : a.passages)
            s << p.section;
        return s;
    }

private Q_SLOTS:
    void titleAndBasicPassage()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>%1</p>").arg(QStringLiteral("Water is a chemical compound. ").repeated(3))));
        QCOMPARE(a.title, QStringLiteral("T"));
        QCOMPARE(a.passages.size(), 1);
        QVERIFY(a.passages[0].section.isEmpty());
    }

    void passagesNeverExceedTheLimit()
    {
        QStringList sentences;
        for (int i = 0; i < 400; ++i)
            sentences << QStringLiteral("Sentence number %1 about water and ice").arg(i);
        const auto a = Extract::extract(page(QStringLiteral("<p>%1.</p>").arg(sentences.join(QStringLiteral(". ")))));
        QVERIFY(a.passages.size() > 3);
        for (const Passage &p : a.passages)
            QVERIFY(p.text.size() <= Extract::MaxChars);
    }

    void unbrokenTextIsHardCutNotLost()
    {
        const QString blob(Extract::MaxChars * 2 + 50, QLatin1Char('x'));
        const QStringList parts = Extract::splitLong(blob);
        QCOMPARE(parts.join(QString()), blob);
        for (const QString &p : parts)
            QVERIFY(p.size() <= Extract::MaxChars);
    }

    void decimalNumbersDoNotSplitSentences()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>Ice has a density of 0.917 g/cm3 at 0 degrees. It floats on liquid water.</p>")));
        QVERIFY(a.passages[0].text.contains(QLatin1String("0.917")));
    }

    void sectionsBecomeSeparatePassagesWithHeadings()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>%1</p><h2>Properties</h2><p>%2</p>")
                                                 .arg(QStringLiteral("Intro text here. ").repeated(5), QStringLiteral("Property text here. ").repeated(5))));
        QCOMPARE(sections(a), (QStringList{QString(), QStringLiteral("Properties")}));
    }

    void referenceLikeSectionsAreSkipped()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>%1</p><h2>References</h2><p>%2</p><h2>External links</h2><ul><li>link one is here for sure</li></ul>")
                                                 .arg(QStringLiteral("Real content here. ").repeated(5), QStringLiteral("Cited paper. ").repeated(9))));
        QCOMPARE(a.passages.size(), 1);
        QVERIFY(!a.passages[0].text.contains(QLatin1String("Cited")));
    }

    void h3UnderASkippedH2StaysSkippedAndH2Resets()
    {
        const auto a = Extract::extract(page(QStringLiteral("<h2>See also</h2><h3>Sub</h3><p>%1</p><h2>Uses</h2><p>%2</p>")
                                                 .arg(QStringLiteral("skipped text. ").repeated(6), QStringLiteral("kept text here. ").repeated(6))));
        QCOMPARE(a.passages.size(), 1);
        QCOMPARE(a.passages[0].section, QStringLiteral("Uses"));
    }

    void tablesScriptsStylesAndCitationsRemoved()
    {
        const auto a = Extract::extract(page(QStringLiteral("<style>.x{}</style><script>alert(1)</script><table><tr><td>TABLEDATA</td></tr></table>"
                                                            "<p>Some real prose that is long enough to keep.<sup class='reference'>[1]</sup></p>")));
        const QString t = a.passages[0].text;
        QVERIFY(!t.contains(QLatin1String("alert")) && !t.contains(QLatin1String("TABLEDATA")) && !t.contains(QLatin1String("[1]")) && !t.contains(QLatin1String(".x{")));
    }

    void listItemsKeepTheirMarkerAndNestedOnesAreNotDuplicated()
    {
        const auto a = Extract::extract(page(QStringLiteral("<ul><li>Outer item one of the list with enough words to keep <ul><li>inner item</li></ul></li></ul>")));
        QCOMPARE(a.passages[0].text.count(QLatin1String("inner item")), 1);
        QVERIFY(a.passages[0].text.startsWith(QLatin1String("- ")));
    }

    void tinyFragmentsDroppedButAShortArticleIsKept()
    {
        QVERIFY(Extract::extract(page(QStringLiteral("<p>tiny</p>"))).passages.isEmpty());
        QCOMPARE(Extract::extract(page(QStringLiteral("<p>%1</p>").arg(QString(Extract::MinChars - 1, QLatin1Char('a'))))).passages.size(), 1);
    }

    void emptyAndOddPagesDoNotCrash()
    {
        for (const QByteArray &raw : {QByteArray("<html></html>"), page(QString()), page(QStringLiteral("<p></p><p>   </p>")),
                                      QByteArray("<html><body>plain text only, no content div that is long</body></html>"), QByteArray()})
            Extract::extract(raw);
    }

    void subHeadingInsideASkippedSectionStaysSkippedButLaterSiblingsReturn()
    {
        const auto a = Extract::extract(page(
            QStringLiteral("<h2>Intro</h2><p>%1</p><h3>References</h3><p>%2</p><h3>Other</h3><p>%3</p><h4>Z</h4><p>%4</p><h2>Notes</h2><h3>Sub</h3><p>%5</p><h2>Uses</h2><p>%6</p>")
                .arg(QStringLiteral("Intro text is here and long enough. ").repeated(3), QStringLiteral("cited stuff here. ").repeated(5),
                     QStringLiteral("Other real content is here too. ").repeated(3), QStringLiteral("More real content here. ").repeated(3),
                     QStringLiteral("skipped note text here. ").repeated(4), QStringLiteral("Uses are described here. ").repeated(3))));
        QCOMPARE(sections(a), (QStringList{QStringLiteral("Intro"), QStringLiteral("Other"), QStringLiteral("Z"), QStringLiteral("Uses")}));
    }

    void definitionListsAndBlockQuotesAreReadOnce()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>%1</p><dl><dt>Term</dt><dd>%2</dd></dl><blockquote><p>%3</p></blockquote>")
                                                 .arg(QStringLiteral("Lead sentence of the article. ").repeated(2), QStringLiteral("The definition of the term is here. ").repeated(2),
                                                      QStringLiteral("A quoted passage with plenty of words. ").repeated(2))));
        const QString t = all(a);
        QVERIFY(t.contains(QLatin1String("definition of the term")));
        QCOMPARE(t.count(QLatin1String("quoted passage")), 2);  // twice in the source, and no more
    }

    void paragraphInsideAListItemIsNotDuplicated()
    {
        const auto a = Extract::extract(page(QStringLiteral("<ul><li><p>Paragraph inside a list item that is long enough. </p></li></ul>")));
        QCOMPARE(all(a).count(QLatin1String("Paragraph inside a list item")), 1);
    }

    void lineBreaksSeparateWordsAndMathIsDropped()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>First line goes here<br>second line follows right after it, long enough to keep.</p>"
                                                            "<p>Formula <math><mi>x</mi></math> then plenty more words after it so that it is kept ok.</p>")));
        const QString t = all(a);
        QVERIFY(t.contains(QLatin1String("here second line")));
        QVERIFY(!t.contains(QLatin1String("heresecond")));
    }

    void shortSectionsAreMergedNotLost()
    {
        const auto a = Extract::extract(page(QStringLiteral("<p>%1</p><h2>Tiny</h2><p>Short one.</p>").arg(QStringLiteral("A decent opening paragraph of text. ").repeated(2))));
        QVERIFY(all(a).contains(QLatin1String("Short one.")));
        QCOMPARE(Extract::extract(page(QStringLiteral("<p>One short sentence here.</p>"))).passages.size(), 1);
    }

    void aClassOnTheRootElementDoesNotDropThePage()
    {
        // real mwoffliner pages: the html element's class list contains "thumb", which the drop rules match
        const QByteArray raw = "<html class=\"client-nojs skin-thumbsize-clientpref-standard\"><head><title>T</title></head><body>"
                               "<div class='mw-parser-output'><p>Water is an inorganic compound with the chemical formula H2O.</p>"
                               "<table class='infobox'><tr><td><ul><li>Hydrogen oxide</li></ul></td></tr></table><style>.x{color:red}</style></div></body></html>";
        const auto a = Extract::extract(raw);
        QCOMPARE(a.passages.size(), 1);
        QVERIFY(!a.passages[0].text.contains(QLatin1String("Hydrogen oxide")));  // and the drop rules still apply below the root
        QVERIFY(!a.passages[0].text.contains(QLatin1String("color")));
    }

    void realArticlesGivePassages()
    {
        // the generated pages above are not enough on their own: this checks pages exactly as mwoffliner writes them
        const QString zim = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (zim.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        QTemporaryDir dir;
        QVERIFY(Database::open(dir.filePath(QStringLiteral("t.sqlite")), nullptr));
        const auto id = Library::add(zim);
        QVERIFY(id);
        int withPassages = 0;
        for (int i = 0; i < 25; ++i) {
            const auto entry = ZimReader::randomArticle(*id);
            const auto content = ZimReader::content(*id, entry->path);
            withPassages += !Extract::extract(content->data).passages.isEmpty();
        }
        QCOMPARE(withPassages, 25);
        ZimReader::close(*id);
        Database::close();
    }

    void unparseableInputReturnsNothing()
    {
        const auto a = Extract::extract(QByteArray());
        QVERIFY(a.title.isEmpty() && a.passages.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ExtractTest)
#include "extracttest.moc"
