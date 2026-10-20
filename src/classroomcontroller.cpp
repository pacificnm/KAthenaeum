// SPDX-License-Identifier: GPL-3.0-or-later
#include "classroomcontroller.h"

#include "core/classroom.h"
#include "core/library.h"
#include "core/teacher.h"
#include "modelsettings.h"
#include "projectlist.h"

#include "core/projects.h"

#include "core/chats.h"

#include <KLocalizedString>

namespace
{
// the best score of the newest quiz of a lesson or test of a unit, in percent; -1 if not taken
int bestPercent(const QString &kind, qint64 targetId)
{
    const auto q = Classroom::latestQuiz(kind, targetId);
    const auto a = q ? Classroom::bestAttempt(q->id) : std::nullopt;
    return a && a->total > 0 ? int(std::lround(100.0 * a->score / a->total)) : -1;
}

QVariantMap graded(const Classroom::Question &q, int chosen)
{
    const auto lesson = Classroom::lesson(q.lessonId);
    return {{QStringLiteral("text"), q.text},
            {QStringLiteral("options"), q.options},
            {QStringLiteral("answer"), q.answer},
            {QStringLiteral("chosen"), chosen},
            {QStringLiteral("correct"), chosen == q.answer},
            {QStringLiteral("explanation"), q.explanation},
            {QStringLiteral("evidence"), q.evidence},
            {QStringLiteral("libraryId"), q.source.libraryId},
            {QStringLiteral("path"), q.source.path},
            {QStringLiteral("title"), q.source.title},
            {QStringLiteral("lessonId"), q.lessonId},
            {QStringLiteral("lessonTitle"), lesson ? lesson->title : QString()}};
}

QVariantMap result(const Classroom::Quiz &quiz, const Classroom::Attempt &attempt)
{
    QVariantList questions;
    for (int i = 0; i < quiz.questions.size(); ++i)
        questions << graded(quiz.questions.at(i), attempt.answers.value(i, -1));
    const int percent = attempt.total > 0 ? int(std::lround(100.0 * attempt.score / attempt.total)) : 0;
    return {{QStringLiteral("score"), attempt.score},
            {QStringLiteral("total"), attempt.total},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("passed"), attempt.total > 0 && double(attempt.score) / attempt.total >= Classroom::PassMark},
            {QStringLiteral("questions"), questions}};
}

Teacher::Models models()
{
    ModelSettings *m = ModelSettings::instance();
    return {m->answerModel(), m->embeddingModel()};
}
}

ClassroomController::ClassroomController(QObject *parent)
    : QObject(parent)
    , m_teacher(new Teacher(ModelSettings::instance()->client(), this))
{
    Classroom::reconcile();  // a plan the last run left unfinished
    connect(m_teacher, &Teacher::progress, this, [this](int step, int total, const QString &label) {
        m_step = step;
        m_total = total;
        m_label = label;
        Q_EMIT stateChanged();
    });
    connect(m_teacher, &Teacher::textAdded, this, [this](const QString &text) {
        m_streamed += text;
        Q_EMIT stateChanged();
    });
    connect(m_teacher, &Teacher::finished, this, [this](const QString &status, const QString &error) {
        const QString job = m_teacher->job();
        const qint64 subject = m_teacher->subject();
        m_label.clear();
        m_streamed.clear();
        Q_EMIT stateChanged();
        Q_EMIT coursesChanged();
        if (job == QLatin1String("plan")) {
            const auto c = Classroom::course(subject);
            if (status == QLatin1String("done") && c && c->projectId > 0) {  // the new lessons' readings go into its project
                Classroom::syncCourse(subject);
                Q_EMIT ProjectsNotifier::instance()->changed();
            }
            if (status == QLatin1String("done") && c)
                Q_EMIT courseReady(subject, c->title);
            else if (status == QLatin1String("error"))
                Q_EMIT failed(i18n("The course could not be planned: %1", error));
        } else if (job == QLatin1String("quiz")) {
            if (status == QLatin1String("done"))
                Q_EMIT quizReady(m_quizKind, subject);
            else if (status == QLatin1String("error"))
                Q_EMIT failed(i18n("The quiz could not be written: %1", error));
        } else if (status == QLatin1String("done")) {
            if (Classroom::syncLesson(subject))  // the introduction, as a document of the course's project
                Q_EMIT ProjectsNotifier::instance()->changed();
            Q_EMIT introReady(subject);
        } else if (status == QLatin1String("error")) {
            Q_EMIT failed(i18n("The introduction could not be written: %1", error));
        }
    });
}

