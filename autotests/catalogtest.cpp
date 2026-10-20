// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTest>

#include "core/catalog.h"

class CatalogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesRealEntries()
    {
        QFile file(QFINDTESTDATA("data/catalog_sample.xml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        const auto entries = Catalog::parse(file.readAll(), Catalog::defaultFeedUrl(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 3); // the entry whose link is not a ZIM is dropped

        const CatalogEntry &chem = entries[0];
        QCOMPARE(chem.name, QStringLiteral("wikipedia_en_chemistry"));
        QCOMPARE(chem.title, QStringLiteral("Chemistry by Wikipedia"));
        QCOMPARE(chem.category, QStringLiteral("wikipedia"));
        QCOMPARE(chem.flavour, QStringLiteral("mini"));
        QCOMPARE(chem.size, qint64(23322624));
        QCOMPARE(chem.filename, QStringLiteral("wikipedia_en_chemistry_mini_2026-10.zim"));
        QCOMPARE(chem.base, QStringLiteral("wikipedia_en_chemistry_mini"));
        QCOMPARE(chem.version, QStringLiteral("2026-10"));
        QCOMPARE(chem.date, QDate(2026, 10, 15));
        QCOMPARE(chem.author, QStringLiteral("Wikipedia"));
        QCOMPARE(chem.publisher, QStringLiteral("openZIM"));
        QVERIFY(chem.articleCount > 0);
        QVERIFY(chem.hasFulltextIndex());
        QCOMPARE(chem.url.host(), QStringLiteral("lb.download.kiwix.org"));
        // relative thumbnail links are resolved against the feed
        QCOMPARE(chem.illustration.host(), QStringLiteral("opds.library.kiwix.org"));
        QVERIFY(chem.illustration.path().startsWith(QStringLiteral("/catalog/v2/illustration/")));

        const CatalogEntry &devdocs = entries[2];
        QVERIFY(devdocs.category.isEmpty());
        QVERIFY(devdocs.flavour.isEmpty());
        QVERIFY(!devdocs.hasFulltextIndex());
    }

    void onlyIllustrationsMayBeLoadedByTheInterface()
    {
        // the one address shape the QML engine may fetch; anything else on kiwix.org could carry text away in its query
        QVERIFY(Catalog::isIllustrationUrl(
            QUrl(QStringLiteral("https://opds.library.kiwix.org/catalog/v2/illustration/9528fea1-132e-9621-e133-e8a6c200e8c7/?size=48"))));
        QVERIFY(
            Catalog::isIllustrationUrl(QUrl(QStringLiteral("https://opds.library.kiwix.org/catalog/v2/illustration/9528fea1-132e-9621-e133-e8a6c200e8c7/"))));
        QVERIFY(!Catalog::isIllustrationUrl(
            QUrl(QStringLiteral("https://opds.library.kiwix.org/catalog/v2/illustration/9528fea1-132e-9621-e133-e8a6c200e8c7/?q=what+did+I+ask"))));
        QVERIFY(!Catalog::isIllustrationUrl(QUrl(QStringLiteral("https://library.kiwix.org/?q=secret"))));
        QVERIFY(
            !Catalog::isIllustrationUrl(QUrl(QStringLiteral("http://opds.library.kiwix.org/catalog/v2/illustration/9528fea1-132e-9621-e133-e8a6c200e8c7/"))));
        QVERIFY(!Catalog::isIllustrationUrl(QUrl(QStringLiteral("https://evil.example/catalog/v2/illustration/9528fea1-132e-9621-e133-e8a6c200e8c7/"))));
    }

    void rejectsBadXml()
    {
        QString error;
        QVERIFY(Catalog::parse("<feed><entry><title>unclosed", QUrl(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(Catalog::parse("<html><body>Bad gateway</body></html>", QUrl(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void refusesEntityBombs()
    {
        const QByteArray bomb =
            "<?xml version=\"1.0\"?><!DOCTYPE feed [<!ENTITY a \"aaaaaaaaaa\">"
            "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\"><!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">"
            "<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\"><!ENTITY e \"&d;&d;&d;&d;&d;&d;&d;&d;&d;&d;\">]>"
            "<feed><entry><title>&e;&e;&e;&e;&e;&e;&e;&e;&e;&e;</title></entry></feed>";
        QString error;
        QVERIFY(Catalog::parse(bomb, QUrl(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void splitsFilenames()
    {
        const auto parts = Catalog::splitFilename(QStringLiteral("wikipedia_en_all_nopic_2026-06.zim"));
        QVERIFY(parts);
        QCOMPARE(parts->first, QStringLiteral("wikipedia_en_all_nopic"));
        QCOMPARE(parts->second, QStringLiteral("2026-06"));
        QVERIFY(!Catalog::splitFilename(QStringLiteral("wikipedia_en_all.zim")));
        QVERIFY(!Catalog::splitFilename(QStringLiteral("wikipedia_en_all_2026-06.zim.meta4")));
    }

    void validatesFilenames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("ok");
        QTest::newRow("normal") << QStringLiteral("wikipedia_en_chemistry_mini_2026-10.zim") << true;
        QTest::newRow("dots") << QStringLiteral("ja.stackoverflow.com_mul_all_2026-02.zim") << true;
        QTest::newRow("traversal") << QStringLiteral("../x.zim") << false;
        QTest::newRow("slash") << QStringLiteral("a/b.zim") << false;
        QTest::newRow("hidden") << QStringLiteral(".zim") << false;
        QTest::newRow("not zim") << QStringLiteral("passwd") << false;
        QTest::newRow("space") << QStringLiteral("a b.zim") << false;
    }
    void validatesFilenames()
    {
        QFETCH(QString, name);
        QFETCH(bool, ok);
        QCOMPARE(Catalog::isValidFilename(name), ok);
    }

    void checksDownloadUrls_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("ok");
        const QString f = QStringLiteral("wikipedia_en_chemistry_mini_2026-10.zim");
        QTest::newRow("kiwix metalink") << QStringLiteral("https://lb.download.kiwix.org/zim/wikipedia/%1.meta4").arg(f) << true;
        QTest::newRow("kiwix direct") << QStringLiteral("https://download.kiwix.org/zim/wikipedia/%1").arg(f) << true;
        QTest::newRow("plain http") << QStringLiteral("http://download.kiwix.org/zim/wikipedia/%1").arg(f) << false;
        QTest::newRow("look-alike host") << QStringLiteral("https://kiwix.org.evil.example/%1").arg(f) << false;
        QTest::newRow("kiwix in the path") << QStringLiteral("https://evil.example/kiwix.org/%1").arg(f) << false;
        QTest::newRow("suffix host") << QStringLiteral("https://evilkiwix.org/%1").arg(f) << false;
        QTest::newRow("user info") << QStringLiteral("https://kiwix.org@evil.example/%1").arg(f) << false;
        QTest::newRow("other file") << QStringLiteral("https://download.kiwix.org/zim/wikipedia/other_2026-10.zim") << false;
    }
    void checksDownloadUrls()
    {
        QFETCH(QString, url);
        QFETCH(bool, ok);
        const QString problem = Catalog::checkDownloadUrl(QUrl(url), QStringLiteral("wikipedia_en_chemistry_mini_2026-10.zim"));
        QCOMPARE(problem.isEmpty(), ok);
    }
};

QTEST_GUILESS_MAIN(CatalogTest)
#include "catalogtest.moc"
