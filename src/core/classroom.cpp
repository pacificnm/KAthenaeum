// SPDX-License-Identifier: GPL-3.0-or-later
#include "classroom.h"

#include "database.h"
#include "library.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimeZone>

namespace
{
QDateTime timestamp(const QVariant &v)
{
    if (v.isNull() || v.toString().isEmpty())
        return {};
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    t.setTimeZone(QTimeZone::UTC);
    return t;
}

void touch(qint64 courseId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE courses SET updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    q.addBindValue(courseId);
    q.exec();
}

QByteArray readingsJson(const QList<Classroom::Reading> &readings)
{
    QJsonArray a;
    for (const Classroom::Reading &r : readings)
        a.append(QJsonObject{{QStringLiteral("libraryId"), r.article.libraryId},
                             {QStringLiteral("path"), r.article.path},
                             {QStringLiteral("title"), r.article.title},
                             {QStringLiteral("score"), r.score}});
    return QJsonDocument(a).toJson(QJsonDocument::Compact);
}

QList<Classroom::Reading> readingsFrom(const QByteArray &json)
{
    QList<Classroom::Reading> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        Classroom::Reading r;
        r.article = {qint64(o.value(QLatin1String("libraryId")).toDouble()), o.value(QLatin1String("path")).toString(),
                     o.value(QLatin1String("title")).toString()};
        r.score = o.value(QLatin1String("score")).toDouble();
        out << r;
    }
    return out;
}

const QString LessonColumns = QStringLiteral("id, course_id, unit_id, position, title, objective, readings, intro, completed_at");

Classroom::Lesson lessonFrom(const QSqlQuery &q)
{
    Classroom::Lesson l;
    l.id = q.value(0).toLongLong();
    l.courseId = q.value(1).toLongLong();
    l.unitId = q.value(2).toLongLong();
    l.position = q.value(3).toInt();
    l.title = q.value(4).toString();
    l.objective = q.value(5).toString();
    l.readings = readingsFrom(q.value(6).toByteArray());
    l.intro = q.value(7).toString();
    l.completedAt = timestamp(q.value(8));
    return l;
}

const QString CourseColumns = QStringLiteral(
    "c.id, c.subject, c.title, c.level, c.focus, c.length, c.status, c.error, c.updated_at, "
    "(SELECT count(*) FROM course_lessons l WHERE l.course_id = c.id), "
    "(SELECT count(*) FROM course_lessons l WHERE l.course_id = c.id AND l.completed_at IS NOT NULL)");

Classroom::Course courseFrom(const QSqlQuery &q)
{
    Classroom::Course c;
    c.id = q.value(0).toLongLong();
    c.subject = q.value(1).toString();
    c.title = q.value(2).toString();
    c.level = q.value(3).toString();
    c.focus = q.value(4).toString();
    c.length = q.value(5).toString();
    c.status = q.value(6).toString();
    c.error = q.value(7).toString();
    c.updatedAt = timestamp(q.value(8));
    c.lessons = q.value(9).toInt();
    c.completed = q.value(10).toInt();
    return c;
}

// a unit or lesson line as the model writes it, without its marks: "1. **Unit 1: Fractions**" -> "Unit 1: Fractions"
QString unmarked(QString line)
{
    static const QRegularExpression marks(QStringLiteral("^(#+|[-*+\\x{2022}]|\\d+[.)]|[a-z][.)])\\s+"), QRegularExpression::CaseInsensitiveOption);
    line = line.trimmed();
    for (int i = 0; i < 3; ++i)
        line.remove(marks);
    line.remove(QStringLiteral("**"));
    line.remove(QStringLiteral("__"));
    return line.trimmed();
}

