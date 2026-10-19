// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "core/articlerenderer.h"
#include "core/database.h"
#include "core/library.h"
#include "core/zimreader.h"

// Ported from Athenaeum's tests/test_render.py: nothing dangerous may survive, links must be rewritten correctly.
class ArticleRendererTest : public QObject
{
    Q_OBJECT

    static QString run(const QString &body, const QString &path = QStringLiteral("Water"),
                       const QSet<QString> &paths = {QStringLiteral("Water"), QStringLiteral("Ice"), QStringLiteral("dir/Sub_page"), QStringLiteral("img/a.png"),
                                                     QStringLiteral("Chemistry:_A_European_Journal")})
    {
        const QByteArray raw = QStringLiteral("<html><head><title>T</title></head><body><h1>Water</h1><div class='mw-parser-output'>%1</div></body></html>").arg(body).toUtf8();
        const auto out = ArticleRenderer::render(raw, path, QStringLiteral("zim://lib7/"), [&](const QString &p) { return paths.contains(p); });
        return out ? out->html : QStringLiteral("<<refused>>");
    }

    static RenderedArticle marked(const QString &body, const QList<Highlight> &highlights)
    {
        const QByteArray raw = QStringLiteral("<html><head><title>T</title></head><body><div class='mw-parser-output'>%1</div></body></html>").arg(body).toUtf8();
        const auto out = ArticleRenderer::render(raw, QStringLiteral("Water"), QStringLiteral("zim://lib7/"), [](const QString &) { return true; }, nullptr, highlights);
        return out ? *out : RenderedArticle{};
    }

private Q_SLOTS:
    void scriptsHandlersAndDangerousUrlsAreStripped()
    {
        const QString low = run(QStringLiteral("<script>alert(1)</script><p onclick=\"x()\" onmouseover=\"y()\">hi</p><a href=\"javascript:alert(1)\">j</a>"
                                               "<a href=\"  JaVaScRiPt:alert(2)\">j2</a><a href=\"data:text/html,<script>1</script>\">d</a>"
                                               "<iframe src=\"//evil\"></iframe><form action=\"/x\"><input name=a></form><object data=\"x\"></object><embed src=\"x\">"))
                                .toLower();
        for (const char *bad : {"<script", "onclick", "onmouseover", "javascript:", "data:text", "<iframe", "<form", "<input", "<object", "<embed"})
            QVERIFY2(!low.contains(QLatin1String(bad)), bad);
        QVERIFY(low.contains(QLatin1String("hi")));
    }

    void styleKeepsHarmlessAndDropsDangerous()
    {
        const QString html = run(QStringLiteral("<p style=\"background:#ccf\">a</p><p style=\"background:url(http://evil/x.png)\">b</p><p style=\"width:expression(alert(1))\">c</p>"));
        QVERIFY(html.contains(QLatin1String("background:#ccf")));
        QVERIFY(!html.contains(QLatin1String("evil")));
        QVERIFY(!html.contains(QLatin1String("expression")));
    }

    void attributesNotOnTheAllowlistAreRemoved()
    {
        const QString html = run(QStringLiteral("<p data-x=\"1\" srcset=\"a\" formaction=\"x\" id=\"ok\" class=\"c\">t</p>"));
        QVERIFY(html.contains(QLatin1String("id=\"ok\"")));
        QVERIFY(html.contains(QLatin1String("class=\"c\"")));
        QVERIFY(!html.contains(QLatin1String("data-x")));
        QVERIFY(!html.contains(QLatin1String("formaction")));
        QVERIFY(!html.contains(QLatin1String("srcset")));
    }

    void internalLinksRewrittenAndMissingLinksDimmed()
    {
        const QString html = run(QStringLiteral("<a href=\"Ice\">ice</a> <a href=\"Nope\">nope</a> <a href=\"Ice#Structure\">frag</a>"));
        QVERIFY(html.contains(QLatin1String("href=\"zim://lib7/Ice\"")));
        QVERIFY(html.contains(QLatin1String("href=\"zim://lib7/Ice#Structure\"")));
        QVERIFY(html.contains(QLatin1String("wl-missing")));
        QVERIFY(html.contains(QLatin1String("nope</span>")));
        QVERIFY(!html.contains(QLatin1String("href=\"Nope\"")));
    }

