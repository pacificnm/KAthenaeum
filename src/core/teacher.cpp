// SPDX-License-Identifier: GPL-3.0-or-later
#include "teacher.h"

#include "drafts.h"
#include "embedding.h"
#include "extract.h"
#include "ollama.h"
#include "zimreader.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QtConcurrentRun>

#include <algorithm>

QList<Classroom::Reading> Teaching::chooseReadings(const QList<Choice> &candidates, const QSet<QString> &usedBefore, int n)
{
    auto key = [](const Choice &c) -> QString {
        return QString::number(c.found.libraryId) + QLatin1Char('/') + c.found.path;
    };
    QList<Choice> eligible;
    QSet<QString> titles;
    for (const Choice &c : candidates) {
        // the same article in two libraries (a subject library and the whole Wikipedia) is one reading
        if (titles.contains(c.found.title.toLower()))
            continue;
        if (c.similarity >= 0 && c.similarity < ReadingMin)
            continue;
        titles.insert(c.found.title.toLower());
        eligible << c;
    }
    // closest first (the search's own order without similarities); an article an earlier lesson already reads goes last
    std::stable_sort(eligible.begin(), eligible.end(), [&](const Choice &a, const Choice &b) {
        const bool aUsed = usedBefore.contains(key(a)), bUsed = usedBefore.contains(key(b));
        if (aUsed != bUsed)
            return !aUsed;
        return a.similarity > b.similarity;
    });
    QList<Classroom::Reading> out;
    for (const Choice &c : std::as_const(eligible)) {
        if (out.size() >= n)
            break;
        // a second reading that only repeats an earlier lesson's, or is much further from the lesson, is not worth it
        if (!out.isEmpty() && (usedBefore.contains(key(c)) || (c.similarity >= 0 && c.similarity < out.first().score - SecondReadingGap)))
            break;
        out << Classroom::Reading{{c.found.libraryId, c.found.path, c.found.title}, c.similarity};
    }
    return out;
}

Teacher::Teacher(OllamaClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

Teacher::~Teacher()
{
    OllamaClient::cancel(m_reply);
}

void Teacher::complete(const QString &prompt, int numPredict, std::function<void(const QString &, const QString &)> done, bool stream)
{
    QJsonObject body{{QStringLiteral("model"), m_models.answer},
                     {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                                         {QStringLiteral("content"), prompt}}}},
                     {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), Rag::Temperature},
                                                             {QStringLiteral("num_ctx"), Rag::NumCtx},
                                                             {QStringLiteral("num_predict"), numPredict}}}};
    if (m_think)
        body.insert(QStringLiteral("think"), false);
    auto text = std::make_shared<QString>();
    const quint64 generation = m_generation;
    m_reply = m_client->chat(
        body, this,
        [this, text, stream, generation](const QString &piece) {
            if (generation != m_generation)
                return;
            *text += piece;
            if (stream)
                Q_EMIT textAdded(piece);
        },
        [this, text, done, generation](const QJsonObject &, const QString &error) {
            if (generation != m_generation)
                return;
            m_reply.clear();
            done(text->trimmed(), error);
        });
}

void Teacher::withCapabilities(std::function<void()> next)
{
    const quint64 generation = m_generation;
    m_client->capabilities(m_models.answer, this, [this, generation, next](const QStringList &capabilities, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        m_think = capabilities.contains(QStringLiteral("thinking"));
        next();
    });
}

// ---- planning a course ----