// A part of a line without what the template put there: "<the lesson's title>", "Lesson title:", "Wikipedia article:"
QString cleanTemplate(QString part)
{
    static const QRegularExpression labels(QStringLiteral("^(?:the\\s+)?(?:lesson(?:'s)?\\s*title|unit(?:'s)?\\s*title|(?:wikipedia\\s+)?article(?:'s)?(?:\\s*title)?|"
                                                          "wikipedia|title|objective|what the student will learn)\\s*[:=-]\\s*"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression placeholder(QStringLiteral("^<[^>]*>$"));
    part = part.trimmed();
    part.remove(QStringLiteral("**"));
    part.remove(labels);
    part = part.trimmed();
    if (placeholder.match(part).hasMatch())
        return {};  // the template itself
    part.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    // quotes or brackets round an article's title: "Plate tectonics", [Plate tectonics]
    while (part.size() > 1 && QStringLiteral("\"'[(\x{201C}").contains(part.front()) && QStringLiteral("\"')]\x{201D}").contains(part.back()))
        part = part.mid(1, part.size() - 2).trimmed();
    const QString lower = part.toLower();
    if (lower == QLatin1String("lesson title") || lower == QLatin1String("unit title") || lower == QLatin1String("article title"))
        return {};
    return part;
}

// "Title: what it teaches", "Title - what it teaches", "Title — what it teaches"
std::pair<QString, QString> titleAndBrief(const QString &line)
{
    static const QRegularExpression split(QStringLiteral("\\s*(:|\\s[\\x{2014}\\x{2013}-]\\s)\\s*"));
    const QRegularExpressionMatch m = split.match(line);
    if (m.hasMatch() && m.capturedStart() > 0)
        return {line.left(m.capturedStart()).trimmed(), line.mid(m.capturedEnd()).trimmed()};
    return {line.trimmed(), QString()};
}
}

// ---- the learner ----

Classroom::Profile Classroom::profile()
{
    QSqlQuery q(QStringLiteral("SELECT name, grade, interests, goals FROM learner WHERE id = 1"));
    if (q.next())
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString()};
    return {};
}

bool Classroom::saveProfile(const Profile &p)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO learner (id, name, grade, interests, goals) VALUES (1, ?, ?, ?, ?) "
                             "ON CONFLICT (id) DO UPDATE SET name = excluded.name, grade = excluded.grade, interests = excluded.interests, "
                             "goals = excluded.goals, updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now')"));
    q.addBindValue(Database::text(p.name.simplified().left(100)));
    q.addBindValue(Database::text(gradesCompleted().contains(p.grade) ? p.grade : QStringLiteral("none")));
    q.addBindValue(Database::text(p.interests.trimmed().left(1000)));
    q.addBindValue(Database::text(p.goals.trimmed().left(1000)));
    return q.exec();
}

// ---- levels ----

QStringList Classroom::levels()
{
    QStringList out{QStringLiteral("k")};
    for (int g = 1; g <= 12; ++g)
        out << QString::number(g);
    out << QStringLiteral("college") << QStringLiteral("adult");
    return out;
}

QStringList Classroom::gradesCompleted()
{
    QStringList out{QStringLiteral("none"), QStringLiteral("k")};
    for (int g = 1; g <= 12; ++g)
        out << QString::number(g);
    out << QStringLiteral("college") << QStringLiteral("degree");
    return out;
}

QString Classroom::levelLabel(const QString &key)
{
    if (key == QLatin1String("k"))
        return QStringLiteral("Kindergarten");
    if (key == QLatin1String("college"))
        return QStringLiteral("College");
    if (key == QLatin1String("adult"))
        return QStringLiteral("Adult learner");
    bool ok = false;
    const int g = key.toInt(&ok);
    return ok ? QStringLiteral("Grade %1").arg(g) : key;
}

QString Classroom::gradeLabel(const QString &key)
{
    if (key == QLatin1String("none") || key.isEmpty())
        return QStringLiteral("None yet");
    if (key == QLatin1String("college"))
        return QStringLiteral("Some college");
    if (key == QLatin1String("degree"))
        return QStringLiteral("College degree");
    return levelLabel(key);
}

QString Classroom::levelForPrompt(const QString &key)
{
    if (key == QLatin1String("k"))
        return QStringLiteral("kindergarten (about 5 years old)");
    if (key == QLatin1String("college"))
        return QStringLiteral("first-year college");
    if (key == QLatin1String("adult"))
        return QStringLiteral("an adult beginner with no special background");
    bool ok = false;
    const int g = key.toInt(&ok);
    return ok ? QStringLiteral("grade %1 (about %2 years old)").arg(g).arg(g + 5) : key;
}

QString Classroom::nextLevel(const QString &gradeCompleted)
{
    if (gradeCompleted == QLatin1String("none") || gradeCompleted.isEmpty())
        return QStringLiteral("k");
    if (gradeCompleted == QLatin1String("k"))
        return QStringLiteral("1");
    if (gradeCompleted == QLatin1String("12"))
        return QStringLiteral("college");
    if (gradeCompleted == QLatin1String("college") || gradeCompleted == QLatin1String("degree"))
        return QStringLiteral("adult");
    bool ok = false;
    const int g = gradeCompleted.toInt(&ok);
    return ok ? QString::number(std::min(g + 1, 12)) : QStringLiteral("adult");
}