    void linksWithAColonAreArticlesNotSchemes()
    {
        const QString html = run(QStringLiteral("<a href=\"Chemistry:_A_European_Journal\">j</a>"));
        QVERIFY2(html.contains(QLatin1String("href=\"zim://lib7/Chemistry%3A_A_European_Journal\"")), qPrintable(html));
    }

    void relativeLinksResolveAgainstTheArticleDirectory()
    {
        const QString html = run(QStringLiteral("<a href=\"Sub_page\">s</a><a href=\"../Water\">w</a>"), QStringLiteral("dir/Other"));
        QVERIFY(html.contains(QLatin1String("zim://lib7/dir/Sub_page")));
        QVERIFY(html.contains(QLatin1String("zim://lib7/Water")));
    }

    void dotDotCannotEscapeTheZim()
    {
        QCOMPARE(ArticleRenderer::resolve(QStringLiteral("Water"), QStringLiteral("../../../../etc/passwd")), QStringLiteral("../../../../etc/passwd"));
        const QString html = run(QStringLiteral("<a href=\"../../../../etc/passwd\">p</a>"));
        QVERIFY(!html.contains(QLatin1String("href=")));
        QVERIFY(html.contains(QLatin1String("wl-missing")));
    }

    void resolvesPaths()
    {
        QCOMPARE(ArticleRenderer::resolve(QStringLiteral("dir/Other"), QStringLiteral("../img/a.png")), QStringLiteral("img/a.png"));
        QCOMPARE(ArticleRenderer::resolve(QStringLiteral("Water"), QStringLiteral("./_res_/x.css")), QStringLiteral("_res_/x.css"));
        QCOMPARE(ArticleRenderer::resolve(QStringLiteral("Water"), QStringLiteral("Ice%27s_age#top")), QStringLiteral("Ice's_age"));
        QCOMPARE(ArticleRenderer::resolve(QStringLiteral("Water"), QStringLiteral("#only")), QString());
    }

    void externalLinksOpenElsewhere()
    {
        const QString html = run(QStringLiteral("<a href=\"https://example.org/x\">e</a><a href=\"//cdn.example/x\">e2</a><a href=\"mailto:a@b.c\">m</a>"));
        QCOMPARE(html.count(QLatin1String("rel=\"noopener noreferrer nofollow\"")), 3);
        QCOMPARE(html.count(QLatin1String("target=\"_blank\"")), 3);
        QCOMPARE(html.count(QLatin1String("wl-ext")), 3);
    }

    void imagesPlaceholdersDroppedRealOnesRewrittenUnknownDropped()
    {
        const QString html = run(QStringLiteral("<img src=\"data:image/svg+xml;utf8,<svg/>\"><img src=\"../img/a.png\" srcset=\"x 2x\"><img src=\"missing.png\"><img src=\"https://upload.example/x.png\">"),
                                 QStringLiteral("dir/Other"));
        QCOMPARE(html.count(QLatin1String("<img")), 1);
        QVERIFY(html.contains(QLatin1String("src=\"zim://lib7/img/a.png\"")));
        QVERIFY(!html.contains(QLatin1String("srcset")));
        QVERIFY(!html.contains(QLatin1String("data:")));
        QVERIFY(!html.contains(QLatin1String("upload.example")));  // nothing is fetched from the internet
    }

    void tocCollectsHeadingsWithIds()
    {
        const auto out = ArticleRenderer::render("<html><head><title>T</title></head><body><div class='mw-parser-output'><h2 id='A'>Alpha</h2>"
                                                 "<section id='S'><h3>Beta <span>two</span></h3></section><h2>NoId</h2></div></body></html>",
                                                 QStringLiteral("Water"), QStringLiteral("zim://lib7/"), [](const QString &) { return false; });
        QVERIFY(out);
        QCOMPARE(out->toc.size(), 2);
        QCOMPARE(out->toc[0].id, QStringLiteral("A"));
        QCOMPARE(out->toc[0].level, 2);
        QCOMPARE(out->toc[1].id, QStringLiteral("S"));  // mwoffliner puts the id on the section around the heading
        QCOMPARE(out->toc[1].text, QStringLiteral("Beta two"));
        QCOMPARE(out->toc[1].level, 3);
    }

