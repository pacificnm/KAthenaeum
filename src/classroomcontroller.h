// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class Teacher;

// The classroom, for QML: the learner's profile, the subjects and levels, courses and lessons, and the AI teacher's one
// job at a time (planning a course, or writing a lesson's introduction), app-wide so it carries on when its page closes.
class ClassroomController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Classroom)
    QML_SINGLETON
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY profileChanged)
    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY profileChanged)
    Q_PROPERTY(QVariantList courses READ courses NOTIFY coursesChanged)
    Q_PROPERTY(QVariantList subjects READ subjects CONSTANT)
    Q_PROPERTY(QVariantList levels READ levels CONSTANT)
    Q_PROPERTY(QVariantList grades READ grades CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString job READ job NOTIFY stateChanged)  // "plan" | "intro" | ""
    Q_PROPERTY(qint64 jobSubject READ jobSubject NOTIFY stateChanged)  // the course being planned, or the lesson (or unit)
    Q_PROPERTY(QString quizKind READ quizKind NOTIFY stateChanged)  // a quiz being written: "lesson" or "unit"
    Q_PROPERTY(int step READ step NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(QString label READ label NOTIFY stateChanged)
    Q_PROPERTY(QString streamed READ streamed NOTIFY stateChanged)  // an introduction as it is written

public:
    explicit ClassroomController(QObject *parent = nullptr);

    QVariantMap profile() const;
    bool hasProfile() const;
    QVariantList courses() const;
    QVariantList subjects() const;
    QVariantList levels() const;
    QVariantList grades() const;
    bool busy() const;
    QString job() const;
    qint64 jobSubject() const;
    int step() const { return m_step; }
    int total() const { return m_total; }
    QString label() const { return m_label; }
    QString streamed() const { return m_streamed; }
    QString quizKind() const { return m_quizKind; }

    // {name, grade, interests, goals}
    Q_INVOKABLE bool saveProfile(const QVariantMap &profile);
    Q_INVOKABLE QString defaultLevel() const;  // the level after the learner's highest grade
    Q_INVOKABLE QString levelLabel(const QString &level) const;
    Q_INVOKABLE QVariantMap subject(const QString &key) const;
    // Which installed libraries a course would read: {ok, libraries: [title], suggestion: catalog title to get when none
    // is installed, simpleSuggestion: Wikipedia in Simple English for a young learner who does not have it}
    Q_INVOKABLE QVariantMap coverage(const QString &subject, const QString &level) const;

    // Creates the course and starts planning it; its id, or 0
    Q_INVOKABLE qint64 createCourse(const QString &subject, const QString &title, const QString &level, const QString &focus, const QString &length);
    Q_INVOKABLE bool planAgain(qint64 courseId);
    Q_INVOKABLE bool removeCourse(qint64 courseId);
    // {id, title, subject, subjectName, icon, level, levelLabel, focus, length, status, error, lessons, completed,
    //  units: [{id, title, lessons: [{id, title, objective, completed, hasIntro, readings}]}]}
    Q_INVOKABLE QVariantMap course(qint64 courseId) const;
    // {id, courseId, courseTitle, unitTitle, title, objective, intro, completed, number, count, previousId, nextId,
    //  readings: [{n, libraryId, path, title, library}]}
    Q_INVOKABLE QVariantMap lesson(qint64 lessonId) const;
    // false when the teacher is busy with another job (a plan is never stopped for an introduction)
    Q_INVOKABLE bool writeIntro(qint64 lessonId);
    Q_INVOKABLE bool setCompleted(qint64 lessonId, bool completed);
    Q_INVOKABLE void stop();

    // ---- quizzes, tests and review ----
    // Has the teacher write a lesson's quiz ("lesson", lessonId) or a unit's test ("unit", unitId); false when busy
    Q_INVOKABLE bool makeQuiz(const QString &kind, qint64 targetId);
    // The newest quiz or test: {id, kind, questions: [{text, options}], result: (see submit) | absent} or an empty map
    Q_INVOKABLE QVariantMap quiz(const QString &kind, qint64 targetId) const;
    // Grades it and keeps the attempt; passing a lesson's quiz marks the lesson finished.
    // {score, total, percent, passed, questions: [{text, options, answer, chosen, correct, explanation, evidence, libraryId,
    //  path, title, lessonTitle}]}
    Q_INVOKABLE QVariantMap submitQuiz(qint64 quizId, const QVariantList &answers);
    // What was got wrong in the course's latest attempts (the same shape as a graded question, with lessonId)
    Q_INVOKABLE QVariantList missed(qint64 courseId) const;
    // The most recent chat with the teacher about the lesson, or 0
    Q_INVOKABLE qint64 latestChat(qint64 lessonId) const;

    // ---- a course kept in a research project ----
    // Ties the course to a project (0: a new one named after the course) and puts in what its lessons have so far.
    // The project's id, or 0.
    Q_INVOKABLE qint64 tieProject(qint64 courseId, qint64 projectId);
    Q_INVOKABLE bool untieProject(qint64 courseId);
    // {id, title} of the course a project is kept for, or an empty map
    Q_INVOKABLE QVariantMap courseForProject(qint64 projectId) const;
    // A highlight made in one of the lesson's readings, kept in its course's project too (when it has one)
    Q_INVOKABLE bool keepHighlight(qint64 lessonId, qint64 libraryId, const QString &path, const QString &title, const QString &text,
                                   const QString &note);

Q_SIGNALS:
    void profileChanged();
    void coursesChanged();
    void stateChanged();
    void courseReady(qint64 courseId, const QString &title);
    void introReady(qint64 lessonId);
    void quizReady(const QString &kind, qint64 targetId);
    void failed(const QString &message);

private:
    bool plan(qint64 courseId);

    Teacher *m_teacher;
    int m_step = 0;
    int m_total = 0;
    QString m_label;
    QString m_streamed;
    QString m_quizKind;
};
