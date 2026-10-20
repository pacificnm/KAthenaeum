// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "classroom.h"

#include "database.h"
#include "library.h"
#include "zimsearch.h"

#include <KLocalizedString>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QRandomGenerator>
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
    for (const auto array = QJsonDocument::fromJson(json).array(); const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        Classroom::Reading r;
        r.article = {qint64(o.value(QLatin1String("libraryId")).toDouble()), o.value(QLatin1String("path")).toString(),
                     o.value(QLatin1String("title")).toString()};
        r.score = o.value(QLatin1String("score")).toDouble();
        out << r;
    }
    return out;
}

constexpr QLatin1StringView LessonColumns("id, course_id, unit_id, position, title, objective, readings, intro, completed_at");

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

constexpr QLatin1StringView CourseColumns(
    "c.id, c.subject, c.title, c.level, c.focus, c.length, c.status, c.error, c.updated_at, coalesce(c.project_id, 0), "
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
    c.projectId = q.value(9).toLongLong();
    c.lessons = q.value(10).toInt();
    c.completed = q.value(11).toInt();
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
    while (part.size() > 1 && QStringLiteral("\"'[(\u201C").contains(part.front()) && QStringLiteral("\"')]\u201D").contains(part.back()))
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
        return i18nc("@item school level", "Kindergarten");
    if (key == QLatin1String("college"))
        return i18nc("@item school level", "College");
    if (key == QLatin1String("adult"))
        return i18nc("@item school level", "Adult learner");
    bool ok = false;
    const int g = key.toInt(&ok);
    return ok ? i18nc("@item school level, %1 is a grade from 1 to 12", "Grade %1", g) : key;
}

QString Classroom::gradeLabel(const QString &key)
{
    if (key == QLatin1String("none") || key.isEmpty())
        return i18nc("@item highest grade completed", "None yet");
    if (key == QLatin1String("college"))
        return i18nc("@item highest grade completed", "Some college");
    if (key == QLatin1String("degree"))
        return i18nc("@item highest grade completed", "College degree");
    return levelLabel(key);
}

// model prompt: stays English (the model is asked in English and its reply is parsed)
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
    // `suggestion` and `libraries` are Kiwix catalog titles and names: they match the catalog, so they stay English
    const QString best = QStringLiteral("Best of Wikipedia");
    return {
        {QStringLiteral("math"), i18nc("@item school subject", "Mathematics"), QStringLiteral("applications-education-mathematics"),
         i18nc("@info what a school subject covers", "Numbers, algebra, geometry, statistics"), {QStringLiteral("wikipedia_en_mathematics")}, QStringLiteral("Mathematics by Wikipedia")},
        {QStringLiteral("science"), i18nc("@item school subject", "Science"), QStringLiteral("applications-education-science"),
         i18nc("@info what a school subject covers", "Physics, chemistry, biology, Earth and space"),
         {QStringLiteral("wikipedia_en_physics"), QStringLiteral("wikipedia_en_chemistry"), QStringLiteral("wikipedia_en_molcell"),
          QStringLiteral("wikipedia_en_astronomy"), QStringLiteral("wikipedia_en_climate-change")},
         best},
        {QStringLiteral("english"), i18nc("@item school subject", "English and Literature"), QStringLiteral("applications-education-language"),
         i18nc("@info what a school subject covers", "Reading, writing, grammar, great books"), {}, best},
        {QStringLiteral("history"), i18nc("@item school subject", "History"), QStringLiteral("view-calendar-timeline"),
         i18nc("@info what a school subject covers", "People, events and civilisations"), {QStringLiteral("wikipedia_en_history")}, QStringLiteral("History by Wikipedia")},
        {QStringLiteral("geography"), i18nc("@item school subject", "Geography"), QStringLiteral("globe"),
         i18nc("@info what a school subject covers", "Places, landforms, climates, peoples"), {QStringLiteral("wikipedia_en_geography")}, QStringLiteral("Geography by Wikipedia")},
        {QStringLiteral("civics"), i18nc("@item school subject", "Civics and Government"), QStringLiteral("system-users"),
         i18nc("@info what a school subject covers", "How governments work, rights, citizenship"), {}, best},
        {QStringLiteral("health"), i18nc("@item school subject", "Health"), QStringLiteral("love"),
         i18nc("@info what a school subject covers", "The body, nutrition, staying well"), {QStringLiteral("wikipedia_en_medicine")}, QStringLiteral("WikiMed Medical Encyclopedia")},
        {QStringLiteral("art"), i18nc("@item school subject", "Art"), QStringLiteral("draw-brush"),
         i18nc("@info what a school subject covers", "Artists, movements, techniques"), {}, best},
        {QStringLiteral("music"), i18nc("@item school subject", "Music"), QStringLiteral("audio-x-generic"),
         i18nc("@info what a school subject covers", "Composers, instruments, styles, theory"), {}, best},
        {QStringLiteral("computing"), i18nc("@item school subject", "Computing"), QStringLiteral("computer"),
         i18nc("@info what a school subject covers", "How computers work, programming, the Internet"), {QStringLiteral("wikipedia_en_computer")}, QStringLiteral("Computer by Wikipedia")},
    };
}

