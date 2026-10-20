// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"
#include "core/highlights.h"

// Highlights and their notes (schema v9)
class HighlightsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private Q_SLOTS:
    void init()
    {
        QString error;
        QVERIFY2(Database::open(m_dir.filePath(QStringLiteral("%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), &error),
                 qPrintable(error));
    }
    void cleanup() { Database::close(); }

    void aPassageIsHighlightedOnce()
    {
        const qint64 a = Highlights::add(1, QStringLiteral("Water"), QStringLiteral("Water"), QStringLiteral("  Water is a polar molecule.  "));
        QVERIFY(a > 0);
        const auto h = Highlights::byId(a);
        QCOMPARE(h->text, QStringLiteral("Water is a polar molecule."));
        QCOMPARE(h->color, QStringLiteral("yellow"));  // the default
        // the same passage again (other spacing and case): the same highlight, its colour and note changed
        QCOMPARE(Highlights::add(1, QStringLiteral("Water"), QStringLiteral("Water"), QStringLiteral("water is a POLAR\nmolecule."),
                                 QStringLiteral("green"), QStringLiteral("why it dissolves salt")),
                 a);
        QCOMPARE(Highlights::byId(a)->color, QStringLiteral("green"));
        QCOMPARE(Highlights::byId(a)->note, QStringLiteral("why it dissolves salt"));
        // again without a note: the note stays
        QCOMPARE(Highlights::add(1, QStringLiteral("Water"), QStringLiteral("Water"), QStringLiteral("Water is a polar molecule.")), a);
        QCOMPARE(Highlights::byId(a)->note, QStringLiteral("why it dissolves salt"));
        // in another article it is another highlight
        QVERIFY(Highlights::add(1, QStringLiteral("Ice"), QStringLiteral("Ice"), QStringLiteral("Water is a polar molecule.")) != a);
        QCOMPARE(Highlights::count(), 2);
    }

    void whatCannotBeHighlighted()
    {
        QVERIFY(Highlights::tooShort(QStringLiteral("wet")));
        QVERIFY(Highlights::tooShort(QStringLiteral("a b c d e f g")));  // seven letters
        QVERIFY(!Highlights::tooShort(QStringLiteral("photosynthesis")));
        QCOMPARE(Highlights::add(1, QStringLiteral("Water"), {}, QStringLiteral("wet")), 0);
        QCOMPARE(Highlights::add(0, QStringLiteral("Water"), {}, QStringLiteral("long enough passage")), 0);
        QCOMPARE(Highlights::add(1, QString(), {}, QStringLiteral("long enough passage")), 0);
        // an unknown colour is the default one; setting one is refused
        const qint64 id = Highlights::add(1, QStringLiteral("Water"), {}, QStringLiteral("long enough passage"), QStringLiteral("purple"));
        QCOMPARE(Highlights::byId(id)->color, QStringLiteral("yellow"));
        QVERIFY(!Highlights::setColor(id, QStringLiteral("purple")));
        QVERIFY(Highlights::setColor(id, QStringLiteral("pink")));
        QVERIFY(Highlights::setNote(id, QStringLiteral("  a note ")));
        QCOMPARE(Highlights::byId(id)->note, QStringLiteral("a note"));
        QVERIFY(Highlights::remove(id));
        QVERIFY(!Highlights::remove(id));
        QVERIFY(!Highlights::byId(id));
    }

    void theArticleMarksThem()
    {
        const qint64 id = Highlights::add(3, QStringLiteral("Ice"), QStringLiteral("Ice"), QStringLiteral("Ice floats on water."), QStringLiteral("blue"),
                                          QStringLiteral("density"));
        const QList<Highlight> marks = Highlights::marksFor(3, QStringLiteral("Ice"));
        QCOMPARE(marks.size(), 1);
        QCOMPARE(marks[0].cssClass, QStringLiteral("hl hl-blue"));
        QCOMPARE(marks[0].anchor, QStringLiteral("hl-%1").arg(id));
        QCOMPARE(marks[0].label, QStringLiteral("density"));
        QVERIFY(Highlights::marksFor(3, QStringLiteral("Water")).isEmpty());
    }

    void allOfThemByArticleAndFiltered()
    {
        Highlights::add(1, QStringLiteral("Water"), QStringLiteral("Water"), QStringLiteral("Water is a polar molecule."), QStringLiteral("yellow"));
        QTest::qWait(5);
        Highlights::add(1, QStringLiteral("Ice"), QStringLiteral("Ice"), QStringLiteral("Ice floats on water."), QStringLiteral("blue"), QStringLiteral("density"));
        QTest::qWait(5);
        Highlights::add(1, QStringLiteral("Water"), QStringLiteral("Water"), QStringLiteral("It dissolves many salts."), QStringLiteral("blue"));
        // grouped by article, the one changed last first; within it, in the order made
        const auto all = Highlights::all();
        QCOMPARE(all.size(), 3);
        QCOMPARE(all[0].text, QStringLiteral("Water is a polar molecule."));
        QCOMPARE(all[1].text, QStringLiteral("It dissolves many salts."));
        QCOMPARE(all[2].path, QStringLiteral("Ice"));
        QCOMPARE(Highlights::all(QStringLiteral("blue")).size(), 2);
        QCOMPARE(Highlights::all({}, QStringLiteral("density")).size(), 1);  // a note matches
        QCOMPARE(Highlights::all({}, QStringLiteral("ice")).size(), 1);      // so does an article's title
        QCOMPARE(Highlights::all({}, QStringLiteral("100%")).size(), 0);     // LIKE's wildcards are only letters

        // as Markdown, by article: a heading linking to it, each passage quoted, its note under it
        QCOMPARE(Highlights::markdown(Highlights::all()),
                 QStringLiteral("## [Water](zim://lib1/Water)\n\n> Water is a polar molecule.\n\n> It dissolves many salts.\n\n"
                                "## [Ice](zim://lib1/Ice)\n\n> Ice floats on water.\n\ndensity\n"));
        QCOMPARE(Highlights::markdown({}), QString());
    }
};

QTEST_GUILESS_MAIN(HighlightsTest)
#include "highlightstest.moc"