// ---- the learner ----

QVariantMap ClassroomController::profile() const
{
    const Classroom::Profile p = Classroom::profile();
    return {{QStringLiteral("name"), p.name},
            {QStringLiteral("grade"), p.grade.isEmpty() ? QStringLiteral("none") : p.grade},
            {QStringLiteral("gradeLabel"), Classroom::gradeLabel(p.grade)},
            {QStringLiteral("interests"), p.interests},
            {QStringLiteral("goals"), p.goals}};
}

bool ClassroomController::hasProfile() const
{
    return Classroom::profile().exists();
}

bool ClassroomController::saveProfile(const QVariantMap &profile)
{
    const bool ok = Classroom::saveProfile({profile.value(QStringLiteral("name")).toString(), profile.value(QStringLiteral("grade")).toString(),
                                            profile.value(QStringLiteral("interests")).toString(), profile.value(QStringLiteral("goals")).toString()});
    Q_EMIT profileChanged();
    return ok;
}

QString ClassroomController::defaultLevel() const
{
    return Classroom::nextLevel(Classroom::profile().grade);
}

QString ClassroomController::levelLabel(const QString &level) const
{
    return Classroom::levelLabel(level);
}

// ---- subjects and levels ----

QVariantList ClassroomController::subjects() const
{
    QVariantList out;
    for (const Classroom::Subject &s : Classroom::subjects())
        out.append(QVariantMap{{QStringLiteral("key"), s.key}, {QStringLiteral("name"), s.name}, {QStringLiteral("icon"), s.icon}, {QStringLiteral("blurb"), s.blurb}});
    return out;
}

QVariantMap ClassroomController::subject(const QString &key) const
{
    const Classroom::Subject s = Classroom::subject(key);
    return {{QStringLiteral("key"), s.key}, {QStringLiteral("name"), s.name}, {QStringLiteral("icon"), s.icon}, {QStringLiteral("blurb"), s.blurb}};
}

QVariantList ClassroomController::levels() const
{
    QVariantList out;
    for (const QString &k : Classroom::levels())
        out.append(QVariantMap{{QStringLiteral("key"), k}, {QStringLiteral("label"), Classroom::levelLabel(k)}});
    return out;
}

QVariantList ClassroomController::grades() const
{
    QVariantList out;
    for (const QString &k : Classroom::gradesCompleted())
        out.append(QVariantMap{{QStringLiteral("key"), k}, {QStringLiteral("label"), Classroom::gradeLabel(k)}});
    return out;
}

QVariantMap ClassroomController::coverage(const QString &subject, const QString &level) const
{
    const QList<LibraryRecord> installed = Library::all();
    const QList<LibraryRecord> libraries = Classroom::librariesFor(subject, installed);
    QStringList titles;
    bool simple = false;
    for (const LibraryRecord &l : libraries) {
        titles << l.title;
        simple = simple || Classroom::isSimpleEnglish(l);
    }
    QVariantMap out{{QStringLiteral("ok"), !libraries.isEmpty()}, {QStringLiteral("libraries"), titles}};
    if (libraries.isEmpty())
        out.insert(QStringLiteral("suggestion"), Classroom::isYoung(level) ? QStringLiteral("Wikipedia in simple English") : Classroom::subject(subject).suggestion);
    else if (Classroom::isYoung(level) && !simple)
        out.insert(QStringLiteral("simpleSuggestion"), QStringLiteral("Wikipedia in simple English"));
    return out;
}