bool Classroom::isYoung(const QString &level)
{
    bool ok = false;
    const int g = level.toInt(&ok);
    return level == QLatin1String("k") || (ok && g <= 5);
}

// ---- subjects ----

QList<Classroom::Subject> Classroom::subjects()
{
    const QString best = QStringLiteral("Best of Wikipedia");
    return {
        {QStringLiteral("math"), QStringLiteral("Mathematics"), QStringLiteral("applications-education-mathematics"),
         QStringLiteral("Numbers, algebra, geometry, statistics"), {QStringLiteral("wikipedia_en_mathematics")}, QStringLiteral("Mathematics by Wikipedia")},
        {QStringLiteral("science"), QStringLiteral("Science"), QStringLiteral("applications-education-science"),
         QStringLiteral("Physics, chemistry, biology, Earth and space"),
         {QStringLiteral("wikipedia_en_physics"), QStringLiteral("wikipedia_en_chemistry"), QStringLiteral("wikipedia_en_molcell"),
          QStringLiteral("wikipedia_en_astronomy"), QStringLiteral("wikipedia_en_climate-change")},
         best},
        {QStringLiteral("english"), QStringLiteral("English and Literature"), QStringLiteral("applications-education-language"),
         QStringLiteral("Reading, writing, grammar, great books"), {}, best},
        {QStringLiteral("history"), QStringLiteral("History"), QStringLiteral("view-calendar-timeline"),
         QStringLiteral("People, events and civilisations"), {QStringLiteral("wikipedia_en_history")}, QStringLiteral("History by Wikipedia")},
        {QStringLiteral("geography"), QStringLiteral("Geography"), QStringLiteral("globe"),
         QStringLiteral("Places, landforms, climates, peoples"), {QStringLiteral("wikipedia_en_geography")}, QStringLiteral("Geography by Wikipedia")},
        {QStringLiteral("civics"), QStringLiteral("Civics and Government"), QStringLiteral("system-users"),
         QStringLiteral("How governments work, rights, citizenship"), {}, best},
        {QStringLiteral("health"), QStringLiteral("Health"), QStringLiteral("love"),
         QStringLiteral("The body, nutrition, staying well"), {QStringLiteral("wikipedia_en_medicine")}, QStringLiteral("WikiMed Medical Encyclopedia")},
        {QStringLiteral("art"), QStringLiteral("Art"), QStringLiteral("draw-brush"),
         QStringLiteral("Artists, movements, techniques"), {}, best},
        {QStringLiteral("music"), QStringLiteral("Music"), QStringLiteral("audio-x-generic"),
         QStringLiteral("Composers, instruments, styles, theory"), {}, best},
        {QStringLiteral("computing"), QStringLiteral("Computing"), QStringLiteral("computer"),
         QStringLiteral("How computers work, programming, the Internet"), {QStringLiteral("wikipedia_en_computer")}, QStringLiteral("Computer by Wikipedia")},
    };
}

Classroom::Subject Classroom::subject(const QString &key)
{
    for (const Subject &s : subjects())
        if (s.key == key)
            return s;
    return {QStringLiteral("other"), QStringLiteral("Other"), QStringLiteral("applications-education"), QStringLiteral("Anything you want to learn"), {},
            QStringLiteral("Best of Wikipedia")};
}

bool Classroom::isGeneral(const LibraryRecord &library)
{
    static const QStringList general = {QStringLiteral("wikipedia_en_all"), QStringLiteral("wikipedia_en_top"), QStringLiteral("wikipedia_en_top1m"),
                                        QStringLiteral("wikipedia_en-simple_all"), QStringLiteral("wikipedia_en_100")};
    return general.contains(library.name);
}

bool Classroom::isSimpleEnglish(const LibraryRecord &library)
{
    return library.name.startsWith(QLatin1String("wikipedia_en-simple"));
}

QList<LibraryRecord> Classroom::librariesFor(const QString &subjectKey, const QList<LibraryRecord> &installed)
{
    const QStringList own = subject(subjectKey).libraries;
    QList<LibraryRecord> out;
    for (const LibraryRecord &l : installed)  // its own libraries first
        if (l.fileExists() && own.contains(l.name))
            out << l;
    for (const LibraryRecord &l : installed)
        if (l.fileExists() && isGeneral(l))
            out << l;
    return out;
}

// ---- courses ----

std::pair<int, int> Classroom::shape(const QString &length)
{
    if (length == QLatin1String("short"))
        return {3, 3};
    if (length == QLatin1String("long"))
        return {6, 4};
    return {4, 4};
}