    void elementsThatCarryUrlsOrCoverThePageAreRemoved()
    {
        const QString low = run(QStringLiteral("<area href=\"javascript:alert(1)\"><video src=\"javascript:x\"></video><audio src=\"x\"></audio><base href=\"//evil/\">"
                                               "<p style=\"position:fixed;top:0;left:0;width:100%;height:100%\">overlay</p><p style=\"z-index:9999\">z</p>"))
                                .toLower();
        for (const char *bad : {"<area", "<video", "<audio", "<base", "position", "z-index"})
            QVERIFY2(!low.contains(QLatin1String(bad)), bad);
    }

    void hrefAndSrcSurviveOnlyOnAAndImg()
    {
        const QString html = run(QStringLiteral("<svg><a href=\"javascript:alert(1)\" xlink:href=\"javascript:2\">s</a></svg><div src=\"x\" href=\"y\">d</div><link href=\"//evil\">"));
        QVERIFY(!html.contains(QLatin1String("javascript")));
        QVERIFY(!html.contains(QLatin1String("src=\"x\"")));
        QVERIFY(!html.contains(QLatin1String("href=\"y\"")));
    }

    void nestedDroppedElementsAreFreedOnce()
    {
        // a script inside a navbox inside a noprint: every one matches the drop list, so dropping each match would free the
        // inner ones twice (run under AddressSanitizer to see it)
        const QString html = run(QStringLiteral("<div class=\"noprint\"><div class=\"navbox\"><script>x</script><style>y</style></div></div><p>kept</p>"));
        QVERIFY(html.contains(QLatin1String("kept")));
        QVERIFY(!html.contains(QLatin1String("navbox")));
    }

    void nonArticleInputIsRefusedNotCrashed()
    {
        for (const QByteArray &raw : {QByteArray(), QByteArray("<p>hi</p>"), QByteArray("just text"), QByteArray("\xff\xfe\x00garbage", 10)})
            QVERIFY(!ArticleRenderer::render(raw, QStringLiteral("Water"), QStringLiteral("zim://lib7/"), [](const QString &) { return true; }));
    }