void Teacher::planCourse(qint64 courseId, const QList<qint64> &libraries, const QList<qint64> &simpleFirst, const Models &models)
{
    stop();
    ++m_generation;
    m_models = models;
    m_running = true;
    m_job = QStringLiteral("plan");
    m_subjectId = courseId;
    m_libraries = libraries;
    m_simpleFirst = simpleFirst;
    m_units.clear();
    m_order.clear();
    m_used.clear();
    m_considered.clear();
    m_planText.clear();
    m_next = 0;
    const auto course = Classroom::course(courseId);
    if (!course) {
        finish(QStringLiteral("error"), QStringLiteral("This course no longer exists."));
        return;
    }
    if (libraries.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("No installed library covers this course."));
        return;
    }
    m_course = *course;
    m_profile = Classroom::profile();
    Classroom::setStatus(courseId, QStringLiteral("planning"));
    ZimSearch::prepare(libraries);  // archives opened here: opening reads the database
    Q_EMIT progress(0, 1, QStringLiteral("Planning the syllabus"));
    withCapabilities([this] {
        const auto [units, lessons] = Classroom::shape(m_course.length);
        complete(Classroom::syllabusPrompt(m_course, m_profile, units, lessons), 900, [this, units, lessons](const QString &text, const QString &error) {
            if (!error.isEmpty()) {
                finish(QStringLiteral("error"), error);
                return;
            }
            m_planText = text;
            m_units = Classroom::parseSyllabus(Rag::tidyAnswer(text), m_course.title, std::min(units + 1, Classroom::MaxUnits),
                                               std::min(lessons + 1, Classroom::MaxLessonsPerUnit));
            if (m_units.isEmpty()) {
                finish(QStringLiteral("error"), QStringLiteral("The teacher could not plan this course. Try again, or say more about what it should cover."));
                return;
            }
            for (int u = 0; u < m_units.size(); ++u)
                for (int l = 0; l < m_units.at(u).lessons.size(); ++l)
                    m_order.append({u, l});
            findReadings();
        });
    });
}

void Teacher::findReadings()
{
    if (m_next >= m_order.size()) {
        finishPlan();
        return;
    }
    const auto [u, l] = m_order.at(m_next);
    const Classroom::Lesson &lesson = m_units.at(u).lessons.at(l);
    Q_EMIT progress(m_next + 1, int(m_order.size()) + 1, QStringLiteral("Finding readings for “%1”").arg(lesson.title));
    // the article the teacher named (not always a real title: "The plate tectonics model"), and the lesson's own title
    // ("The formation of volcanoes" finds "Volcano")
    QStringList queries{Classroom::lessonQuery(m_course, lesson)};
    if (!queries.contains(lesson.title))
        queries << lesson.title;
    const quint64 generation = m_generation;
    auto *watcher = new QFutureWatcher<QList<ZimSearch::Article>>(this);
    connect(watcher, &QFutureWatcher<QList<ZimSearch::Article>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        readingsFound(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([queries, libraries = m_libraries, simple = m_simpleFirst] {
        QList<qint64> rest = libraries;
        rest.removeIf([&simple](qint64 id) {
            return simple.contains(id);
        });
        // the simple libraries' articles first, so a young learner's version of an article is the one kept
        QList<ZimSearch::Article> out;
        QSet<QString> titles;
        for (const QList<qint64> &libs : {simple, rest}) {
            if (libs.isEmpty())
                continue;
            for (const QString &query : queries)
                for (const auto readArticles = ZimSearch::readArticles(query, libs, Teaching::CandidateArticles); const ZimSearch::Article &a : readArticles)
                    if (!titles.contains(a.found.title.toLower())) {
                        titles.insert(a.found.title.toLower());
                        out << a;
                    }
        }
        return out;
    }));
}

void Teacher::readingsFound(const QList<ZimSearch::Article> &articles)
{
    const auto [u, l] = m_order.at(m_next);
    const Classroom::Lesson lesson = m_units.at(u).lessons.at(l);
    auto store = [this, u, l](const QList<Teaching::Choice> &choices) {
        const QList<Classroom::Reading> readings = Teaching::chooseReadings(choices, m_used);
        for (const Classroom::Reading &r : readings)
            m_used.insert(QString::number(r.article.libraryId) + QLatin1Char('/') + r.article.path);
        m_units[u].lessons[l].readings = readings;
        ++m_next;
        findReadings();
    };
    QList<Teaching::Choice> choices;
    QStringList input{Embedding::formatQuery(m_models.embedding, Classroom::lessonMeaning(m_course, lesson))};
    for (const ZimSearch::Article &a : articles) {
        if (a.passages.isEmpty())
            continue;
        choices << Teaching::Choice{a.found, -1};
        input << Embedding::formatDocument(m_models.embedding, QString(a.found.title + QStringLiteral(". ") + a.passages.first().text).left(1500));
    }
    if (choices.isEmpty()) {
        store({});
        return;
    }
    const QString lessonText = lesson.title + lesson.article;
    const quint64 generation = m_generation;
    m_reply = m_client->embed(m_models.embedding, input, this, [this, generation, choices, store, lessonText](const QList<Vector> &vectors, const QString &error) mutable {
        if (generation != m_generation)
            return;
        m_reply.clear();
        if (error.isEmpty() && vectors.size() == choices.size() + 1) {
            for (int i = 0; i < choices.size(); ++i) {
                choices[i].similarity = Embedding::cosine(vectors.at(0), vectors.at(i + 1));
                if (m_simpleFirst.contains(choices.at(i).found.libraryId))
                    choices[i].similarity += 0.03;  // a young learner's version, when it is about as close
                // "Roman Republic (1849–1850)" beside "Roman Republic": a title in brackets is another thing of the same
                // name, unless the lesson asked for one
                if (choices.at(i).found.title.contains(QLatin1Char('(')) && !lessonText.contains(QLatin1Char('(')))
                    choices[i].similarity -= 0.03;
            }
        }
        for (const Teaching::Choice &c : std::as_const(choices))
            m_considered << QStringLiteral("%1\t%2\t%3").arg(m_units.at(m_order.at(m_next).first).lessons.at(m_order.at(m_next).second).title)
                                .arg(c.similarity, 0, 'f', 3)
                                .arg(c.found.title);
        store(choices);
    });
}

void Teacher::finishPlan()
{
    // a lesson nothing in the libraries is about is left out: the teacher never makes up its material
    QList<Classroom::Unit> units = m_units;
    for (Classroom::Unit &unit : units) {
        unit.lessons.removeIf([](const Classroom::Lesson &l) {
            return l.readings.isEmpty();
        });
        for (int i = 0; i < unit.lessons.size(); ++i)
            unit.lessons[i].position = i;
    }
    units.removeIf([](const Classroom::Unit &u) {
        return u.lessons.isEmpty();
    });
    if (units.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("Your libraries have no articles for this course's lessons. Install a library for the subject, "
                                                       "or describe the course differently."));
        return;
    }
    if (!Classroom::saveSyllabus(m_course.id, units)) {
        finish(QStringLiteral("error"), QStringLiteral("The syllabus could not be saved."));
        return;
    }
    finish(QStringLiteral("done"));
}