qint64 Classroom::createCourse(const QString &subject, const QString &title, const QString &level, const QString &focus, const QString &length)
{
    const QString name = title.simplified().left(150);
    if (name.isEmpty())
        return 0;
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO courses (subject, title, level, focus, length) VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(Database::text(Classroom::subject(subject).key));
    q.addBindValue(name);
    q.addBindValue(levels().contains(level) ? level : QStringLiteral("adult"));
    q.addBindValue(Database::text(focus.trimmed().left(2000)));
    q.addBindValue(QStringList{QStringLiteral("short"), QStringLiteral("standard"), QStringLiteral("long")}.contains(length) ? length
                                                                                                                          : QStringLiteral("standard"));
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

bool Classroom::removeCourse(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM courses WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

bool Classroom::setStatus(qint64 id, const QString &status, const QString &error)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE courses SET status = ?, error = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    q.addBindValue(status);
    q.addBindValue(Database::text(error));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

void Classroom::reconcile()
{
    QSqlQuery q;
    q.exec(QStringLiteral("UPDATE courses SET status = 'failed', error = 'Planning was interrupted. Press Plan Again.' WHERE status = 'planning'"));
}

QList<Classroom::Course> Classroom::courses()
{
    QList<Course> out;
    QSqlQuery q(QStringLiteral("SELECT %1 FROM courses c ORDER BY c.updated_at DESC, c.id DESC").arg(CourseColumns));
    while (q.next())
        out << courseFrom(q);
    return out;
}

std::optional<Classroom::Course> Classroom::course(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM courses c WHERE c.id = ?").arg(CourseColumns));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return courseFrom(q);
    return std::nullopt;
}

bool Classroom::saveSyllabus(qint64 courseId, const QList<Unit> &units)
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction())
        return false;
    bool ok = true;
    {
        QSqlQuery clear;
        clear.prepare(QStringLiteral("DELETE FROM course_units WHERE course_id = ?"));  // their lessons go with them
        clear.addBindValue(courseId);
        ok = clear.exec();
    }
    for (int u = 0; ok && u < units.size(); ++u) {
        QSqlQuery unit;
        unit.prepare(QStringLiteral("INSERT INTO course_units (course_id, position, title) VALUES (?, ?, ?)"));
        unit.addBindValue(courseId);
        unit.addBindValue(u);
        unit.addBindValue(Database::text(units.at(u).title));
        ok = unit.exec();
        const qint64 unitId = unit.lastInsertId().toLongLong();
        for (int l = 0; ok && l < units.at(u).lessons.size(); ++l) {
            const Lesson &lesson = units.at(u).lessons.at(l);
            QSqlQuery q;
            q.prepare(QStringLiteral("INSERT INTO course_lessons (course_id, unit_id, position, title, objective, readings) VALUES (?, ?, ?, ?, ?, ?)"));
            q.addBindValue(courseId);
            q.addBindValue(unitId);
            q.addBindValue(l);
            q.addBindValue(Database::text(lesson.title));
            q.addBindValue(Database::text(lesson.objective));
            q.addBindValue(QString::fromUtf8(readingsJson(lesson.readings)));
            ok = q.exec();
        }
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    if (!db.commit())
        return false;
    touch(courseId);
    return true;
}

QList<Classroom::Unit> Classroom::syllabus(qint64 courseId)
{
    QList<Unit> units;
    QSqlQuery u;
    u.prepare(QStringLiteral("SELECT id, position, title FROM course_units WHERE course_id = ? ORDER BY position, id"));
    u.addBindValue(courseId);
    if (u.exec())
        while (u.next())
            units << Unit{u.value(0).toLongLong(), u.value(1).toInt(), u.value(2).toString(), {}};
    QSqlQuery l;
    l.prepare(QStringLiteral("SELECT %1 FROM course_lessons WHERE course_id = ? ORDER BY position, id").arg(LessonColumns));
    l.addBindValue(courseId);
    if (l.exec())
        while (l.next()) {
            const Lesson lesson = lessonFrom(l);
            for (Unit &unit : units)
                if (unit.id == lesson.unitId)
                    unit.lessons << lesson;
        }
    return units;
}

std::optional<Classroom::Lesson> Classroom::lesson(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM course_lessons WHERE id = ?").arg(LessonColumns));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return lessonFrom(q);
    return std::nullopt;
}

bool Classroom::setIntro(qint64 lessonId, const QString &markdown)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE course_lessons SET intro = ? WHERE id = ? RETURNING course_id"));
    q.addBindValue(Database::text(markdown));
    q.addBindValue(lessonId);
    if (!q.exec() || !q.next())
        return false;
    const qint64 courseId = q.value(0).toLongLong();
    q.finish();
    touch(courseId);
    return true;
}

