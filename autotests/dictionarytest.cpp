// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTest>

#include "core/dictionary.h"
#include "core/library.h"

// Looking a word up in Wiktionary: the two layouts the ZIMs have (the full English edition, a heading per language; the
// Simple English one, none), and the forms of a word to try.
class DictionaryTest : public QObject
{
    Q_OBJECT

    static QByteArray fixture(const char *name)
    {
        QFile f(QFINDTESTDATA(QStringLiteral("data/") + QLatin1String(name)));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private Q_SLOTS:
    void theFullWiktionaryIsReadForEnglishOnly()
    {
        const auto e = Dictionary::parse(fixture("wiktionary_full_happy.html"));
        QVERIFY(e);
        QCOMPARE(e->pronunciation, QStringLiteral("/ˈhæpi/"));
        QCOMPARE(e->parts.size(), 2); // the German adjective is another language's
        QCOMPARE(e->parts[0].name, QStringLiteral("Adjective"));
        QCOMPARE(e->parts[1].name, QStringLiteral("Noun"));

        const auto &senses = e->parts[0].senses;
        QCOMPARE(senses.size(), 4); // at most four, of five
        // the definition alone: not its synonyms, examples or quotations
        QCOMPARE(senses[0].definition, QStringLiteral("Having a feeling arising from a consciousness of well-being or of enjoyment; contented, joyous."));
        QCOMPARE(senses[0].example, QStringLiteral("Music makes me feel happy.")); // the synonyms in the same list are skipped
        // its label kept, its reference mark and sub-senses not
        QCOMPARE(senses[1].definition, QStringLiteral("(archaic) Experiencing the effect of favourable fortune; favored by hap; lucky."));
        QCOMPARE(senses[1].example, QString());
        QCOMPARE(senses[2].example, QStringLiteral("I'd be happy to help."));
        QCOMPARE(e->parts[1].senses.first().definition, QStringLiteral("(colloquial) A happy event, thing, person, etc."));

        // asked for German, the German entry
        const auto de = Dictionary::parse(fixture("wiktionary_full_happy.html"), QStringLiteral("German"));
        QVERIFY(de);
        QCOMPARE(de->pronunciation, QStringLiteral("/ˈhɛpi/"));
        QCOMPARE(de->parts.size(), 1);
        QVERIFY(de->parts[0].senses[0].definition.contains(QLatin1String("German sense")));
        QVERIFY(!Dictionary::parse(fixture("wiktionary_full_happy.html"), QStringLiteral("French")));
    }

    void theSimpleWiktionaryHasNoLanguageHeadings()
    {
        // the real page for "run" from wiktionary_en-simple_all_nopic_2026-10
        const auto e = Dictionary::parse(fixture("wiktionary_simple_run.html"));
        QVERIFY(e);
        QVERIFY(!e->pronunciation.isEmpty());
        QCOMPARE(e->parts.first().name, QStringLiteral("Verb"));
        const auto &first = e->parts.first().senses.first();
        QCOMPARE(first.definition, QStringLiteral("If you run, you go quickly, with both feet off the ground at one time.")); // no "Synonym: hurry"
        QCOMPARE(first.example, QStringLiteral("I must run."));
    }

    void aFormPointsAtItsWord()
    {
        Dictionary::Entry plural{QStringLiteral("molecules"),
                                 {},
                                 {{QStringLiteral("Noun"), {{QStringLiteral("The plural form of molecule; more than one (kind of) molecule."), {}}}}}};
        QCOMPARE(Dictionary::baseForm(plural), QStringLiteral("molecule"));
        Dictionary::Entry studies{QStringLiteral("studies"),
                                  {},
                                  {{QStringLiteral("Noun"), {{QStringLiteral("The plural form of study."), {}}}},
                                   {QStringLiteral("Verb"), {{QStringLiteral("The third-person singular form of study."), {}}}}}};
        QCOMPARE(Dictionary::baseForm(studies), QStringLiteral("study"));
        Dictionary::Entry ran{QStringLiteral("ran"), {}, {{QStringLiteral("Verb"), {{QStringLiteral("(archaic) simple past tense of run"), {}}}}}};
        QCOMPARE(Dictionary::baseForm(ran), QStringLiteral("run"));
        // a word with a meaning of its own is not a form, even if one sense is
        Dictionary::Entry left{QStringLiteral("left"),
                               {},
                               {{QStringLiteral("Verb"), {{QStringLiteral("The simple past of leave."), {}}}},
                                {QStringLiteral("Adjective"), {{QStringLiteral("On the west side when facing north."), {}}}}}};
        QCOMPARE(Dictionary::baseForm(left), QString());
        // "a form of government" is far into a definition of its own
        Dictionary::Entry democracy{QStringLiteral("democracy"),
                                    {},
                                    {{QStringLiteral("Noun"), {{QStringLiteral("Democracy is a system of government; a form of rule by the people."), {}}}}}};
        QCOMPARE(Dictionary::baseForm(democracy), QString());
    }

    void notAnEntry()
    {
        QVERIFY(!Dictionary::parse(QByteArray()));
        QVERIFY(!Dictionary::parse("<html><body><div class='mw-parser-output'><p>Just a page.</p></div></body></html>"));
        QVERIFY(!Dictionary::parse("<<<not html at all"));
    }

    void theFormsOfAWordToTry()
    {
        // the ordinary word first, then as written; punctuation round it dropped
        QCOMPARE(Dictionary::candidates(QStringLiteral("“Water,”")).mid(0, 2), (QStringList{QStringLiteral("water"), QStringLiteral("Water")}));
        QVERIFY(Dictionary::candidates(QStringLiteral("running")).contains(QStringLiteral("run")));
        QVERIFY(Dictionary::candidates(QStringLiteral("studies")).contains(QStringLiteral("study")));
        QVERIFY(Dictionary::candidates(QStringLiteral("making")).contains(QStringLiteral("make")));
        QVERIFY(Dictionary::candidates(QStringLiteral("molecules")).contains(QStringLiteral("molecule")));
        QVERIFY(Dictionary::candidates(QStringLiteral("hoped")).contains(QStringLiteral("hope")));
        QCOMPARE(Dictionary::candidates(QStringLiteral("NASA")).constFirst(), QStringLiteral("NASA")); // capitals inside: as written
        QCOMPARE(Dictionary::candidates(QStringLiteral("carbon dioxide")), (QStringList{QStringLiteral("carbon dioxide"), QStringLiteral("Carbon dioxide")}));
        QVERIFY(Dictionary::candidates(QStringLiteral("  ... ")).isEmpty());
        QVERIFY(!Dictionary::candidates(QStringLiteral("is")).contains(QStringLiteral("i"))); // never shorter than two letters
    }

    void whichLibrariesAreDictionaries()
    {
        LibraryRecord r;
        r.name = QStringLiteral("wiktionary_en_all");
        QVERIFY(Dictionary::isDictionary(r));
        r.name = QStringLiteral("wikipedia_en_all");
        QVERIFY(!Dictionary::isDictionary(r));
    }
};

QTEST_GUILESS_MAIN(DictionaryTest)
#include "dictionarytest.moc"