// ---- a lesson's introduction ----

void Teacher::writeIntro(qint64 lessonId, const Models &models)
{
    stop();
    ++m_generation;
    m_models = models;
    m_running = true;
    m_job = QStringLiteral("intro");
    m_subjectId = lessonId;
    const auto lesson = Classroom::lesson(lessonId);
    const auto course = lesson ? Classroom::course(lesson->courseId) : std::nullopt;
    if (!lesson || !course) {
        finish(QStringLiteral("error"), QStringLiteral("This lesson no longer exists."));
        return;
    }
    m_lesson = *lesson;
    m_course = *course;
    m_profile = Classroom::profile();
    QList<qint64> open;
    for (const Classroom::Reading &r : std::as_const(m_lesson.readings))
        if (!open.contains(r.article.libraryId))
            open << r.article.libraryId;
    ZimSearch::prepare(open);
    Q_EMIT progress(0, 2, QStringLiteral("Reading the lesson"));
    const quint64 generation = m_generation;
    withCapabilities([this, generation] {
        auto *watcher = new QFutureWatcher<QList<ZimSearch::Article>>(this);
        connect(watcher, &QFutureWatcher<QList<ZimSearch::Article>>::finished, this, [this, watcher, generation] {
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            introSources(watcher->result());
        });
        watcher->setFuture(QtConcurrent::run([readings = m_lesson.readings] {
            QList<ZimSearch::Article> out;
            for (const Classroom::Reading &r : readings) {
                const auto content = ZimReader::content(r.article.libraryId, r.article.path);
                if (!content)
                    continue;
                ZimSearch::Article a;
                a.found.libraryId = r.article.libraryId;
                a.found.path = content->path;
                a.found.title = r.article.title;
                a.passages = Extract::extract(content->data).passages;
                if (!a.passages.isEmpty())
                    out << a;
            }
            return out;
        }));
    });
}

