// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "projects.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

struct LibraryRecord;

// The classroom: the learner's profile, courses with a syllabus of units and lessons, each lesson's readings (real
// articles from the installed libraries) and the teacher's introduction to it. Schema v10. The AI teacher that plans
// courses and writes introductions is Teacher (teacher.h).
namespace Classroom
{
// ---- the learner (one per computer) ----
struct Profile {
    QString name;
    QString grade;      // the highest grade completed: a level key ("8", "12", "college") or "none"
    QString interests;  // what they like, to choose examples
    QString goals;      // what they want to learn for
    bool exists() const { return !name.trimmed().isEmpty(); }
};
Profile profile();
bool saveProfile(const Profile &p);

// ---- levels ----
// "k", "1".."12", "college", "adult": a course's level. A profile's grade uses the same keys, plus "none" (not yet
// through kindergarten) and "degree" (a college degree).
QStringList levels();
QStringList gradesCompleted();
QString levelLabel(const QString &key);  // "Grade 7", "Kindergarten", "College", "Adult learner"
QString gradeLabel(const QString &key);  // "Grade 8", "None yet", "Some college", "College degree"
QString levelForPrompt(const QString &key);  // "Grade 7 (about 12 years old)": what the model is told
QString nextLevel(const QString &gradeCompleted);  // the level a new course starts at
bool isYoung(const QString &level);  // kindergarten to grade 5: Wikipedia in Simple English first, when it is installed

// ---- subjects ----
struct Subject {
    QString key;   // "math"
    QString name;  // "Mathematics"
    QString icon;
    QString blurb;
    QStringList libraries;  // Kiwix names of the libraries made for it ("wikipedia_en_mathematics")
    QString suggestion;     // the catalog title to offer when no library serves it ("Mathematics by Wikipedia")
};
QList<Subject> subjects();  // the core subjects, in the order shown
Subject subject(const QString &key);  // an unknown key: "Other" (a course of the learner's own)
// The installed libraries a course in this subject reads from: those made for it, and the general Wikipedias (the whole
// English Wikipedia, the best-of selections, Simple English). Never an unrelated topic library ("Ray Charles").
QList<LibraryRecord> librariesFor(const QString &subjectKey, const QList<LibraryRecord> &installed);
// The general Wikipedias, which serve every subject
bool isGeneral(const LibraryRecord &library);
bool isSimpleEnglish(const LibraryRecord &library);

// ---- courses ----
struct Reading {
    ArticleRef article;
    double score = 0;  // how well it matched the lesson (similarity), for ordering
};
struct Lesson {
    qint64 id = 0;
    qint64 courseId = 0;
    qint64 unitId = 0;
    int position = 0;
    QString title;
    QString objective;  // what the learner will learn
    QString article;    // the encyclopedia article the teacher named for it ("Plate tectonics"): looked up first
    QList<Reading> readings;
    QString intro;  // the teacher's introduction (Markdown), written when the lesson is first opened
    QDateTime completedAt;
    bool completed() const { return completedAt.isValid(); }
};
struct Unit {
    qint64 id = 0;
    int position = 0;
    QString title;
    QList<Lesson> lessons;
};
struct Course {
    qint64 id = 0;
    QString subject;
    QString title;
    QString level;
    QString focus;   // what the learner asked the course to cover, in their words
    QString length;  // short | standard | long
    QString status;  // new | planning | ready | failed
    QString error;
    QDateTime updatedAt;
    qint64 projectId = 0;  // the research project it is kept in, if any
    int lessons = 0;
    int completed = 0;
};
constexpr int MaxUnits = 6;
constexpr int MaxLessonsPerUnit = 5;
// units x lessons for a course length
std::pair<int, int> shape(const QString &length);

qint64 createCourse(const QString &subject, const QString &title, const QString &level, const QString &focus, const QString &length);
bool removeCourse(qint64 id);
bool setStatus(qint64 id, const QString &status, const QString &error = {});
// A previous run's plan that never finished: failed, with a word on what to do
void reconcile();
QList<Course> courses();  // most recently used first
std::optional<Course> course(qint64 id);
// Replaces the course's syllabus (a new plan)
bool saveSyllabus(qint64 courseId, const QList<Unit> &units);
QList<Unit> syllabus(qint64 courseId);
qint64 unitCourse(qint64 unitId);  // the course a unit is in, or 0
std::optional<Lesson> lesson(qint64 id);
bool setIntro(qint64 lessonId, const QString &markdown);

// ---- a course kept in a research project ----
bool setProject(qint64 courseId, qint64 projectId);  // 0: none
std::optional<Course> courseForProject(qint64 projectId);
QString lessonLabel(qint64 lessonId);  // "Lesson 3: Fractions", numbered through the course
// Puts what the lesson has into its course's project: its readings as sources, its introduction as a document (once;
// again if that document was deleted). False if the course has no project.
bool syncLesson(qint64 lessonId);
void syncCourse(qint64 courseId);
// A highlight made while reading for the lesson, kept in its course's project as an excerpt (once). Its item id, or 0.
qint64 keepHighlight(qint64 lessonId, const QString &text, const ArticleRef &article, const QString &note);
// The introduction as a project document: the teacher's words, the readings linked, and room for the answers
QString lessonDocument(const Lesson &lesson);
bool setCompleted(qint64 lessonId, bool completed);

// ---- quizzes (a lesson's) and tests (a unit's) ----
struct Question {
    qint64 id = 0;
    QString text;
    QStringList options;  // four
    int answer = 0;       // index into options
    QString explanation;
    QString evidence;  // the passage it was written from, which has the answer
    ArticleRef source;
    qint64 lessonId = 0;
};
struct Quiz {
    qint64 id = 0;
    qint64 courseId = 0;
    qint64 lessonId = 0;  // a lesson's quiz
    qint64 unitId = 0;    // a unit's test
    QString kind;         // lesson | unit
    QList<Question> questions;
};
struct Attempt {
    qint64 id = 0;
    QList<int> answers;
    int score = 0;
    int total = 0;
    QDateTime at;
};
constexpr int QuizQuestions = 5;
constexpr int TestQuestionsPerLesson = 2;
constexpr double PassMark = 0.7;
qint64 saveQuiz(const Quiz &quiz);
std::optional<Quiz> quiz(qint64 id);
// The newest quiz of a lesson ("lesson", lessonId) or test of a unit ("unit", unitId)
std::optional<Quiz> latestQuiz(const QString &kind, qint64 targetId);
// Grades the answers (index per question, -1 none) and keeps the attempt; nothing for an unknown quiz
std::optional<Attempt> submit(qint64 quizId, const QList<int> &answers);
std::optional<Attempt> latestAttempt(qint64 quizId);
std::optional<Attempt> bestAttempt(qint64 quizId);
// What the learner got wrong in their latest attempt at each quiz and test of the course, newest first
struct Missed {
    Question question;
    int chosen = -1;
    QString quizKind;
    QString lessonTitle;
};
QList<Missed> missed(qint64 courseId);

// ---- what the teacher is asked, and how its answers are read ----
// One multiple-choice question on a passage, at the course's level
QString questionPrompt(const Course &course, const QString &lessonTitle, const QString &passage);
// "Question: …", four options "A) …" to "D) …", "Answer: B", "Why: …"; nothing unless it has all of them and the four
// options differ
std::optional<Question> parseQuestion(const QString &text);
// Is the right option stated in the passage? Most of its words (or its figure) must be there, and most of the question's
// own words; no wrong option may match the passage better. A question whose answer the passage does not have is never
// asked (the model is not trusted to know).
bool answerInPassage(const Question &question, const QString &passage);
// The options in another order (the model favours some letters), the answer following its option
Question shuffled(Question question, quint32 seed);
// The tutor: guides the learner through the lesson's readings with hints and questions rather than answers
QString tutorPrompt(const Course &course, const Profile &profile, const Lesson &lesson);
QString syllabusPrompt(const Course &course, const Profile &profile, int units, int lessonsPerUnit);
// "Unit 1: Title" lines, then "- Lesson title | Article title | what it teaches" lines under each (numbered, bulleted,
// bold and "##" forms too; "Title: what it teaches" without an article). The template's own words ("Lesson title",
// "<…>"), which a small model copies, are dropped. Lessons before any unit go in one named after the course. Empty
// units are dropped.
QList<Unit> parseSyllabus(const QString &text, const QString &courseTitle, int maxUnits, int maxLessons);
// What a lesson's articles are searched with: the article the teacher named, else the lesson's title and objective
QString lessonQuery(const Course &course, const Lesson &lesson);
// What its readings are compared with: the lesson, in the course's words ("Cells: what living things are made of
// (Biology)"), so an article about prison cells is far from it
QString lessonMeaning(const Course &course, const Lesson &lesson);
QString introPrompt(const Course &course, const Profile &profile, const Lesson &lesson, const QString &sources);
}
