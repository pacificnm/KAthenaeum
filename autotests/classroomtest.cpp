// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/classroom.h"
#include "core/database.h"
#include "core/library.h"
#include "core/teacher.h"

// The classroom (schema v10): reading the teacher's syllabus, levels, which libraries a subject reads, storing courses,
// and choosing a lesson's readings. The cases are what qwen3.5:2b actually wrote (2026-10-20).
class ClassroomTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static LibraryRecord library(qint64 id, const QString &name, const QString &path = QString())
    {
        LibraryRecord r;
        r.id = id;
        r.name = name;
        r.title = name;
        r.path = path.isEmpty() ? QStringLiteral("/proc/self/exe") : path;  // a file that exists
        return r;
    }

    static Teaching::Choice choice(const QString &title, double similarity, qint64 libraryId = 1)
    {
        Teaching::Choice c;
        c.found.libraryId = libraryId;
        c.found.path = QString(title).replace(QLatin1Char(' '), QLatin1Char('_'));
        c.found.title = title;
        c.similarity = similarity;
        return c;
    }

private Q_SLOTS:
    void init()
    {
        QString error;
        QVERIFY2(Database::open(m_dir.filePath(QStringLiteral("%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), &error),
                 qPrintable(error));
    }
    void cleanup() { Database::close(); }

    void theSyllabusAsTheModelWritesIt()
    {
        // the form asked for, with the article named
        const auto units = Classroom::parseSyllabus(QStringLiteral(
            "Unit 1: Introduction to Plate Tectonics and Earthquakes\n"
            "- The movement of tectonic plates | The plate tectonics model | You will learn how the crust moves.\n"
            "- How earthquakes happen | Seismic waves and earthquake magnitude | You will learn about waves.\n\n"
            "Unit 2: Volcanoes, Eruptions, and Famous Events\n"
            "- The formation of volcanoes | Volcano | You will learn how magma rises.\n"), QStringLiteral("Volcanoes"), 6, 5);
        QCOMPARE(units.size(), 2);
        QCOMPARE(units[0].title, QStringLiteral("Introduction to Plate Tectonics and Earthquakes"));
        QCOMPARE(units[0].lessons.size(), 2);
        QCOMPARE(units[0].lessons[0].title, QStringLiteral("The movement of tectonic plates"));
        QCOMPARE(units[0].lessons[0].article, QStringLiteral("The plate tectonics model"));
        QCOMPARE(units[0].lessons[0].objective, QStringLiteral("You will learn how the crust moves."));
        QCOMPARE(units[1].lessons[0].article, QStringLiteral("Volcano"));
        QCOMPARE(units[1].lessons[0].position, 0);
    }

    void theTemplatesOwnWordsAreDropped()
    {
        // the first run: every lesson began "Lesson title:", and all but one were dropped as repeats
        const auto copied = Classroom::parseSyllabus(QStringLiteral(
            "Unit 1: Plates\n"
            "- Lesson title: The Three Types of Plates | what moves\n"
            "- Lesson title: Faults | how they slip\n"
            "- <the lesson's title> | <the Wikipedia article's title> | <one sentence>\n"
            "- **Subduction** | \"Subduction\" | Article: plates sinking\n"), QStringLiteral("Earth"), 6, 5);
        QCOMPARE(copied.size(), 1);
        QCOMPARE(copied[0].lessons.size(), 3);  // the placeholder line is not a lesson
        QCOMPARE(copied[0].lessons[0].title, QStringLiteral("The Three Types of Plates"));
        QCOMPARE(copied[0].lessons[1].title, QStringLiteral("Faults"));
        QCOMPARE(copied[0].lessons[2].title, QStringLiteral("Subduction"));
        QCOMPARE(copied[0].lessons[2].article, QStringLiteral("Subduction"));  // quotes round it dropped
        QCOMPARE(copied[0].lessons[2].objective, QStringLiteral("plates sinking"));
    }

    void otherFormsAndLimits()
    {
        // headings, numbering and bold; lessons before any unit; a unit with no lessons; repeats; too many
        const auto units = Classroom::parseSyllabus(QStringLiteral(
            "Here is the syllabus:\n"
            "1. Counting: numbers to 100\n"
            "## Fractions\n"
            "1. **What a fraction is** - parts of a whole\n"
            "2. Comparing fractions — which is bigger\n"
            "3. What a fraction is - again\n"
            "**Unit 3: Empty**\n"
            "Module 4. Measurement\n"
            "- Units of measurement: metres and inches\n"
            "- Part of speech: not a unit either\n"
            "- Lesson 3: Rulers\n"), QStringLiteral("Mathematics"), 6, 2);
        QCOMPARE(units.size(), 3);
        QCOMPARE(units[0].title, QStringLiteral("Mathematics"));  // the lessons before any unit
        QCOMPARE(units[0].lessons[0].title, QStringLiteral("Counting"));
        QCOMPARE(units[1].title, QStringLiteral("Fractions"));
        QCOMPARE(units[1].lessons.size(), 2);  // the repeat is dropped
        QCOMPARE(units[1].lessons[1].objective, QStringLiteral("which is bigger"));
        QCOMPARE(units[2].title, QStringLiteral("Measurement"));  // "Empty" had no lessons
        QCOMPARE(units[2].lessons.size(), 2);  // at most two each
        QCOMPARE(units[2].lessons[0].title, QStringLiteral("Units of measurement"));  // a lesson, not a unit
        QCOMPARE(units[2].lessons[1].title, QStringLiteral("Part of speech"));
        QCOMPARE(units[2].position, 2);
        QVERIFY(Classroom::parseSyllabus(QStringLiteral("I cannot help with that."), QStringLiteral("X"), 6, 5).size() <= 1);
        QVERIFY(Classroom::parseSyllabus(QString(), QStringLiteral("X"), 6, 5).isEmpty());
    }

    void levels()
    {
        QCOMPARE(Classroom::nextLevel(QStringLiteral("none")), QStringLiteral("k"));
        QCOMPARE(Classroom::nextLevel(QStringLiteral("k")), QStringLiteral("1"));
        QCOMPARE(Classroom::nextLevel(QStringLiteral("8")), QStringLiteral("9"));
        QCOMPARE(Classroom::nextLevel(QStringLiteral("12")), QStringLiteral("college"));
        QCOMPARE(Classroom::nextLevel(QStringLiteral("degree")), QStringLiteral("adult"));
        QVERIFY(Classroom::isYoung(QStringLiteral("k")));
        QVERIFY(Classroom::isYoung(QStringLiteral("5")));
        QVERIFY(!Classroom::isYoung(QStringLiteral("6")));
        QVERIFY(!Classroom::isYoung(QStringLiteral("college")));
        QCOMPARE(Classroom::levelLabel(QStringLiteral("7")), QStringLiteral("Grade 7"));
        QCOMPARE(Classroom::levelForPrompt(QStringLiteral("7")), QStringLiteral("grade 7 (about 12 years old)"));
        QCOMPARE(Classroom::gradeLabel(QStringLiteral("college")), QStringLiteral("Some college"));
        QCOMPARE(Classroom::levels().size(), 15);
        QCOMPARE(Classroom::subjects().size(), 10);
        QCOMPARE(Classroom::subject(QStringLiteral("nonsense")).key, QStringLiteral("other"));
    }

    void whichLibrariesACourseReads()
    {
        const QList<LibraryRecord> installed = {library(1, QStringLiteral("wikipedia_en_ray-charles")), library(2, QStringLiteral("wikipedia_en_all")),
                                                library(3, QStringLiteral("wikipedia_en_mathematics")),
                                                library(4, QStringLiteral("wikipedia_en_history"), QStringLiteral("/no/such/file.zim"))};
        // the subject's own first, then the general ones; never an unrelated topic library; never a missing file
        auto ids = [](const QList<LibraryRecord> &l) {
            QList<qint64> out;
            for (const LibraryRecord &r : l)
                out << r.id;
            return out;
        };
        QCOMPARE(ids(Classroom::librariesFor(QStringLiteral("math"), installed)), (QList<qint64>{3, 2}));
        QCOMPARE(ids(Classroom::librariesFor(QStringLiteral("history"), installed)), (QList<qint64>{2}));
        QCOMPARE(ids(Classroom::librariesFor(QStringLiteral("other"), installed)), (QList<qint64>{2}));
        QVERIFY(Classroom::librariesFor(QStringLiteral("math"), {library(1, QStringLiteral("wikipedia_en_ray-charles"))}).isEmpty());
        QVERIFY(Classroom::isSimpleEnglish(library(5, QStringLiteral("wikipedia_en-simple_all"))));
        QVERIFY(Classroom::isGeneral(library(5, QStringLiteral("wikipedia_en-simple_all"))));
    }

    void theProfileAndCoursesAreKept()
    {
        QVERIFY(!Classroom::profile().exists());
        QVERIFY(Classroom::saveProfile({QStringLiteral("  Ada  Lovelace "), QStringLiteral("8"), QStringLiteral("engines"), QString()}));
        QCOMPARE(Classroom::profile().name, QStringLiteral("Ada Lovelace"));
        QVERIFY(Classroom::saveProfile({QStringLiteral("Ada"), QStringLiteral("not a grade"), {}, {}}));
        QCOMPARE(Classroom::profile().grade, QStringLiteral("none"));  // one row, and only known grades

        QCOMPARE(Classroom::createCourse(QStringLiteral("math"), QStringLiteral("  "), QStringLiteral("5"), {}, {}), 0);
        const qint64 id = Classroom::createCourse(QStringLiteral("math"), QStringLiteral("Fractions"), QStringLiteral("5"), QStringLiteral("halves"),
                                                  QStringLiteral("short"));
        QVERIFY(id > 0);
        QCOMPARE(Classroom::course(id)->status, QStringLiteral("new"));
        Classroom::Unit u1{0, 0, QStringLiteral("Basics"), {}};
        Classroom::Lesson a;
        a.title = QStringLiteral("What a fraction is");
        a.readings = {{{2, QStringLiteral("Fraction"), QStringLiteral("Fraction")}, 0.86}};
        Classroom::Lesson b;
        b.title = QStringLiteral("Comparing");
        u1.lessons = {a, b};
        Classroom::Unit u2{0, 1, QStringLiteral("More"), {a}};
        QVERIFY(Classroom::saveSyllabus(id, {u1, u2}));
        auto syllabus = Classroom::syllabus(id);
        QCOMPARE(syllabus.size(), 2);
        QCOMPARE(syllabus[0].lessons.size(), 2);
        QCOMPARE(syllabus[0].lessons[0].readings.first().article.title, QStringLiteral("Fraction"));
        QCOMPARE(syllabus[0].lessons[0].readings.first().score, 0.86);
        QCOMPARE(Classroom::course(id)->lessons, 3);

        const qint64 lesson = syllabus[0].lessons[0].id;
        QVERIFY(Classroom::setIntro(lesson, QStringLiteral("## Key words")));
        QVERIFY(Classroom::setCompleted(lesson, true));
        QVERIFY(Classroom::lesson(lesson)->completed());
        QCOMPARE(Classroom::course(id)->completed, 1);
        QVERIFY(Classroom::setCompleted(lesson, false));
        QCOMPARE(Classroom::course(id)->completed, 0);

        // a new plan replaces the old one; a plan left running is failed at the next start
        QVERIFY(Classroom::saveSyllabus(id, {u2}));
        QCOMPARE(Classroom::course(id)->lessons, 1);
        QVERIFY(!Classroom::lesson(lesson));
        QVERIFY(Classroom::setStatus(id, QStringLiteral("planning")));
        Classroom::reconcile();
        QCOMPARE(Classroom::course(id)->status, QStringLiteral("failed"));
        QVERIFY(!Classroom::course(id)->error.isEmpty());

        // deleting the course takes its units and lessons
        QVERIFY(Classroom::removeCourse(id));
        QSqlQuery q(QStringLiteral("SELECT (SELECT count(*) FROM course_units) + (SELECT count(*) FROM course_lessons)"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    void aLessonsReadings()
    {
        // the closest first; nothing below the floor; a second only if nearly as close; the same article once
        auto titles = [](const QList<Classroom::Reading> &r) {
            QStringList out;
            for (const Classroom::Reading &x : r)
                out << x.article.title;
            return out;
        };
        QCOMPARE(titles(Teaching::chooseReadings({choice(QStringLiteral("Geodynamics of terrestrial exoplanets"), 0.773),
                                                  choice(QStringLiteral("Plate tectonics"), 0.870), choice(QStringLiteral("Animal locomotion"), 0.659)},
                                                 {})),
                 QStringList{QStringLiteral("Plate tectonics")});
        QCOMPARE(titles(Teaching::chooseReadings({choice(QStringLiteral("Stratovolcano"), 0.811), choice(QStringLiteral("Caldera"), 0.803)}, {})),
                 (QStringList{QStringLiteral("Stratovolcano"), QStringLiteral("Caldera")}));
        QVERIFY(Teaching::chooseReadings({choice(QStringLiteral("Contemporary Turkish literature"), 0.695)}, {}).isEmpty());
        QCOMPARE(titles(Teaching::chooseReadings({choice(QStringLiteral("Fraction"), 0.86, 3), choice(QStringLiteral("Fraction"), 0.85, 2)}, {})),
                 QStringList{QStringLiteral("Fraction")});
        // an article an earlier lesson reads goes after a new one, and is never a second reading
        const QSet<QString> used{QStringLiteral("1/Earthquake")};
        QCOMPARE(titles(Teaching::chooseReadings({choice(QStringLiteral("Earthquake"), 0.80), choice(QStringLiteral("Seismic wave"), 0.78)}, used)),
                 QStringList{QStringLiteral("Seismic wave")});
        QCOMPARE(titles(Teaching::chooseReadings({choice(QStringLiteral("Earthquake"), 0.80)}, used)), QStringList{QStringLiteral("Earthquake")});
        // without similarities (no embedding model): the search's order
        QCOMPARE(titles(Teaching::chooseReadings({choice(QStringLiteral("A"), -1), choice(QStringLiteral("B"), -1), choice(QStringLiteral("C"), -1)}, {})),
                 (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    }

    void thePromptsSayWhatMatters()
    {
        Classroom::Course c;
        c.subject = QStringLiteral("math");
        c.title = QStringLiteral("Mathematics");
        c.level = QStringLiteral("5");
        const QString plan = Classroom::syllabusPrompt(c, {QStringLiteral("Jaimie"), QStringLiteral("4"), QStringLiteral("volcanoes, hiking"), {}}, 3, 3);
        QVERIFY(plan.contains(QLatin1String("exactly 3 units")));
        QVERIFY(plan.contains(QLatin1String("Every lesson must be a mathematics topic")));
        QVERIFY(!plan.contains(QLatin1String("volcanoes")));  // interests drew a maths course off its subject
        Classroom::Lesson l;
        l.title = QStringLiteral("Fractions");
        const QString intro = Classroom::introPrompt(c, {QStringLiteral("Jaimie Garner"), {}, QStringLiteral("volcanoes"), {}}, l, QStringLiteral("[1] Fraction"));
        QVERIFY(intro.contains(QLatin1String("speaking to Jaimie directly")));
        QVERIFY(intro.contains(QLatin1String("volcanoes")));  // examples may come from them
        QVERIFY(intro.contains(QLatin1String("Do not greet")));
    }
};

QTEST_GUILESS_MAIN(ClassroomTest)
#include "classroomtest.moc"