bool Classroom::setCompleted(qint64 lessonId, bool completed)
{
    QSqlQuery q;
    q.prepare(completed ? QStringLiteral("UPDATE course_lessons SET completed_at = coalesce(completed_at, strftime('%Y-%m-%dT%H:%M:%f', 'now')) "
                                         "WHERE id = ? RETURNING course_id")
                        : QStringLiteral("UPDATE course_lessons SET completed_at = NULL WHERE id = ? RETURNING course_id"));
    q.addBindValue(lessonId);
    if (!q.exec() || !q.next())
        return false;
    const qint64 courseId = q.value(0).toLongLong();
    q.finish();
    touch(courseId);
    return true;
}

// ---- prompts ----

namespace
{
QString learnerLines(const Classroom::Profile &p)
{
    QString out;
    if (!p.name.trimmed().isEmpty())
        out += QStringLiteral("The student's name: ") + p.name.trimmed() + QLatin1Char('\n');
    if (!p.grade.isEmpty() && p.grade != QLatin1String("none"))
        out += QStringLiteral("Highest grade completed: ") + Classroom::gradeLabel(p.grade) + QLatin1Char('\n');
    if (!p.interests.trimmed().isEmpty())
        out += QStringLiteral("Their interests: ") + p.interests.trimmed().left(400) + QLatin1Char('\n');
    if (!p.goals.trimmed().isEmpty())
        out += QStringLiteral("Their goals: ") + p.goals.trimmed().left(400) + QLatin1Char('\n');
    return out;
}
}

QString Classroom::syllabusPrompt(const Course &course, const Profile &profile, int units, int lessonsPerUnit)
{
    // The learner's interests are left out: given them, a grade 5 Mathematics course was planned about volcanoes and
    // hiking (2026-10-20). They shape a lesson's examples (introPrompt), not what the course teaches.
    Q_UNUSED(profile)
    const Subject s = subject(course.subject);
    const bool core = s.key != QLatin1String("other");
    QString prompt = QStringLiteral("You are a teacher planning a course for one student.\n\n");
    prompt += QStringLiteral("Course: ") + course.title + QLatin1Char('\n');
    if (core)
        prompt += QStringLiteral("Subject: ") + s.name + QLatin1Char('\n');
    prompt += QStringLiteral("Level: ") + levelForPrompt(course.level) + QLatin1Char('\n');
    if (!course.focus.trimmed().isEmpty())
        prompt += QStringLiteral("What the student wants this course to cover: ") + course.focus.trimmed() + QLatin1Char('\n');
    else if (core)
        prompt += QStringLiteral("Cover the main %1 topics usually taught at this level.\n").arg(s.name.toLower());
    if (core)
        prompt += QStringLiteral("Every lesson must be a %1 topic.\n").arg(s.name.toLower());
    prompt += QStringLiteral("\nWrite the syllabus: exactly %1 units, and exactly %2 lessons in each unit, in the order they should be learned, the basics first. "
                             "Each lesson is one topic for this level (not one learned in earlier grades) that Wikipedia has an article about: "
                             "give that article's exact "
                             "title. Write each lesson on one line, with its parts separated by \"|\", and nothing else before or after:\n\n"
                             "Unit 1: <the unit's title>\n"
                             "- <the lesson's title> | <the Wikipedia article's title> | <one sentence on what the student will learn>\n"
                             "(%2 lesson lines in each unit)\n\n"
                             "Unit 2: <the unit's title>\n…\n\n(%1 units in all)")
                  .arg(units)
                  .arg(lessonsPerUnit);
    return prompt;
}

