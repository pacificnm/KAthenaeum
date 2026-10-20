// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "classroom.h"
#include "rag.h"
#include "zimsearch.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <functional>

class OllamaClient;
class QNetworkReply;

namespace Teaching
{
constexpr int CandidateArticles = 6;  // articles read for each lesson, to choose its readings from
constexpr int ReadingsPerLesson = 2;
// How close an article's opening must be to the lesson (nomic-embed-text, query against document) to be one of its
// readings: below it, the article only shares words with the lesson. Measured on real courses (2026-10-20): every
// wrong article was 0.70 or less ("Animal Locomotion" for plate tectonics, 0.659; "Contemporary Turkish literature" for
// "Contemporary Issues in Literature", 0.695), and right first readings 0.74 and up.
constexpr double ReadingMin = 0.70;
// A second reading must be nearly as close as the first: "Geodynamics of terrestrial exoplanets" (0.773) came second to
// "Plate tectonics" (0.839) for a grade 9 lesson
constexpr double SecondReadingGap = 0.04;

struct Choice {
    ZimSearch::Found found;
    double similarity = -1;  // -1: no embedding (ranked by search alone)
};
// The lesson's readings from the articles found for it: the closest above ReadingMin (or, with no similarities, the
// search's own order), at most `n`, preferring articles no earlier lesson uses
QList<Classroom::Reading> chooseReadings(const QList<Choice> &candidates, const QSet<QString> &usedBefore, int n = ReadingsPerLesson);
}

// The AI teacher: plans a course (the syllabus, then the readings of each lesson, found in the libraries) and writes a
// lesson's introduction from its readings. One job at a time; stop() ends it and nothing is kept.
class Teacher : public QObject
{
    Q_OBJECT

public:
    explicit Teacher(OllamaClient *client, QObject *parent = nullptr);
    ~Teacher() override;

    struct Models {
        QString answer;
        QString embedding;
    };

    // `libraries`: those the course reads from, in order; `simpleFirst`: tried first (young learners)
    // (opens archives: call on the UI thread)
    void planCourse(qint64 courseId, const QList<qint64> &libraries, const QList<qint64> &simpleFirst, const Models &models);
    void writeIntro(qint64 lessonId, const Models &models);
    // A lesson's quiz ("lesson", lessonId) or a unit's test ("unit", unitId): questions written from the readings' best
    // passages, each kept only if its answer is in its passage. subject() is then the lesson or unit.
    void writeQuiz(const QString &kind, qint64 targetId, const Models &models);
    void stop();
    bool running() const { return m_running; }
    QString job() const { return m_job; }  // "plan" | "intro" | "quiz"
    qint64 subject() const { return m_subjectId; }  // the course or lesson being worked on
    QString planText() const { return m_planText; }  // the syllabus as the model wrote it (for measuring)
    // every article considered for each lesson, with its similarity, as "lesson\tsimilarity\ttitle" (for measuring)
    QStringList considered() const { return m_considered; }

Q_SIGNALS:
    void progress(int step, int total, const QString &label);
    void textAdded(const QString &text);  // an introduction as it is written
    void finished(const QString &status, const QString &error);  // done | stopped | error

private:
    void complete(const QString &prompt, int numPredict, std::function<void(const QString &, const QString &)> done, bool stream = false);
    void withCapabilities(std::function<void()> next);
    void findReadings();
    void readingsFound(const QList<ZimSearch::Article> &articles);
    void finishPlan();
    void introSources(const QList<ZimSearch::Article> &articles);
    void introWritten(const QString &text);
    void finish(const QString &status, const QString &error = {});
    void quizPassages(const QList<std::pair<qint64, QList<ZimSearch::Article>>> &read);
    void rankQuizPassages();
    void nextQuestion();

    QPointer<OllamaClient> m_client;
    QPointer<QNetworkReply> m_reply;
    Models m_models;
    quint64 m_generation = 0;
    bool m_running = false;
    bool m_think = false;
    QString m_job;
    qint64 m_subjectId = 0;

    Classroom::Course m_course;
    Classroom::Profile m_profile;
    // a plan
    QList<qint64> m_libraries;
    QList<qint64> m_simpleFirst;
    QList<Classroom::Unit> m_units;
    QList<std::pair<int, int>> m_order;  // (unit, lesson) in the order their readings are found
    int m_next = 0;
    QSet<QString> m_used;  // "libraryId/path" of readings already given to a lesson
    QString m_planText;
    QStringList m_considered;
    // a quiz
    QString m_quizKind;
    QList<Classroom::Lesson> m_quizLessons;
    QList<std::pair<qint64, QList<ZimSearch::Article>>> m_quizRead;  // (lesson, its readings)
    int m_quizRank = 0;  // the lesson whose passages are being ranked
    struct QuizPassage {
        qint64 lessonId;
        QString lessonTitle;
        Rag::Candidate passage;
    };
    QList<QuizPassage> m_quizQueue;  // in the order questions are written
    QHash<qint64, int> m_quizWanted;  // questions still wanted per lesson
    QList<Classroom::Question> m_questions;
    int m_quizTried = 0;
    // an introduction
    Classroom::Lesson m_lesson;
    Rag::Context m_context;
};