void Teacher::introSources(const QList<ZimSearch::Article> &articles)
{
    if (articles.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("The lesson's readings could not be opened. Is their library still installed?"));
        return;
    }
    const QString query = Classroom::lessonQuery(m_course, m_lesson);
    QList<Rag::Candidate> pool = Rag::pickPassages(query, articles);
    QStringList input{Embedding::formatQuery(m_models.embedding, query)};
    for (const Rag::Candidate &c : std::as_const(pool))
        input << Embedding::formatDocument(m_models.embedding, c.text.left(3000));
    const quint64 generation = m_generation;
    m_reply = m_client->embed(m_models.embedding, input, this, [this, generation, pool](const QList<Vector> &vectors, const QString &error) mutable {
        if (generation != m_generation)
            return;
        m_reply.clear();
        Vector question;
        if (error.isEmpty() && vectors.size() == pool.size() + 1) {
            question = vectors.at(0);
            for (int i = 0; i < pool.size(); ++i)
                pool[i].vector = vectors.at(i + 1);
        }
        m_context = Rag::buildContext(Rag::rank(pool, question));
        Q_EMIT progress(1, 2, QStringLiteral("Writing the introduction"));
        complete(Classroom::introPrompt(m_course, m_profile, m_lesson, m_context.text), 800,
                 [this](const QString &text, const QString &error) {
                     if (!error.isEmpty()) {
                         finish(QStringLiteral("error"), error);
                         return;
                     }
                     introWritten(text);
                 },
                 true);
    });
}

void Teacher::introWritten(const QString &raw)
{
    // the model's title, if it wrote one, goes: the lesson page has its own
    QStringList lines = Rag::tidyAnswer(raw).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && (lines.first().trimmed().isEmpty() || lines.first().trimmed().startsWith(QLatin1String("# "))))
        lines.removeFirst();
    const QString text = Drafts::dropUnfinishedSentence(Drafts::demoteHeadings(lines.join(QLatin1Char('\n')).trimmed()));
    // each source is numbered as the lesson lists its readings
    QList<int> numbers;
    for (const Rag::Source &s : std::as_const(m_context.sources)) {
        int n = 0;
        for (int i = 0; i < m_lesson.readings.size(); ++i)
            if (m_lesson.readings.at(i).article.libraryId == s.libraryId && m_lesson.readings.at(i).article.path == s.path)
                n = i + 1;
        numbers << n;
    }
    auto save = [this, text, numbers](const QList<Rag::Citation> &citations) {
        // only the lesson's readings are cited (they are all it read; a source matching none would be "[0]")
        QList<Rag::Citation> kept = citations;
        for (Rag::Citation &c : kept) {
            c.sources.removeIf([&numbers](int n) {
                return n < 1 || n > numbers.size() || numbers.at(n - 1) < 1;
            });
            // a teacher's welcome and everyday examples are not claims from the reading: only a figure the reading does not
            // have is marked for checking, not a sentence on its own topic
            if (c.reason == QLatin1String("topic"))
                c.reason.clear();
            // with one reading, "[1]" after every sentence says nothing
            if (m_lesson.readings.size() < 2)
                c.sources.clear();
        }
        if (!Classroom::setIntro(m_lesson.id, Drafts::citeSection(text, kept, numbers)))
            finish(QStringLiteral("error"), QStringLiteral("The introduction could not be saved."));
        else
            finish(QStringLiteral("done"));
    };
    const auto spans = Rag::citableSentences(text);
    if (spans.isEmpty()) {
        save({});
        return;
    }
    QStringList input;
    for (const auto &[a, b] : spans)
        input << Embedding::formatDocument(m_models.embedding, text.mid(a, b - a));
    const quint64 generation = m_generation;
    m_reply = m_client->embed(m_models.embedding, input, this, [this, generation, text, spans, save](const QList<Vector> &vectors, const QString &error) {
        if (generation != m_generation)
            return;
        m_reply.clear();
        if (!error.isEmpty() || vectors.size() != spans.size()) {  // citations are a bonus
            save({});
            return;
        }
        save(Rag::attribute(text, spans, vectors, m_context.sources));
    });
}

// ---- a quiz or a test ----