// ---- courses ----

QVariantList ClassroomController::courses() const
{
    QVariantList out;
    for (const Classroom::Course &c : Classroom::courses()) {
        const Classroom::Subject s = Classroom::subject(c.subject);
        out.append(QVariantMap{{QStringLiteral("id"), c.id},
                               {QStringLiteral("title"), c.title},
                               {QStringLiteral("subject"), s.key},
                               {QStringLiteral("subjectName"), s.name},
                               {QStringLiteral("icon"), s.icon},
                               {QStringLiteral("level"), c.level},
                               {QStringLiteral("levelLabel"), Classroom::levelLabel(c.level)},
                               {QStringLiteral("status"), c.status},
                               {QStringLiteral("error"), c.error},
                               {QStringLiteral("lessons"), c.lessons},
                               {QStringLiteral("completed"), c.completed}});
    }
    return out;
}

bool ClassroomController::plan(qint64 courseId)
{
    const auto c = Classroom::course(courseId);
    if (!c)
        return false;
    if (m_teacher->running()) {
        Q_EMIT failed(i18n("The teacher is busy. Wait for it to finish, or stop it, then try again."));
        return false;
    }
    QList<qint64> libraries, simple;
    for (const LibraryRecord &l : Classroom::librariesFor(c->subject, Library::all())) {
        libraries << l.id;
        if (Classroom::isYoung(c->level) && Classroom::isSimpleEnglish(l))
            simple << l.id;
    }
    m_streamed.clear();
    m_teacher->planCourse(courseId, libraries, simple, models());
    Q_EMIT coursesChanged();
    Q_EMIT stateChanged();
    return true;
}

qint64 ClassroomController::createCourse(const QString &subject, const QString &title, const QString &level, const QString &focus, const QString &length)
{
    const qint64 id = Classroom::createCourse(subject, title, level, focus, length);
    if (id <= 0)
        return 0;
    Q_EMIT coursesChanged();
    plan(id);
    return id;
}

bool ClassroomController::planAgain(qint64 courseId)
{
    return plan(courseId);
}

bool ClassroomController::removeCourse(qint64 courseId)
{
    if (m_teacher->running() && m_teacher->job() == QLatin1String("plan") && m_teacher->subject() == courseId)
        m_teacher->stop();
    const bool ok = Classroom::removeCourse(courseId);
    Q_EMIT coursesChanged();
    return ok;
}

QVariantMap ClassroomController::course(qint64 courseId) const
{
    const auto c = Classroom::course(courseId);
    if (!c)
        return {};
    const Classroom::Subject s = Classroom::subject(c->subject);
    const auto project = c->projectId > 0 ? Projects::byId(c->projectId) : std::nullopt;
    QVariantList units;
    for (const Classroom::Unit &u : Classroom::syllabus(courseId)) {
        QVariantList lessons;
        for (const Classroom::Lesson &l : u.lessons)
            lessons.append(QVariantMap{{QStringLiteral("id"), l.id},
                                       {QStringLiteral("quizScore"), bestPercent(QStringLiteral("lesson"), l.id)},
                                       {QStringLiteral("title"), l.title},
                                       {QStringLiteral("objective"), l.objective},
                                       {QStringLiteral("completed"), l.completed()},
                                       {QStringLiteral("hasIntro"), !l.intro.isEmpty()},
                                       {QStringLiteral("readings"), int(l.readings.size())}});
        units.append(QVariantMap{{QStringLiteral("id"), u.id},
                                 {QStringLiteral("title"), u.title},
                                 {QStringLiteral("testScore"), bestPercent(QStringLiteral("unit"), u.id)},
                                 {QStringLiteral("lessons"), lessons}});
    }
    return {{QStringLiteral("id"), c->id},
            {QStringLiteral("title"), c->title},
            {QStringLiteral("subject"), s.key},
            {QStringLiteral("subjectName"), s.name},
            {QStringLiteral("icon"), s.icon},
            {QStringLiteral("level"), c->level},
            {QStringLiteral("levelLabel"), Classroom::levelLabel(c->level)},
            {QStringLiteral("focus"), c->focus},
            {QStringLiteral("length"), c->length},
            {QStringLiteral("status"), c->status},
            {QStringLiteral("error"), c->error},
            {QStringLiteral("lessons"), c->lessons},
            {QStringLiteral("completed"), c->completed},
            {QStringLiteral("projectId"), c->projectId},
            {QStringLiteral("projectTitle"), project ? project->title : QString()},
            {QStringLiteral("units"), units}};
}