Classroom::Subject Classroom::subject(const QString &key)
{
    for (const auto all = subjects(); const Subject &s : all)
        if (s.key == key)
            return s;
    return {QStringLiteral("other"), i18nc("@item school subject", "Other"), QStringLiteral("applications-education"),
            i18nc("@info what a school subject covers", "Anything you want to learn"), {}, QStringLiteral("Best of Wikipedia")};
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
    // a failed commit is rolled back too: left open, the transaction would take in every later write
    if (!ok || !db.commit()) {
        db.rollback();
        return false;
    }
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

qint64 Classroom::unitCourse(qint64 unitId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT course_id FROM course_units WHERE id = ?"));
    q.addBindValue(unitId);
    return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
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

// ---- a course kept in a research project ----

bool Classroom::setProject(qint64 courseId, qint64 projectId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE courses SET project_id = ? WHERE id = ?"));
    q.addBindValue(projectId > 0 ? QVariant(projectId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(courseId);
    return q.exec() && q.numRowsAffected() == 1;
}

std::optional<Classroom::Course> Classroom::courseForProject(qint64 projectId)
{
    if (projectId <= 0)
        return std::nullopt;
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM courses c WHERE c.project_id = ? ORDER BY c.updated_at DESC LIMIT 1").arg(CourseColumns));
    q.addBindValue(projectId);
    if (q.exec() && q.next())
        return courseFrom(q);
    return std::nullopt;
}

QString Classroom::lessonLabel(qint64 lessonId)
{
    const auto l = lesson(lessonId);
    if (!l)
        return {};
    int n = 0;
    for (const auto units = syllabus(l->courseId); const Unit &u : units)
        for (const Lesson &other : u.lessons) {
            ++n;
            if (other.id == lessonId)
                return i18nc("@label %1 is the lesson's number in its course, %2 its title", "Lesson %1: %2", n, l->title);
        }
    return l->title;
}

QString Classroom::lessonDocument(const Lesson &lesson)
{
    QStringList out{lesson.intro.trimmed()};
    if (!lesson.readings.isEmpty()) {
        out << QString() << QStringLiteral("## ") + i18nc("@title heading in a lesson's document", "Readings") << QString();
        for (int i = 0; i < lesson.readings.size(); ++i)
            out << QStringLiteral("%1. ").arg(i + 1) + Projects::articleLink(lesson.readings.at(i).article);
    }
    out << QString() << QStringLiteral("## ") + i18nc("@title heading in a lesson's document", "My answers") << QString();
    return out.join(QLatin1Char('\n')).trimmed() + QStringLiteral("\n\n");
}

bool Classroom::syncLesson(qint64 lessonId)
{
    const auto l = lesson(lessonId);
    const auto c = l ? course(l->courseId) : std::nullopt;
    if (!c || c->projectId <= 0 || !Projects::byId(c->projectId))
        return false;
    const QString label = lessonLabel(lessonId);
    for (const Reading &r : l->readings)
        if (const qint64 item = Projects::addArticle(c->projectId, r.article))
            Projects::setLesson(item, label);
    // the introduction, once: a document the learner deleted is written again only when it is gone
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT coalesce(document_id, 0) FROM course_lessons WHERE id = ?"));
    q.addBindValue(lessonId);
    const qint64 documentId = q.exec() && q.next() ? q.value(0).toLongLong() : 0;
    q.finish();
    if (!l->intro.isEmpty() && (documentId <= 0 || !Projects::document(documentId))) {
        const qint64 created = Projects::createDocument(c->projectId, label, lessonDocument(*l));
        QSqlQuery set;
        set.prepare(QStringLiteral("UPDATE course_lessons SET document_id = ? WHERE id = ?"));
        set.addBindValue(created > 0 ? QVariant(created) : QVariant(QMetaType::fromType<qint64>()));
        set.addBindValue(lessonId);
        set.exec();
    }
    return true;
}

void Classroom::syncCourse(qint64 courseId)
{
    // the last lesson first: a project lists its newest sources first, so the course reads from lesson 1 down
    QList<qint64> lessons;
    for (const auto units = syllabus(courseId); const Unit &u : units)
        for (const Lesson &l : u.lessons)
            lessons.prepend(l.id);
    for (qint64 id : std::as_const(lessons))
        if (!syncLesson(id))
            return;
    // an article two lessons read is labelled with the first of them (added last to first, it got the later one)
    const auto c = course(courseId);
    if (!c)
        return;
    QSet<QString> seen;
    for (auto it = lessons.crbegin(); it != lessons.crend(); ++it) {
        const auto l = lesson(*it);
        if (!l)
            continue;
        const QString label = lessonLabel(*it);
        for (const Reading &r : l->readings) {
            const QString key = QString::number(r.article.libraryId) + QLatin1Char('/') + r.article.path;
            if (seen.contains(key))
                continue;
            seen.insert(key);
            QSqlQuery q;
            q.prepare(QStringLiteral("UPDATE project_items SET lesson = ? WHERE project_id = ? AND kind = 'article' AND library_id = ? AND path = ?"));
            q.addBindValue(Database::text(label));
            q.addBindValue(c->projectId);
            q.addBindValue(r.article.libraryId);
            q.addBindValue(r.article.path);
            q.exec();
        }
    }
}

qint64 Classroom::keepHighlight(qint64 lessonId, const QString &text, const ArticleRef &article, const QString &note)
{
    const auto l = lesson(lessonId);
    const auto c = l ? course(l->courseId) : std::nullopt;
    if (!c || c->projectId <= 0 || text.trimmed().isEmpty())
        return 0;
    // the same passage highlighted again (another colour, a note added) is one excerpt
    const QString squeezed = text.simplified().toLower();
    for (const auto qStringLiteral = Projects::items(c->projectId, QStringLiteral("clip")); const ProjectItem &i : qStringLiteral)
        if (i.text.simplified().toLower() == squeezed) {
            if (!note.trimmed().isEmpty() && i.note.isEmpty())
                Projects::setNote(c->projectId, i.id, note);
            return i.id;
        }
    const qint64 id = Projects::addClip(c->projectId, text, {article}, {}, note);
    if (id > 0)
        Projects::setLesson(id, lessonLabel(lessonId));
    return id;
}

// ---- prompts ----
// model prompts in this section: stay English (the model is asked in English and its reply is parsed), so they use the
// English names below rather than the translated labels the interface shows

namespace
{
QString englishGrade(const QString &key)
{
    if (key == QLatin1String("k"))
        return QStringLiteral("Kindergarten");
    if (key == QLatin1String("college"))
        return QStringLiteral("Some college");
    if (key == QLatin1String("degree"))
        return QStringLiteral("College degree");
    if (key == QLatin1String("adult"))
        return QStringLiteral("Adult learner");
    bool ok = false;
    const int g = key.toInt(&ok);
    return ok ? QStringLiteral("Grade %1").arg(g) : key;
}

QString englishSubject(const QString &key)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("math"), QStringLiteral("Mathematics")},
        {QStringLiteral("science"), QStringLiteral("Science")},
        {QStringLiteral("english"), QStringLiteral("English and Literature")},
        {QStringLiteral("history"), QStringLiteral("History")},
        {QStringLiteral("geography"), QStringLiteral("Geography")},
        {QStringLiteral("civics"), QStringLiteral("Civics and Government")},
        {QStringLiteral("health"), QStringLiteral("Health")},
        {QStringLiteral("art"), QStringLiteral("Art")},
        {QStringLiteral("music"), QStringLiteral("Music")},
        {QStringLiteral("computing"), QStringLiteral("Computing")},
    };
    return names.value(key, QStringLiteral("Other"));
}

QString learnerLines(const Classroom::Profile &p)
{
    QString out;
    if (!p.name.trimmed().isEmpty())
        out += QStringLiteral("The student's name: ") + p.name.trimmed() + QLatin1Char('\n');
    if (!p.grade.isEmpty() && p.grade != QLatin1String("none"))
        out += QStringLiteral("Highest grade completed: ") + englishGrade(p.grade) + QLatin1Char('\n');
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
        prompt += QStringLiteral("Subject: ") + englishSubject(s.key) + QLatin1Char('\n');
    prompt += QStringLiteral("Level: ") + levelForPrompt(course.level) + QLatin1Char('\n');
    if (!course.focus.trimmed().isEmpty())
        prompt += QStringLiteral("What the student wants this course to cover: ") + course.focus.trimmed() + QLatin1Char('\n');
    else if (core)
        prompt += QStringLiteral("Cover the main %1 topics usually taught at this level.\n").arg(englishSubject(s.key).toLower());
    if (core)
        prompt += QStringLiteral("Every lesson must be a %1 topic.\n").arg(englishSubject(s.key).toLower());
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
    for (const auto qLatin1Char = text.split(QLatin1Char('\n')); const QString &raw : qLatin1Char) {
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

// model prompt: stays English (the model is asked in English and its reply is parsed)
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

// ---- quizzes and tests ----

namespace
{
QByteArray intsJson(const QList<int> &values)
{
    QJsonArray a;
    for (int v : values)
        a.append(v);
    return QJsonDocument(a).toJson(QJsonDocument::Compact);
}

QList<int> intsFrom(const QByteArray &json)
{
    QList<int> out;
    for (const auto array = QJsonDocument::fromJson(json).array(); const QJsonValue &v : array)
        out << v.toInt(-1);
    return out;
}

std::optional<Classroom::Attempt> attemptWhere(qint64 quizId, const char *order)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT id, answers, score, total, created_at FROM quiz_attempts WHERE quiz_id = ? ORDER BY %1 LIMIT 1")
                  .arg(QLatin1String(order)));
    q.addBindValue(quizId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return Classroom::Attempt{q.value(0).toLongLong(), intsFrom(q.value(1).toByteArray()), q.value(2).toInt(), q.value(3).toInt(), timestamp(q.value(4))};
}
}

qint64 Classroom::saveQuiz(const Quiz &quiz)
{
    if (quiz.questions.isEmpty())
        return 0;
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction())
        return 0;
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO quizzes (course_id, lesson_id, unit_id, kind) VALUES (?, ?, ?, ?)"));
    q.addBindValue(quiz.courseId);
    q.addBindValue(quiz.lessonId > 0 ? QVariant(quiz.lessonId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(quiz.unitId > 0 ? QVariant(quiz.unitId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(quiz.kind);
    bool ok = q.exec();
    const qint64 id = q.lastInsertId().toLongLong();
    for (int i = 0; ok && i < quiz.questions.size(); ++i) {
        const Question &question = quiz.questions.at(i);
        QSqlQuery insert;
        insert.prepare(QStringLiteral("INSERT INTO quiz_questions (quiz_id, position, question, options, answer, explanation, evidence, library_id, path, "
                                      "title, lesson_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(id);
        insert.addBindValue(i);
        insert.addBindValue(question.text);
        insert.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(question.options)).toJson(QJsonDocument::Compact)));
        insert.addBindValue(question.answer);
        insert.addBindValue(Database::text(question.explanation));
        insert.addBindValue(Database::text(question.evidence));
        insert.addBindValue(question.source.libraryId > 0 ? QVariant(question.source.libraryId) : QVariant(QMetaType::fromType<qint64>()));
        insert.addBindValue(Database::text(question.source.path));
        insert.addBindValue(Database::text(question.source.title));
        insert.addBindValue(question.lessonId > 0 ? QVariant(question.lessonId) : QVariant(QMetaType::fromType<qint64>()));
        ok = insert.exec();
    }
    if (!ok || !db.commit()) {
        db.rollback();
        return 0;
    }
    touch(quiz.courseId);
    return id;
}

std::optional<Classroom::Quiz> Classroom::quiz(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT id, course_id, coalesce(lesson_id, 0), coalesce(unit_id, 0), kind FROM quizzes WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    Quiz out{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toLongLong(), q.value(3).toLongLong(), q.value(4).toString(), {}};
    QSqlQuery questions;
    questions.prepare(QStringLiteral("SELECT id, question, options, answer, explanation, evidence, coalesce(library_id, 0), path, title, "
                                     "coalesce(lesson_id, 0) FROM quiz_questions WHERE quiz_id = ? ORDER BY position"));
    questions.addBindValue(id);
    if (questions.exec())
        while (questions.next()) {
            Question question;
            question.id = questions.value(0).toLongLong();
            question.text = questions.value(1).toString();
            for (const auto array = QJsonDocument::fromJson(questions.value(2).toByteArray()).array(); const QJsonValue &v : array)
                question.options << v.toString();
            question.answer = questions.value(3).toInt();
            question.explanation = questions.value(4).toString();
            question.evidence = questions.value(5).toString();
            question.source = {questions.value(6).toLongLong(), questions.value(7).toString(), questions.value(8).toString()};
            question.lessonId = questions.value(9).toLongLong();
            out.questions << question;
        }
    return out;
}

std::optional<Classroom::Quiz> Classroom::latestQuiz(const QString &kind, qint64 targetId)
{
    QSqlQuery q;
    q.prepare(kind == QLatin1String("unit") ? QStringLiteral("SELECT id FROM quizzes WHERE kind = 'unit' AND unit_id = ? ORDER BY id DESC LIMIT 1")
                                            : QStringLiteral("SELECT id FROM quizzes WHERE kind = 'lesson' AND lesson_id = ? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(targetId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return quiz(q.value(0).toLongLong());
}

std::optional<Classroom::Attempt> Classroom::submit(qint64 quizId, const QList<int> &answers)
{
    const auto qz = quiz(quizId);
    if (!qz)
        return std::nullopt;
    Attempt a;
    a.total = int(qz->questions.size());
    for (int i = 0; i < qz->questions.size(); ++i) {
        const int chosen = answers.value(i, -1);
        a.answers << chosen;
        if (chosen == qz->questions.at(i).answer)
            ++a.score;
    }
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO quiz_attempts (quiz_id, answers, score, total) VALUES (?, ?, ?, ?)"));
    q.addBindValue(quizId);
    q.addBindValue(QString::fromUtf8(intsJson(a.answers)));
    q.addBindValue(a.score);
    q.addBindValue(a.total);
    if (!q.exec())
        return std::nullopt;
    a.id = q.lastInsertId().toLongLong();
    a.at = QDateTime::currentDateTimeUtc();
    touch(qz->courseId);
    return a;
}

std::optional<Classroom::Attempt> Classroom::latestAttempt(qint64 quizId)
{
    return attemptWhere(quizId, "id DESC");
}

std::optional<Classroom::Attempt> Classroom::bestAttempt(qint64 quizId)
{
    return attemptWhere(quizId, "score * 1.0 / max(total, 1) DESC, id DESC");
}

QList<Classroom::Missed> Classroom::missed(qint64 courseId)
{
    QList<Missed> out;
    // the newest quiz of each lesson and test of each unit: older ones were replaced by new questions
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT max(id) FROM quizzes WHERE course_id = ? GROUP BY kind, coalesce(lesson_id, 0), coalesce(unit_id, 0) "
                             "ORDER BY max(id) DESC"));
    q.addBindValue(courseId);
    QList<qint64> ids;
    if (q.exec())
        while (q.next())
            ids << q.value(0).toLongLong();
    for (qint64 id : std::as_const(ids)) {
        const auto attempt = latestAttempt(id);
        const auto qz = attempt ? quiz(id) : std::nullopt;
        if (!qz)
            continue;
        for (int i = 0; i < qz->questions.size(); ++i) {
            const int chosen = attempt->answers.value(i, -1);
            if (chosen == qz->questions.at(i).answer)
                continue;
            const auto l = lesson(qz->questions.at(i).lessonId);
            out << Missed{qz->questions.at(i), chosen, qz->kind, l ? l->title : QString()};
        }
    }
    return out;
}

// model prompt: stays English (the model is asked in English and its reply is parsed)
QString Classroom::questionPrompt(const Course &course, const QString &lessonTitle, const QString &passage)
{
    return QStringLiteral("You are a teacher writing a quiz for the lesson \"%1\" of the course \"%2\", for a student at the level of %3.\n\n"
                          "The passage:\n\n%4\n\n"
                          "Write one multiple-choice question that checks the student understood an important idea or fact in this passage, "
                          "one a student at this level should know: not a technical detail beyond the level. The options must be real answers "
                          "to the question, not the question's own words. "
                          "The right answer must be stated in the passage. Give four options: the right one, and three wrong ones that "
                          "sound believable, all about the same length. Do not write \"according to the passage\". "
                          "Use exactly this form and write nothing else:\n\n"
                          "Question: <the question>\nA) <option>\nB) <option>\nC) <option>\nD) <option>\nAnswer: <the letter of the right "
                          "option>\nWhy: <one sentence from the passage that shows it>")
        .arg(lessonTitle, course.title, levelForPrompt(course.level), passage.left(2500));
}

std::optional<Classroom::Question> Classroom::parseQuestion(const QString &text)
{
    static const QRegularExpression option(QStringLiteral("^\\(?([A-Da-d])[).:]\\s*(.+)$"));
    static const QRegularExpression label(QStringLiteral("^(question|answer|correct answer|why|explanation)\\s*:\\s*(.*)$"), QRegularExpression::CaseInsensitiveOption);
    Question out;
    QString options[4];
    QString answerLetter;
    for (const auto qLatin1Char = text.split(QLatin1Char('\n')); QString line : qLatin1Char) {
        line = line.trimmed();
        line.remove(QStringLiteral("**"));
        line = line.trimmed();
        if (line.startsWith(QLatin1String("- ")))
            line = line.mid(2).trimmed();
        if (line.isEmpty())
            continue;
        if (const auto m = label.match(line); m.hasMatch()) {
            const QString key = m.captured(1).toLower();
            const QString value = m.captured(2).trimmed();
            if (key == QLatin1String("question"))
                out.text = value;
            else if (key.contains(QLatin1String("answer")))
                answerLetter = value.left(3);
            else
                out.explanation = value;
            continue;
        }
        if (const auto m = option.match(line); m.hasMatch()) {
            options[m.captured(1).toUpper().at(0).unicode() - 'A'] = m.captured(2).trimmed();
            continue;
        }
        if (out.text.isEmpty() && line.endsWith(QLatin1Char('?')))  // a question without its label
            out.text = line;
    }
    static const QRegularExpression letter(QStringLiteral("([A-Da-d])"));
    const auto lm = letter.match(answerLetter.remove(QLatin1Char('(')));
    if (out.text.isEmpty() || !lm.hasMatch())
        return std::nullopt;
    QSet<QString> distinct;
    for (const QString &o : options) {
        if (o.isEmpty())
            return std::nullopt;
        distinct.insert(o.simplified().toLower());
        out.options << o;
    }
    if (distinct.size() != 4)
        return std::nullopt;
    out.answer = lm.captured(1).toUpper().at(0).unicode() - 'A';
    return out;
}

bool Classroom::answerInPassage(const Question &question, const QString &passage)
{
    const QString answer = question.options.value(question.answer);
    if (answer.isEmpty())
        return false;
    // a figure ("1883", "6,000 km") must be there as it is written
    static const QRegularExpression figure(QStringLiteral("\\d[\\d,.]*"));
    for (auto it = figure.globalMatch(answer); it.hasNext();) {
        const QString f = it.next().captured(0);
        if (f.size() > 1 && !passage.contains(f))
            return false;
    }
    auto share = [&passage](const QString &option) {
        QStringList stems;
        for (const auto terms = ZimSearch::terms(option); const QString &t : terms)
            stems << ZimSearch::stem(t);
        return stems.isEmpty() ? (passage.contains(option, Qt::CaseInsensitive) ? 1.0 : 0.0) : ZimSearch::overlap(stems, passage);
    };
    // the question's own words too: "What ended the conflict between Caesar and Pompey?" was answered "the Battle of
    // Actium" from a passage about Antony and Octavian (2026-10-20)
    if (share(question.text) < 0.5)
        return false;
    // most of its words, and no wrong option more of them (a key pointing at the wrong letter is caught here)
    const double right = share(answer);
    if (right < 0.5)
        return false;
    for (int i = 0; i < question.options.size(); ++i)
        if (i != question.answer && share(question.options.at(i)) > right)
            return false;
    return true;
}

Classroom::Question Classroom::shuffled(Question question, quint32 seed)
{
    QRandomGenerator random(seed);
    QList<int> order{0, 1, 2, 3};
    for (int i = 3; i > 0; --i)
        std::swap(order[i], order[int(random.bounded(i + 1))]);
    QStringList options;
    int answer = 0;
    for (int i = 0; i < order.size(); ++i) {
        options << question.options.value(order.at(i));
        if (order.at(i) == question.answer)
            answer = i;
    }
    question.options = options;
    question.answer = answer;
    return question;
}

// model prompt: stays English (the model is asked in English and its reply is parsed)
QString Classroom::tutorPrompt(const Course &course, const Profile &profile, const Lesson &lesson)
{
    const QString name = profile.name.trimmed().isEmpty() ? QStringLiteral("the student") : profile.name.trimmed().section(QLatin1Char(' '), 0, 0);
    return QStringLiteral(
               "You are %1's teacher for the lesson \"%2\" of the course \"%3\". %1 is at the level of %4. The sources below are the "
               "lesson's readings.\n\n"
               "Help %1 learn, rather than doing the work for them. When they ask a question the lesson wants them to answer, do not just "
               "give the answer: give a hint, say which source has it, and ask a short question that leads them there. When they give "
               "an answer, say whether it is right, using the sources, and explain briefly; if it is partly right, say which part. When they "
               "ask for an explanation of something hard, explain it simply, with an example, then check they followed with one question. "
               "Use only facts from the sources; if they do not cover something, say so and suggest what to read. Keep replies short "
               "(under 120 words), warm, and in words that suit the level. Do not write source numbers.")
        .arg(name, lesson.title, course.title, levelForPrompt(course.level));
}