void Teacher::writeQuiz(const QString &kind, qint64 targetId, const Models &models)
{
    stop();
    ++m_generation;
    m_models = models;
    m_running = true;
    m_job = QStringLiteral("quiz");
    m_subjectId = targetId;
    m_quizKind = kind;
    m_quizLessons.clear();
    m_quizRead.clear();
    m_quizQueue.clear();
    m_quizWanted.clear();
    m_questions.clear();
    m_quizRank = 0;
    m_quizTried = 0;
    qint64 courseId = 0;
    if (kind == QLatin1String("unit")) {
        courseId = Classroom::unitCourse(targetId);
        for (const auto syllabus = Classroom::syllabus(courseId); const Classroom::Unit &u : syllabus)
            if (u.id == targetId)
                m_quizLessons = u.lessons;
        for (const Classroom::Lesson &l : std::as_const(m_quizLessons))
            m_quizWanted.insert(l.id, Classroom::TestQuestionsPerLesson);
    } else if (const auto l = Classroom::lesson(targetId)) {
        courseId = l->courseId;
        m_quizLessons = {*l};
        m_quizWanted.insert(l->id, Classroom::QuizQuestions);
    }
    const auto course = Classroom::course(courseId);
    if (!course || m_quizLessons.isEmpty()) {
        finish(QStringLiteral("error"), QStringLiteral("This lesson or unit no longer exists."));
        return;
    }
    m_course = *course;
    QList<qint64> open;
    for (const Classroom::Lesson &l : std::as_const(m_quizLessons))
        for (const Classroom::Reading &r : l.readings)
            if (!open.contains(r.article.libraryId))
                open << r.article.libraryId;
    ZimSearch::prepare(open);
    Q_EMIT progress(0, 1, QStringLiteral("Reading the lessons"));
    const quint64 generation = m_generation;
    withCapabilities([this, generation] {
        using Read = QList<std::pair<qint64, QList<ZimSearch::Article>>>;
        auto *watcher = new QFutureWatcher<Read>(this);
        connect(watcher, &QFutureWatcher<Read>::finished, this, [this, watcher, generation] {
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            quizPassages(watcher->result());
        });
        watcher->setFuture(QtConcurrent::run([lessons = m_quizLessons] {
            Read out;
            for (const Classroom::Lesson &l : lessons) {
                QList<ZimSearch::Article> articles;
                for (const Classroom::Reading &r : l.readings) {
                    const auto content = ZimReader::content(r.article.libraryId, r.article.path);
                    if (!content)
                        continue;
                    ZimSearch::Article a;
                    a.found.libraryId = r.article.libraryId;
                    a.found.path = content->path;
                    a.found.title = r.article.title;
                    a.passages = Extract::extract(content->data).passages;
                    if (!a.passages.isEmpty())
                        articles << a;
                }
                out.append({l.id, articles});
            }
            return out;
        }));
    });
}

void Teacher::quizPassages(const QList<std::pair<qint64, QList<ZimSearch::Article>>> &read)
{
    m_quizRead = read;
    rankQuizPassages();
}

// each lesson's passages, ranked against the lesson (one embedding request per lesson), then the questions written
void Teacher::rankQuizPassages()
{
    if (m_quizRank >= m_quizRead.size()) {
        nextQuestion();
        return;
    }
    const auto &[lessonId, articles] = m_quizRead.at(m_quizRank);
    Classroom::Lesson lesson;
    for (const Classroom::Lesson &l : std::as_const(m_quizLessons))
        if (l.id == lessonId)
            lesson = l;
    // passages with enough in them to ask about; not the infobox's facts ("the official language of Rome", asked in both
    // a quiz and its unit's test: trivia, 2026-10-20)
    QList<Rag::Candidate> pool;
    for (const auto lessonQuery = Rag::pickPassages(Classroom::lessonQuery(m_course, lesson), articles); const Rag::Candidate &c : lessonQuery)
        if (c.text.size() >= 250 && c.section != QLatin1String("Key facts"))
            pool << c;
    QStringList input{Embedding::formatQuery(m_models.embedding, Classroom::lessonMeaning(m_course, lesson))};
    for (const Rag::Candidate &c : std::as_const(pool))
        input << Embedding::formatDocument(m_models.embedding, c.text.left(3000));
    Q_EMIT progress(0, 1, QStringLiteral("Choosing what to ask about “%1”").arg(lesson.title));
    const quint64 generation = m_generation;
    m_reply = m_client->embed(m_models.embedding, input, this, [this, generation, pool, lesson](const QList<Vector> &vectors, const QString &error) mutable {
        if (generation != m_generation)
            return;
        m_reply.clear();
        Vector question;
        if (error.isEmpty() && vectors.size() == pool.size() + 1) {
            question = vectors.at(0);
            for (int i = 0; i < pool.size(); ++i)
                pool[i].vector = vectors.at(i + 1);
        }
        // the best passages, a few more than the questions wanted (some questions are not kept)
        const int wanted = m_quizWanted.value(lesson.id);
        for (const auto rank = Rag::rank(pool, question, wanted + 3); const Rag::Candidate &c : rank)
            m_quizQueue.append({lesson.id, lesson.title, c});
        ++m_quizRank;
        rankQuizPassages();
    });
}