QVariantMap ClassroomController::lesson(qint64 lessonId) const
{
    const auto l = Classroom::lesson(lessonId);
    if (!l)
        return {};
    const auto c = Classroom::course(l->courseId);
    const auto project = c && c->projectId > 0 ? Projects::byId(c->projectId) : std::nullopt;
    // where it is in the course: its number, and the lessons before and after it
    QList<qint64> order;
    QString unitTitle;
    for (const Classroom::Unit &u : Classroom::syllabus(l->courseId))
        for (const Classroom::Lesson &other : u.lessons) {
            order << other.id;
            if (other.id == lessonId)
                unitTitle = u.title;
        }
    const qsizetype at = order.indexOf(lessonId);
    QVariantList readings;
    for (int i = 0; i < l->readings.size(); ++i) {
        const Classroom::Reading &r = l->readings.at(i);
        const auto library = Library::byId(r.article.libraryId);
        readings.append(QVariantMap{{QStringLiteral("n"), i + 1},
                                    {QStringLiteral("libraryId"), r.article.libraryId},
                                    {QStringLiteral("path"), r.article.path},
                                    {QStringLiteral("title"), r.article.title},
                                    {QStringLiteral("library"), library ? library->title : QString()},
                                    {QStringLiteral("available"), library && library->fileExists()}});
    }
    return {{QStringLiteral("id"), l->id},
            {QStringLiteral("courseId"), l->courseId},
            {QStringLiteral("courseTitle"), c ? c->title : QString()},
            {QStringLiteral("level"), c ? c->level : QString()},
            {QStringLiteral("unitTitle"), unitTitle},
            {QStringLiteral("title"), l->title},
            {QStringLiteral("objective"), l->objective},
            {QStringLiteral("intro"), l->intro},
            {QStringLiteral("completed"), l->completed()},
            {QStringLiteral("number"), int(at + 1)},
            {QStringLiteral("count"), int(order.size())},
            {QStringLiteral("projectId"), project ? project->id : 0},
            {QStringLiteral("projectTitle"), project ? project->title : QString()},
            {QStringLiteral("previousId"), at > 0 ? order.at(at - 1) : 0},
            {QStringLiteral("nextId"), at >= 0 && at + 1 < order.size() ? order.at(at + 1) : 0},
            {QStringLiteral("readings"), readings}};
}

bool ClassroomController::writeIntro(qint64 lessonId)
{
    if (m_teacher->running())
        return false;
    m_streamed.clear();
    m_teacher->writeIntro(lessonId, models());
    Q_EMIT stateChanged();
    return true;
}

bool ClassroomController::setCompleted(qint64 lessonId, bool completed)
{
    const bool ok = Classroom::setCompleted(lessonId, completed);
    Q_EMIT coursesChanged();
    return ok;
}

void ClassroomController::stop()
{
    m_teacher->stop();
}

bool ClassroomController::busy() const
{
    return m_teacher->running();
}

QString ClassroomController::job() const
{
    return m_teacher->running() ? m_teacher->job() : QString();
}

qint64 ClassroomController::jobSubject() const
{
    return m_teacher->running() ? m_teacher->subject() : 0;
}

// ---- a course kept in a research project ----