QList<Classroom::Unit> Classroom::parseSyllabus(const QString &text, const QString &courseTitle, int maxUnits, int maxLessons)
{
    // "Unit 1: Fractions", "Unit One - Fractions", "Module 2. Fractions"; not "Units of measurement: …" or "Part of speech: …"
    static const QRegularExpression unitLine(QStringLiteral("^(?:unit|module|part|week|chapter)\\b\\s*(?:\\d+|[a-z]+)?\\s*[:.\\x{2014}\\x{2013}-]\\s*(.*)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression lessonWord(QStringLiteral("^lesson\\s*\\d*\\s*[:.\\x{2014}\\x{2013}-]\\s*"), QRegularExpression::CaseInsensitiveOption);
    QList<Unit> units;
    QSet<QString> seen;  // a lesson the model repeats
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = unmarked(raw);
        if (line.isEmpty())
            continue;
        const QRegularExpressionMatch unit = unitLine.match(line);
        // a Markdown heading is a unit too ("## Fractions"), whatever it says
        if (unit.hasMatch() || raw.trimmed().startsWith(QLatin1Char('#'))) {
            QString title = titleAndBrief(unit.hasMatch() ? unit.captured(1) : line).first;
            if (title.isEmpty())
                title = line;
            if (units.size() >= maxUnits)
                break;
            units << Unit{0, int(units.size()), title.left(150), {}};
            continue;
        }
        if (line.endsWith(QLatin1Char(':')) && line.size() < 40)  // "Syllabus:"
            continue;
        QString lessonText = line;
        lessonText.remove(lessonWord);
        QString title, objective, article;
        if (lessonText.contains(QLatin1Char('|'))) {
            const QStringList parts = lessonText.split(QLatin1Char('|'));
            title = cleanTemplate(parts.value(0));
            article = cleanTemplate(parts.value(1));
            objective = cleanTemplate(parts.mid(2).join(QStringLiteral(" ")));
        } else {
            std::tie(title, objective) = titleAndBrief(lessonText);
            title = cleanTemplate(title);
            objective = cleanTemplate(objective);
        }
        if (title.isEmpty())
            title = article;
        if (title.isEmpty() || title.size() > 120)
            continue;
        if (seen.contains(title.toLower()))
            continue;
        if (units.isEmpty())
            units << Unit{0, 0, courseTitle, {}};
        if (units.last().lessons.size() >= maxLessons)
            continue;
        seen.insert(title.toLower());
        Lesson l;
        l.title = title;
        l.objective = objective.left(400);
        l.article = article.left(150);
        l.position = int(units.last().lessons.size());
        units.last().lessons << l;
    }
    units.removeIf([](const Unit &u) {
        return u.lessons.isEmpty();
    });
    for (int i = 0; i < units.size(); ++i)
        units[i].position = i;
    return units;
}

QString Classroom::lessonQuery(const Course &course, const Lesson &lesson)
{
    Q_UNUSED(course)
    return !lesson.article.isEmpty() ? lesson.article : QString(lesson.title + QLatin1Char(' ') + lesson.objective).simplified();
}

QString Classroom::lessonMeaning(const Course &course, const Lesson &lesson)
{
    QString out = lesson.title;
    if (!lesson.objective.isEmpty())
        out += QStringLiteral(": ") + lesson.objective;
    return out + QStringLiteral(" (") + course.title + QLatin1Char(')');
}

QString Classroom::introPrompt(const Course &course, const Profile &profile, const Lesson &lesson, const QString &sources)
{
    const QString name = profile.name.trimmed().isEmpty() ? QStringLiteral("the student") : profile.name.trimmed().section(QLatin1Char(' '), 0, 0);
    QString prompt = QStringLiteral("You are a friendly teacher writing the introduction to one lesson of the course \"") + course.title
        + QStringLiteral("\", for a student at the level of ") + levelForPrompt(course.level) + QStringLiteral(".\n");
    prompt += learnerLines(profile);
    prompt += QStringLiteral("\nThe lesson: \"") + lesson.title + QLatin1Char('"');
    if (!lesson.objective.isEmpty())
        prompt += QStringLiteral(" (") + lesson.objective + QLatin1Char(')');
    prompt += QStringLiteral("\n\nThe lesson's reading, from the encyclopedia:\n\n") + sources;
    prompt += QStringLiteral(
                  "\n\nWrite the introduction in Markdown, speaking to %1 directly, in words and examples that suit the level%3. Do not "
                  "greet the student.\n"
                  "1. Two short paragraphs explaining the main idea simply, with an example from everyday life%2.\n"
                  "2. A section \"## Key words\": three to six words from the reading, each with a one-line meaning.\n"
                  "3. A section \"## As you read\": three questions to think about while reading.\n"
                  "Use only facts from the reading above, and copy figures exactly. Do not write a title, and do not write source numbers.")
                  .arg(name, profile.interests.trimmed().isEmpty() ? QString() : QStringLiteral(" or from the student's interests where it fits naturally"),
                       isYoung(course.level) ? QString() : QStringLiteral(" (a serious learner: no childish comparisons)"));
    return prompt;
}