void Teacher::nextQuestion()
{
    // the next passage of a lesson that still wants questions
    int wantedTotal = 0;
    for (int w : std::as_const(m_quizWanted))
        wantedTotal += w;
    qsizetype pick = -1;
    if (wantedTotal > 0) {
        // round the lessons: a unit test asks about each in turn
        QHash<qint64, int> asked;
        for (const Classroom::Question &q : std::as_const(m_questions))
            ++asked[q.lessonId];
        int fewest = INT_MAX;
        for (qsizetype i = 0; i < m_quizQueue.size(); ++i) {
            const qint64 l = m_quizQueue.at(i).lessonId;
            if (m_quizWanted.value(l) > 0 && asked.value(l) < fewest) {
                fewest = asked.value(l);
                pick = i;
            }
        }
    }
    if (pick < 0) {
        if (m_questions.isEmpty()) {
            finish(QStringLiteral("error"), QStringLiteral("The teacher could not write questions from this lesson's readings. Try again."));
            return;
        }
        Classroom::Quiz quiz;
        quiz.courseId = m_course.id;
        quiz.kind = m_quizKind;
        (m_quizKind == QLatin1String("unit") ? quiz.unitId : quiz.lessonId) = m_subjectId;
        quiz.questions = m_questions;
        if (Classroom::saveQuiz(quiz) <= 0)
            finish(QStringLiteral("error"), QStringLiteral("The quiz could not be saved."));
        else
            finish(QStringLiteral("done"));
        return;
    }
    const QuizPassage item = m_quizQueue.takeAt(pick);
    ++m_quizTried;
    const int total = int(m_questions.size()) + wantedTotal;
    Q_EMIT progress(int(m_questions.size()) + 1, total + 1, QStringLiteral("Writing question %1 of %2").arg(m_questions.size() + 1).arg(total));
    complete(Classroom::questionPrompt(m_course, item.lessonTitle, item.passage.text), 400, [this, item](const QString &text, const QString &error) {
        if (!error.isEmpty()) {
            finish(QStringLiteral("error"), error);
            return;
        }
        // kept only when its answer is in the passage it was written from
        if (auto q = Classroom::parseQuestion(Rag::tidyAnswer(text)); q && Classroom::answerInPassage(*q, item.passage.text)) {
            q->evidence = item.passage.text.left(1500);
            q->source = {item.passage.libraryId, item.passage.path, item.passage.title};
            q->lessonId = item.lessonId;
            m_questions << Classroom::shuffled(*q, quint32(qHash(q->text)));
            m_quizWanted[item.lessonId] -= 1;
        }
        // a lesson whose passages ran out wants no more
        for (auto it = m_quizWanted.begin(); it != m_quizWanted.end(); ++it) {
            const bool left = std::any_of(m_quizQueue.cbegin(), m_quizQueue.cend(), [&it](const QuizPassage &p) {
                return p.lessonId == it.key();
            });
            if (!left)
                it.value() = 0;
        }
        nextQuestion();
    });
}

void Teacher::stop()
{
    if (!m_running)
        return;
    ++m_generation;  // late answers are ignored
    OllamaClient::cancel(m_reply);
    finish(QStringLiteral("stopped"));
}

void Teacher::finish(const QString &status, const QString &error)
{
    if (!m_running)
        return;
    m_running = false;
    ++m_generation;  // whatever is still on its way for this job is ignored (as Answerer and Drafter do)
    if (m_job == QLatin1String("plan") && status != QLatin1String("done"))
        Classroom::setStatus(m_subjectId, QStringLiteral("failed"),
                             status == QLatin1String("stopped") ? QStringLiteral("Planning was stopped. Press Plan Again.") : error);
    else if (m_job == QLatin1String("plan"))
        Classroom::setStatus(m_subjectId, QStringLiteral("ready"));
    Q_EMIT finished(status, error);
}
