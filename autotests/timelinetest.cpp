// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "core/timeline.h"

// Timelines: dates found in articles' own sentences, in order
class TimelineTest : public QObject
{
    Q_OBJECT

    static double value(const QString &sentence)
    {
        const auto d = Timeline::findDate(sentence, 2026);
        return d ? d->value : qQNaN();
    }
    static QString label(const QString &sentence)
    {
        const auto d = Timeline::findDate(sentence, 2026);
        return d ? d->label : QStringLiteral("<none>");
    }

private Q_SLOTS:
    void datesAsArticlesWriteThem()
    {
        QCOMPARE(label(QStringLiteral("The Battle of Hastings was fought on 14 October 1066 near Hastings.")), QStringLiteral("14 October 1066"));
        QVERIFY(qAbs(value(QStringLiteral("It was fought on 14 October 1066.")) - (1066 + 9 / 12.0 + 13 / 365.0)) < 1e-9);
        QCOMPARE(label(QStringLiteral("The city was incorporated on February 8, 1851, as a town.")), QStringLiteral("February 8, 1851"));
        QCOMPARE(label(QStringLiteral("In March 1890 the mill closed.")), QStringLiteral("March 1890"));
        QCOMPARE(value(QStringLiteral("Antony was defeated at Actium in 31 BC by Octavian.")), -31.0);
        QCOMPARE(value(QStringLiteral("Settled around 10,000 BCE by hunters.")), -10000.0);
        QCOMPARE(value(QStringLiteral("The Western Empire fell in AD 476 when Odoacer took power.")), 476.0);
        QCOMPARE(value(QStringLiteral("European settlers arrived in the 1840s along the river.")), 1845.0);
        QCOMPARE(label(QStringLiteral("The plebs achieved equality by the 4th century BC through reform.")), QStringLiteral("4th century BC"));
        QCOMPARE(value(QStringLiteral("The plebs achieved equality by the 4th century BC through reform.")), -350.0);
        QCOMPARE(value(QStringLiteral("Mount Mazama collapsed about 7,700 years ago, forming the caldera.")), 2026.0 - 7700);
        QCOMPARE(value(QStringLiteral("The dinosaurs died out 66 million years ago after an impact.")), 2026.0 - 66e6);
        QCOMPARE(value(QStringLiteral("Portland was named after Portland, Maine, in 1845 after a coin toss.")), 1845.0);
        // the first date in the sentence
        QCOMPARE(value(QStringLiteral("Built in 1878 and closed in 1894, the furnace was the first.")), 1878.0);
    }

    void numbersThatAreNotDates()
    {
        QVERIFY(!Timeline::findDate(QStringLiteral("The summit stands 1500 metres above the lake below it."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("About 1,850 people lived in the town at the time."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("The lake is 594 m deep, the deepest in the country."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("A population of 1200 inhabitants was recorded there."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("Its catalogue number is #1999 in the list of stars."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("The value rose to 2.1999 after the change in method."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("In May 5 cases were reported in the town."), 2026));  // "May 5" is not a year
        // the Battle of Hastings: "1000–2000 cavalry" is a count, not a year
        QVERIFY(!Timeline::findDate(QStringLiteral("Estimates of William's forces: men including 1000–2000 cavalry and archers."), 2026));
        QVERIFY(!Timeline::findDate(QStringLiteral("The fleet numbered 1200 ships when it sailed for the coast."), 2026));
        // a range of years is a date
        QCOMPARE(value(QStringLiteral("He served in the war from 1914–1918 as a medic.")), 1914.0);
    }

    void eventsFromAnArticle()
    {
        const QList<Passage> passages = {
            {QString(), QStringLiteral("Portland is a city in Oregon. It was named in 1845 after a coin toss. The railroad arrived in 1886.")},
            {QStringLiteral("Key facts"), QStringLiteral("Founded: 1845. Incorporated: 1851.")},
            {QStringLiteral("History"), QStringLiteral("The Clackamas people lived here for at least 10,000 years before settlers. The 1840s brought settlers.")},
        };
        const ArticleRef portland{2, QStringLiteral("Portland,_Oregon"), QStringLiteral("Portland, Oregon")};
        const auto events = Timeline::fromPassages(passages, portland, 2026);
        QCOMPARE(events.size(), 3);  // not the infobox, not the undated sentence, not "10,000 years" (not "ago")
        QCOMPARE(events[0].date.label, QStringLiteral("1845"));
        QCOMPARE(events[2].section, QStringLiteral("History"));

        const auto ordered = Timeline::sorted(events + events);  // repeats dropped
        QCOMPARE(ordered.size(), 3);
        QCOMPARE(ordered[0].date.label, QStringLiteral("1845"));
        QCOMPARE(ordered[1].date.label, QStringLiteral("1840s"));  // a year before the approximate decade it falls in
        QCOMPARE(ordered[2].date.label, QStringLiteral("1886"));
        QVERIFY(Timeline::fromPassages(passages, portland, 2026, 1).size() == 1);
    }

    void periods()
    {
        QCOMPARE(Timeline::period(1845), QStringLiteral("1800s"));
        QCOMPARE(Timeline::period(2021), QStringLiteral("2000s"));
        QCOMPARE(Timeline::period(476), QStringLiteral("5th century"));
        QCOMPARE(Timeline::period(12), QStringLiteral("1st century"));
        QCOMPARE(Timeline::period(-31), QStringLiteral("1st century BC"));
        QCOMPARE(Timeline::period(-350), QStringLiteral("4th century BC"));
        QCOMPARE(Timeline::period(-2500), QStringLiteral("3rd millennium BC"));
        QCOMPARE(Timeline::period(-5674), QStringLiteral("6th millennium BC"));
        QCOMPARE(Timeline::period(-66e6), QStringLiteral("Millions of years ago"));
        QCOMPARE(Timeline::period(-4e9), QStringLiteral("Billions of years ago"));
        QCOMPARE(Timeline::period(-12000), QStringLiteral("Over 10,000 years ago"));
    }

    void asMarkdown()
    {
        const Timeline::Event e{{1845, QStringLiteral("1845"), 1}, QStringLiteral("Named after a coin toss."), {2, QStringLiteral("Portland,_Oregon"), QStringLiteral("Portland, Oregon")}, {}};
        QCOMPARE(Timeline::markdown(QStringLiteral("Portland"), {e}),
                 QStringLiteral("# Timeline: Portland\n\n## 1800s\n\n- **1845**: Named after a coin toss. ([Portland, Oregon](zim://lib2/Portland,_Oregon))\n"));
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "timelinetest.moc"