    void rendersRealArticles()
    {
        const QString zim = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (zim.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        QTemporaryDir dir;
        QVERIFY(Database::open(dir.filePath(QStringLiteral("t.sqlite")), nullptr));
        const auto id = Library::add(zim);
        QVERIFY(id);
        const auto main = ZimReader::randomArticle(*id);
        QVERIFY(main);
        int rendered = 0;
        for (int i = 0; i < 25; ++i) {
            const auto entry = ZimReader::randomArticle(*id);
            const auto content = ZimReader::content(*id, entry->path);
            QVERIFY(content);
            const auto out = ArticleRenderer::render(content->data, content->path, QStringLiteral("zim://lib1/"),
                                                     [&](const QString &p) { return ZimReader::hasEntry(*id, p); });
            QVERIFY2(out, qPrintable(entry->path));
            QVERIFY(!out->title.isEmpty());
            QVERIFY(!out->html.isEmpty());
            const QString low = out->html.toLower();
            QVERIFY(!low.contains(QLatin1String("<script")));
            QVERIFY(!low.contains(QLatin1String("<link")));
            QVERIFY(!low.contains(QLatin1String("./_res_/")));
            QVERIFY(!low.contains(QLatin1String("./_mw_/")));
            ++rendered;
        }
        QCOMPARE(rendered, 25);
        ZimReader::close(*id);
        Database::close();
    }

    // ---- saved excerpts marked in their article (Athenaeum's highlights.ts, done while rendering here) ----

    void aSavedPassageIsMarkedWithItsNote()
    {
        const auto out = marked(QStringLiteral("<p>Water is a polar molecule. It bends light.</p>"),
                                {{QStringLiteral("Water is a polar molecule."), QStringLiteral("Saved in “Water”: key point")}});
        QCOMPARE(out.highlighted, 1);
        QCOMPARE(out.html, QStringLiteral("<p><mark class=\"excerpt-hl\" title=\"Saved in “Water”: key point\">Water is a polar molecule.</mark> It bends light.</p>"));
    }

    void aPassageAcrossALinkAndParagraphsIsMarkedPieceByPiece()
    {
        const auto out = marked(QStringLiteral("<p>Ice is <a href=\"Water\">frozen water</a> that floats.</p><p>It is less dense. More text.</p>"),
                                {{QStringLiteral("frozen water that floats. It is less dense."), QStringLiteral("n")}});
        QCOMPARE(out.highlighted, 1);
        QCOMPARE(out.html.count(QLatin1String("<mark class=\"excerpt-hl\"")), 3);
        QVERIFY2(out.html.contains(QLatin1String("<a href=\"zim://lib7/Water\"><mark class=\"excerpt-hl\" title=\"n\">frozen water</mark></a>")), qPrintable(out.html));
        QVERIFY(out.html.contains(QLatin1String("<mark class=\"excerpt-hl\" title=\"n\"> that floats.</mark></p>")));
        QVERIFY(out.html.contains(QLatin1String("<p><mark class=\"excerpt-hl\" title=\"n\">It is less dense.</mark> More text.</p>")));
        QVERIFY(out.html.startsWith(QLatin1String("<p>Ice is <a")));  // the text before it is untouched
    }

    void spacingAndCaseDoNotMatter()
    {
        const auto out = marked(QStringLiteral("<p>Water   is a POLAR\nmolecule.</p>"), {{QStringLiteral("water is a polar molecule"), QString()}});
        QCOMPARE(out.highlighted, 1);
        QVERIFY(out.html.contains(QLatin1String("<mark class=\"excerpt-hl\">Water   is a POLAR\nmolecule</mark>.")));
    }

    void aPassageNoLongerInTheTextIsNotShown()
    {
        const auto out = marked(QStringLiteral("<p>Water is wet.</p>"), {{QStringLiteral("Something the article never said."), QString()},
                                                                       {QStringLiteral("wet"), QString()}});  // too short to place
        QCOMPARE(out.highlighted, 0);
        QCOMPARE(out.html, QStringLiteral("<p>Water is wet.</p>"));
    }

    void aNoteCannotBecomeMarkup()
    {
        const auto out = marked(QStringLiteral("<p>Water is a polar molecule.</p>"),
                                {{QStringLiteral("Water is a polar molecule."), QStringLiteral("\"><script>alert(1)</script>")}});
        QVERIFY(!out.html.contains(QLatin1String("<script")));
        QVERIFY(out.html.contains(QLatin1String("&lt;script&gt;alert(1)&lt;/script&gt;")));  // shown as text in the tooltip
        // both kinds of quote: the attribute still cannot be closed early
        const auto both = marked(QStringLiteral("<p>Water is a polar molecule.</p>"),
                                 {{QStringLiteral("Water is a polar molecule."), QStringLiteral("it's \"x\"><b onmouseover=y>")}});
        QVERIFY2(!both.html.contains(QLatin1String("<b")), qPrintable(both.html));
        QCOMPARE(both.html.count(QLatin1String("<mark")), 1);
    }

    void twoPassagesInTheSameTextNode()
    {
        const auto out = marked(QStringLiteral("<p>Water is a polar molecule. Ice floats on top of water.</p>"),
                                {{QStringLiteral("Water is a polar molecule."), QStringLiteral("a")}, {QStringLiteral("Ice floats on top of water."), QStringLiteral("b")}});
        QCOMPARE(out.highlighted, 2);
        QCOMPARE(out.html, QStringLiteral("<p><mark class=\"excerpt-hl\" title=\"a\">Water is a polar molecule.</mark> "
                                          "<mark class=\"excerpt-hl\" title=\"b\">Ice floats on top of water.</mark></p>"));
    }

    void cssEscapesAndImageFunctionsAreDropped()
    {
        // "u\72l(" is url() written with a CSS escape: it got past the filter (only the page's CSP stopped it)
        for (const char *style : {"background:u\\72l(http://x/a)", "background:image-set('a.png' 1x)", "content:src(x)", "color:red\\9"})
            QVERIFY2(!run(QStringLiteral("<p style=\"%1\">t</p>").arg(QString::fromLatin1(style))).contains(QLatin1String("style=")), style);
        QVERIFY(run(QStringLiteral("<p style=\"color:red\">t</p>")).contains(QLatin1String("style=\"color:red\"")));
    }
};

QTEST_GUILESS_MAIN(ArticleRendererTest)
#include "articlerenderertest.moc"