qint64 ClassroomController::tieProject(qint64 courseId, qint64 projectId)
{
    const auto c = Classroom::course(courseId);
    if (!c)
        return 0;
    qint64 id = projectId;
    if (id <= 0) {
        // the course's goal for the project chat and drafts: what it is, at what level, and what the learner asked for
        QString goal = i18n("The course “%1” (%2, %3).", c->title, Classroom::subject(c->subject).name, Classroom::levelLabel(c->level));
        if (!c->focus.isEmpty())
            goal += QLatin1Char(' ') + i18n("Focus: %1", c->focus);
        id = Projects::create(c->title, goal);
    }
    if (id <= 0 || !Projects::byId(id) || !Classroom::setProject(courseId, id))
        return 0;
    Classroom::syncCourse(courseId);
    Q_EMIT ProjectsNotifier::instance()->changed();
    Q_EMIT coursesChanged();
    return id;
}

bool ClassroomController::untieProject(qint64 courseId)
{
    const bool ok = Classroom::setProject(courseId, 0);
    Q_EMIT ProjectsNotifier::instance()->changed();
    Q_EMIT coursesChanged();
    return ok;
}

QVariantMap ClassroomController::courseForProject(qint64 projectId) const
{
    const auto c = Classroom::courseForProject(projectId);
    if (!c)
        return {};
    return {{QStringLiteral("id"), c->id}, {QStringLiteral("title"), c->title}};
}

bool ClassroomController::keepHighlight(qint64 lessonId, qint64 libraryId, const QString &path, const QString &title, const QString &text,
                                        const QString &note)
{
    const bool kept = Classroom::keepHighlight(lessonId, text, {libraryId, path, title}, note) > 0;
    if (kept)
        Q_EMIT ProjectsNotifier::instance()->changed();
    return kept;
}

// ---- quizzes, tests and review ----

bool ClassroomController::makeQuiz(const QString &kind, qint64 targetId)
{
    if (m_teacher->running())
        return false;
    m_quizKind = kind == QLatin1String("unit") ? QStringLiteral("unit") : QStringLiteral("lesson");
    m_streamed.clear();
    m_teacher->writeQuiz(m_quizKind, targetId, models());
    Q_EMIT stateChanged();
    return true;
}

QVariantMap ClassroomController::quiz(const QString &kind, qint64 targetId) const
{
    const auto q = Classroom::latestQuiz(kind, targetId);
    if (!q)
        return {};
    QVariantList questions;
    for (const Classroom::Question &x : q->questions)
        questions.append(QVariantMap{{QStringLiteral("text"), x.text}, {QStringLiteral("options"), x.options}});
    QVariantMap out{{QStringLiteral("id"), q->id}, {QStringLiteral("kind"), q->kind}, {QStringLiteral("questions"), questions}};
    if (const auto a = Classroom::latestAttempt(q->id))
        out.insert(QStringLiteral("result"), result(*q, *a));
    return out;
}

QVariantMap ClassroomController::submitQuiz(qint64 quizId, const QVariantList &answers)
{
    QList<int> chosen;
    for (const QVariant &v : answers)
        chosen << (v.isValid() && !v.isNull() ? v.toInt() : -1);
    const auto attempt = Classroom::submit(quizId, chosen);
    const auto q = attempt ? Classroom::quiz(quizId) : std::nullopt;
    if (!q)
        return {};
    const QVariantMap out = result(*q, *attempt);
    // passing a lesson's quiz finishes the lesson
    if (q->kind == QLatin1String("lesson") && out.value(QStringLiteral("passed")).toBool())
        Classroom::setCompleted(q->lessonId, true);
    Q_EMIT coursesChanged();
    return out;
}

QVariantList ClassroomController::missed(qint64 courseId) const
{
    QVariantList out;
    for (const Classroom::Missed &m : Classroom::missed(courseId)) {
        QVariantMap g = graded(m.question, m.chosen);
        g.insert(QStringLiteral("quizKind"), m.quizKind);
        out << g;
    }
    return out;
}

qint64 ClassroomController::latestChat(qint64 lessonId) const
{
    const QList<ChatRecord> chats = Chats::aboutLesson(lessonId);
    return chats.isEmpty() ? 0 : chats.first().id;
}
