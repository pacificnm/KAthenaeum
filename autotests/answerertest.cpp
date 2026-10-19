// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "core/answerer.h"
#include "core/chats.h"
#include "core/drafts.h"
#include "core/projects.h"
#include "core/database.h"
#include "core/library.h"
#include "core/ollama.h"
#include "core/zimreader.h"

#include <zim/writer/creator.h>
#include <zim/writer/item.h>

// A stand-in for Ollama that answers by path: /api/embed with one vector per input, /api/show with capabilities,
// /api/chat with a streamed answer. Each path can be made to fail (an HTTP status and body) or to hang.
class Fake : public QObject
{
public:
    QTcpServer server;
    QHash<QString, std::pair<int, QByteArray>> failures;  // path -> (status, body)
    QString hangOn;
    QStringList answer{QStringLiteral("Photosynthesis turns sunlight "), QStringLiteral("into sugar in green plants.")};
    QList<QJsonObject> requests;  // bodies, in order, with "path" added
    QStringList paths;

    Fake()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(s, &QTcpSocket::readyRead, this, [this, s, buffer] {
                *buffer += s->readAll();
                const qsizetype end = buffer->indexOf("\r\n\r\n");
                if (end < 0 || s->property("answered").toBool())
                    return;
                const QByteArray head = buffer->left(end);
                const qsizetype at = head.toLower().indexOf("content-length:");
                const int length = at < 0 ? 0 : head.mid(at + 15, head.indexOf("\r\n", at) - at - 15).trimmed().toInt();
                if (buffer->size() < end + 4 + length)
                    return;
                s->setProperty("answered", true);
                const QString path = QString::fromLatin1(head.split(' ').value(1));
                QJsonObject body = QJsonDocument::fromJson(buffer->mid(end + 4, length)).object();
                paths << path;
                requests << body;
                respond(s, path, body);
            });
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

    QJsonObject last(const QString &path) const
    {
        for (qsizetype i = paths.size() - 1; i >= 0; --i)
            if (paths[i] == path)
                return requests[i];
        return {};
    }

private:
    static void send(QTcpSocket *s, int status, const QByteArray &body, const char *type = "application/json")
    {
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + type + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }

    void respond(QTcpSocket *s, const QString &path, const QJsonObject &body)
    {
        if (failures.contains(path)) {
            send(s, failures[path].first, failures[path].second);
            return;
        }
        if (path == QLatin1String("/api/embed")) {
            QJsonArray vectors;
            for (int i = 0; i < body.value(QLatin1String("input")).toArray().size(); ++i)
                vectors.append(QJsonArray{1.0, 0.0, 0.0});  // everything alike: every sentence is backed
            send(s, 200, QJsonDocument(QJsonObject{{QStringLiteral("embeddings"), vectors}}).toJson(QJsonDocument::Compact));
        } else if (path == QLatin1String("/api/show")) {
            send(s, 200, "{\"capabilities\":[\"completion\",\"thinking\"]}");
        } else if (path == QLatin1String("/api/chat")) {
            s->write("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n\r\n");
            if (hangOn == path) {
                s->write("{\"message\":{\"content\":\"Partial \"},\"done\":false}" "\n");
                return;  // and nothing more
            }
            for (const QString &t : std::as_const(answer))
                s->write(QJsonDocument(QJsonObject{{QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), t}}}, {QStringLiteral("done"), false}})
                             .toJson(QJsonDocument::Compact)
                         + "\n");
            s->write("{\"message\":{\"content\":\"\"},\"done\":true,\"eval_count\":9,\"eval_duration\":1000000000,\"done_reason\":\"stop\"}" "\n");
            s->disconnectFromHost();
        } else {
            send(s, 404, "{\"error\":\"unknown\"}");
        }
    }
};

class AnswererTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    qint64 m_id = 0;

    static std::string page(const std::string &title, const std::string &intro, const std::string &section, const std::string &text)
    {
        return "<html><head><title>" + title + "</title></head><body><div class='mw-parser-output'><p>" + intro + "</p><h2>" + section
            + "</h2><p>" + text + "</p></div></body></html>";
    }

    struct Run {
        QList<Rag::Source> sources;
        bool degraded = false;
        QString note;
        QString text;
        QList<Answerer::Phase> phases;
        AnswerResult result;
        bool done = false;
        QList<std::pair<qint64, Vector>> embeddedClips;
        QString clipModel;
    };

    // Asks and waits for finished(); `whileRunning` is called once the answer has started streaming
    Run ask(Fake &fake, const QString &question, const QStringList &earlier = {}, const std::function<void(Answerer &)> &whileRunning = {},
            const ArticleScope &article = {}, const ProjectScope &project = {})
    {
        OllamaClient client;
        client.setBaseUrl(fake.url());
        Answerer answerer(&client);
        Run run;
        connect(&answerer, &Answerer::phaseChanged, this, [&](Answerer::Phase p, int) { run.phases << p; });
        connect(&answerer, &Answerer::sourcesReady, this, [&](const QList<Rag::Source> &s, bool d, const QString &n) {
            run.sources = s;
            run.degraded = d;
            run.note = n;
        });
        connect(&answerer, &Answerer::textAdded, this, [&](const QString &t) {
            run.text += t;
            if (whileRunning)
                QTimer::singleShot(0, &answerer, [&] { whileRunning(answerer); });
        });
        connect(&answerer, &Answerer::clipsEmbedded, this, [&](const QList<std::pair<qint64, Vector>> &v, const QString &model) {
            run.embeddedClips = v;
            run.clipModel = model;
        });
        connect(&answerer, &Answerer::finished, this, [&](const AnswerResult &r) {
            run.result = r;
            run.done = true;
        });
        answerer.start({question, earlier, {m_id}, QStringLiteral("qwen3.5:2b"), QStringLiteral("nomic-embed-text"), article, project});
        [&] { QTRY_VERIFY_WITH_TIMEOUT(run.done, 20000); }();
        return run;
    }

public:
    struct Draft {
        QString status, title, markdown, error;
        QStringList labels;
        QString streamed;
        bool done = false;
    };

    Draft draft(Fake &fake, const std::function<void(Drafter &)> &start, bool stopWhenStreaming = false)
    {
        OllamaClient client;
        client.setBaseUrl(fake.url());
        Drafter drafter(&client);
        Draft d;
        connect(&drafter, &Drafter::progress, this, [&](int, int, const QString &label) { d.labels << label; });
        connect(&drafter, &Drafter::textAdded, this, [&](const QString &t) { d.streamed += t; });
        connect(&drafter, &Drafter::finished, this, [&](const QString &status, const QString &title, const QString &md, const QString &error) {
            d.status = status;
            d.title = title;
            d.markdown = md;
            d.error = error;
            d.done = true;
        });
        start(drafter);
        if (stopWhenStreaming)
            [&] { QTRY_VERIFY_WITH_TIMEOUT(fake.paths.contains(QStringLiteral("/api/chat")), 20000); }();
        if (stopWhenStreaming)
            drafter.stop();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(d.done, 20000); }();
        return d;
    }


private Q_SLOTS:
    void initTestCase()
    {
        initXmlParser();
        const QString file = m_dir.filePath(QStringLiteral("plants.zim"));
        zim::writer::Creator c;
        c.configIndexing(true, "eng");
        c.startZimCreation(file.toStdString());
        c.setMainPath("Photosynthesis");
        for (const auto &[k, v] : std::vector<std::pair<std::string, std::string>>{
                 {"Name", "t"}, {"Title", "Plants"}, {"Creator", "x"}, {"Publisher", "x"}, {"Date", "2026-01-01"}, {"Description", "d"}, {"Language", "eng"}})
            c.addMetadata(k, v);
        const std::vector<std::tuple<std::string, std::string, std::string>> pages = {
            {"Photosynthesis", "Photosynthesis",
             page("Photosynthesis", "Photosynthesis is the process by which green plants turn sunlight, water and carbon dioxide into sugar.",
                  "Chlorophyll", "Chlorophyll is the green pigment in leaves that absorbs sunlight.")},
            {"Leaf", "Leaf", page("Leaf", "A leaf is the main organ of photosynthesis in green plants, flat and thin to catch sunlight.", "Shape", "Leaves vary in shape.")},
            {"Index", "Index", "<html><body>front page</body></html>"},
        };
        for (const auto &[path, title, html] : pages)
            c.addItem(zim::writer::StringItem::create(path, std::string("text/html"), title, zim::writer::Hints{{zim::writer::HintKeys::FRONT_ARTICLE, 1}}, html));
        c.finishZimCreation();

        QVERIFY(Database::open(m_dir.filePath(QStringLiteral("t.sqlite")), nullptr));
        const auto id = Library::add(file);
        QVERIFY(id);
        m_id = *id;
    }

    void aQuestionIsAnsweredFromTheLibraryAndCited()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("how do plants use sunlight"));
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QVERIFY(!run.degraded);
        QVERIFY(!run.sources.isEmpty());
        QCOMPARE(run.sources.first().title, QStringLiteral("Photosynthesis"));
        QCOMPARE(run.result.answer, QStringLiteral("Photosynthesis turns sunlight into sugar in green plants."));
        QCOMPARE(run.result.citations.size(), 1);
        QVERIFY(run.result.citations.first().reason.isEmpty());
        QCOMPARE(run.phases, (QList<Answerer::Phase>{Answerer::Finding, Answerer::Ranking, Answerer::Reading, Answerer::Writing, Answerer::Checking}));
        QCOMPARE(run.result.stats.tokens, 9);
        QCOMPARE(run.result.stats.tokensPerSecond, 9.0);

        // what went to Ollama: the question and passages with nomic's prefixes, thinking off, the measured options
        QCOMPARE(fake.paths, (QStringList{QStringLiteral("/api/embed"), QStringLiteral("/api/show"), QStringLiteral("/api/chat"), QStringLiteral("/api/embed")}));
        const QJsonArray input = fake.requests.first().value(QLatin1String("input")).toArray();
        QVERIFY(input.first().toString().startsWith(QLatin1String("search_query: how do plants use sunlight")));
        QVERIFY(input.last().toString().startsWith(QLatin1String("search_document: ")));
        const QJsonObject chat = fake.last(QStringLiteral("/api/chat"));
        QCOMPARE(chat.value(QLatin1String("think")).toBool(true), false);
        QCOMPARE(chat.value(QLatin1String("options")).toObject().value(QLatin1String("num_ctx")).toInt(), Rag::NumCtx);
        const QJsonArray messages = chat.value(QLatin1String("messages")).toArray();
        QCOMPARE(messages.size(), 2);  // system, and the sources with the question
        QVERIFY(messages.last().toObject().value(QLatin1String("content")).toString().contains(QLatin1String("[1] Photosynthesis")));
    }

    void aFollowUpIsSearchedWithTheQuestionBeforeIt()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("and its pigment?"), {QStringLiteral("What is photosynthesis?")});
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QVERIFY(fake.requests.first().value(QLatin1String("input")).toArray().first().toString().contains(QLatin1String("photosynthesis? and its pigment?")));
        const QString prompt = fake.last(QStringLiteral("/api/chat")).value(QLatin1String("messages")).toArray().last().toObject().value(QLatin1String("content")).toString();
        QVERIFY(prompt.contains(QLatin1String("Earlier in this conversation the user asked: \"What is photosynthesis?\"\nQuestion: and its pigment?")));
    }

    void withoutTheEmbeddingModelPassagesAreRankedByWordsAndNotCited()
    {
        Fake fake;
        fake.failures.insert(QStringLiteral("/api/embed"), {404, "{\"error\":\"model \\\"nomic-embed-text\\\" not found, try pulling it first\"}"});
        const Run run = ask(fake, QStringLiteral("how do plants use sunlight"));
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QVERIFY(run.degraded);
        QVERIFY2(run.note.contains(QLatin1String("ollama pull nomic-embed-text")), qPrintable(run.note));
        QVERIFY(!run.sources.isEmpty());
        QVERIFY(!run.result.answer.isEmpty());
        QVERIFY(run.result.citations.isEmpty());
        QCOMPARE(fake.paths.count(QStringLiteral("/api/embed")), 1);  // no second try for the citations
    }

    void withoutTheAnswerModelTheSourcesAreStillShown()
    {
        Fake fake;
        fake.failures.insert(QStringLiteral("/api/show"), {404, "{\"error\":\"model 'qwen3.5:2b' not found\"}"});
        const Run run = ask(fake, QStringLiteral("how do plants use sunlight"));
        QCOMPARE(run.result.status, QStringLiteral("error"));
        QVERIFY2(run.result.error.contains(QLatin1String("ollama pull qwen3.5:2b")), qPrintable(run.result.error));
        QVERIFY(!run.sources.isEmpty());
        QVERIFY(!fake.paths.contains(QStringLiteral("/api/chat")));
    }

    void stoppingKeepsWhatWasWritten()
    {
        Fake fake;
        fake.hangOn = QStringLiteral("/api/chat");
        const Run run = ask(fake, QStringLiteral("how do plants use sunlight"), {}, [](Answerer &a) { a.stop(); });
        QCOMPARE(run.result.status, QStringLiteral("stopped"));
        QCOMPARE(run.result.answer, QStringLiteral("Partial "));
        QVERIFY(run.result.citations.isEmpty());
    }

    void aQuestionTheLibraryKnowsNothingAboutAsksNoModel()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("zzyzx qwertyuiop"));
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QCOMPARE(run.result.answer, Answerer::noSourcesAnswer());
        QVERIFY(run.sources.isEmpty());
        QVERIFY(fake.paths.isEmpty());
    }

    // ---- a chat about one article ----

    void aChatAboutAnArticleAnswersFromItsSectionsAlone()
    {
        Fake fake;
        // "sunlight" is in both articles; only Photosynthesis may be used
        const Run run = ask(fake, QStringLiteral("what absorbs sunlight"), {}, {}, {m_id, QStringLiteral("Photosynthesis"), QStringLiteral("Photosynthesis")});
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QVERIFY(run.sources.size() >= 2);  // its introduction and its Chlorophyll section, each a source of its own
        QStringList sections;
        for (const Rag::Source &s : run.sources) {
            QCOMPARE(s.path, QStringLiteral("Photosynthesis"));
            sections << s.section;
        }
        QVERIFY(sections.contains(QStringLiteral("Chlorophyll")));
        QVERIFY(sections.contains(QString()));
        const QJsonArray messages = fake.last(QStringLiteral("/api/chat")).value(QLatin1String("messages")).toArray();
        QVERIFY(messages.first().toObject().value(QLatin1String("content")).toString().contains(QLatin1String("reading the article \"Photosynthesis\"")));
        const QString prompt = messages.last().toObject().value(QLatin1String("content")).toString();
        QVERIFY(prompt.contains(QLatin1String("Photosynthesis - Introduction\n")));
        QVERIFY(prompt.contains(QLatin1String("Photosynthesis - Chlorophyll\n")));
        QVERIFY(!prompt.contains(QLatin1String("Leaf")));
        QCOMPARE(run.result.citations.size(), 1);
    }

    void aPageWithoutArticleTextAsksNoModel()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("what is this"), {}, {}, {m_id, QStringLiteral("Index"), QStringLiteral("Index")});
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QCOMPARE(run.result.answer, Answerer::noTextAnswer());
        QVERIFY(fake.paths.isEmpty());
    }

    void anArticleThatIsGoneIsAnError()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("what is this"), {}, {}, {m_id, QStringLiteral("No_such_page"), QStringLiteral("No such page")});
        QCOMPARE(run.result.status, QStringLiteral("error"));
        QVERIFY(run.result.error.contains(QLatin1String("No such page")));
        QVERIFY(fake.paths.isEmpty());
    }

    // ---- a project chat ----

    ProjectScope project(bool alsoLibrary = false)
    {
        ProjectScope p;
        p.id = 7;
        p.title = QStringLiteral("Leaves");
        p.goal = QStringLiteral("How do leaves catch light?");
        p.articles = {{m_id, QStringLiteral("Leaf"), QStringLiteral("Leaf")}};
        Rag::Clip saved;
        saved.id = 42;
        saved.title = QStringLiteral("Photosynthesis");
        saved.text = QStringLiteral("Chlorophyll absorbs mostly red and blue light.");
        saved.libraryId = m_id;
        saved.path = QStringLiteral("Photosynthesis");
        p.clips = {saved};  // no vector yet
        p.alsoLibrary = alsoLibrary;
        return p;
    }

    void aProjectChatAnswersFromItsArticlesAndExcerpts()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("how do leaves catch sunlight"), {}, {}, {}, project());
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QStringList kinds, titles;
        for (const Rag::Source &s : run.sources) {
            kinds << s.kind;
            titles << s.title;
        }
        QCOMPARE(kinds, (QStringList{QStringLiteral("clip"), QString()}));
        QCOMPARE(titles, (QStringList{QStringLiteral("Saved excerpt: Photosynthesis"), QStringLiteral("Leaf")}));  // not the Photosynthesis article
        // the excerpt was embedded with the question, and handed back to be saved
        QCOMPARE(run.embeddedClips.size(), 1);
        QCOMPARE(run.embeddedClips.first().first, 42);
        QCOMPARE(run.clipModel, QStringLiteral("nomic-embed-text"));
        const QJsonArray input = fake.requests.first().value(QLatin1String("input")).toArray();
        QVERIFY(input.last().toString().contains(QLatin1String("Chlorophyll absorbs")));
        const QJsonArray messages = fake.last(QStringLiteral("/api/chat")).value(QLatin1String("messages")).toArray();
        const QString system = messages.first().toObject().value(QLatin1String("content")).toString();
        QVERIFY(system.contains(QLatin1String("research project titled \"Leaves\".\nProject goal: How do leaves catch light?")));
        QVERIFY(!messages.last().toObject().value(QLatin1String("content")).toString().contains(QLatin1String("Other library articles")));
    }

    void aProjectChatCanAlsoSearchTheLibraryAsASeparateGroup()
    {
        Fake fake;
        const Run run = ask(fake, QStringLiteral("how do plants use sunlight"), {}, {}, {}, project(true));
        QCOMPARE(run.result.status, QStringLiteral("done"));
        QStringList outside;
        for (const Rag::Source &s : run.sources)
            if (s.kind == QLatin1String("library"))
                outside << s.path;
        QCOMPARE(outside, QStringList{QStringLiteral("Photosynthesis")});  // Leaf is the project's own, never "outside"
        const QString prompt = fake.last(QStringLiteral("/api/chat")).value(QLatin1String("messages")).toArray().last().toObject()
                                   .value(QLatin1String("content")).toString();
        QVERIFY(prompt.contains(QLatin1String("Other library articles (NOT part of the research project):")));
    }

    void anEmptyProjectAsksNoModel()
    {
        Fake fake;
        ProjectScope empty;
        empty.id = 8;
        empty.title = QStringLiteral("Empty");
        const Run run = ask(fake, QStringLiteral("how do plants use sunlight"), {}, {}, {}, empty);
        QCOMPARE(run.result.answer, Answerer::noProjectSourcesAnswer());
        QVERIFY(fake.paths.isEmpty());
    }

    // ---- AI drafts ----

    void aProjectSummaryReadsEachArticleThenWritesACitedOverview()
    {
        const qint64 p = Projects::create(QStringLiteral("Plants"), QStringLiteral("How do plants use light?"));
        Projects::addArticle(p, {m_id, QStringLiteral("Photosynthesis"), QStringLiteral("Photosynthesis")}, QStringLiteral("start here"));
        Projects::addArticle(p, {m_id, QStringLiteral("Leaf"), QStringLiteral("Leaf")});
        Projects::addClip(p, QStringLiteral("Chlorophyll absorbs sunlight."), {{m_id, QStringLiteral("Photosynthesis"), QStringLiteral("Photosynthesis")}});
        Fake fake;
        const Draft d = draft(fake, [&](Drafter &dr) { dr.summarizeProject(p, {QStringLiteral("qwen3.5:2b"), QStringLiteral("nomic-embed-text")}); });
        QCOMPARE(d.status, QStringLiteral("done"));
        QCOMPARE(d.title, QStringLiteral("Summary: Plants"));
        QCOMPARE(d.labels, (QStringList{QStringLiteral("Reading the articles"), QStringLiteral("Summarizing “Photosynthesis”"),
                                        QStringLiteral("Summarizing “Leaf”"), QStringLiteral("Writing the overview")}));
        // one model call per article, one for the overview, then one embedding call for the overview's numbers
        QCOMPARE(fake.paths, (QStringList{QStringLiteral("/api/show"), QStringLiteral("/api/chat"), QStringLiteral("/api/chat"), QStringLiteral("/api/chat"),
                                          QStringLiteral("/api/embed")}));
        QCOMPARE(fake.last(QStringLiteral("/api/chat")).value(QLatin1String("think")).toBool(true), false);
        const QString overviewPrompt = fake.last(QStringLiteral("/api/chat")).value(QLatin1String("messages")).toArray().first().toObject()
                                           .value(QLatin1String("content")).toString();
        QVERIFY(overviewPrompt.contains(QLatin1String("The research question is: How do plants use light?")));
        QVERIFY(overviewPrompt.contains(QLatin1String("Saved excerpt (Photosynthesis): Chlorophyll absorbs sunlight.")));
        QVERIFY2(d.markdown.contains(QLatin1String("## Overview\n\nPhotosynthesis turns sunlight into sugar in green plants. [1]")), qPrintable(d.markdown));
        QVERIFY(d.markdown.contains(QLatin1String("### 1. [Photosynthesis](zim://lib")));
        QVERIFY(d.markdown.contains(QLatin1String("> **Your note:** start here")));
        QVERIFY(d.markdown.contains(QLatin1String("## References")));
    }

    void chatNotesStreamAndListTheSourcesDiscussed()
    {
        const qint64 chat = Chats::create(QStringLiteral("Light"));
        MessageRecord q;
        q.chatId = chat;
        q.role = QStringLiteral("user");
        q.content = QStringLiteral("What absorbs light?");
        Chats::addMessage(q);
        MessageRecord a;
        a.chatId = chat;
        a.role = QStringLiteral("assistant");
        a.content = QStringLiteral("Chlorophyll [1].");
        a.sources = QStringLiteral("[{\"n\":1,\"libraryId\":%1,\"path\":\"Photosynthesis\",\"title\":\"Photosynthesis\"},"
                                   "{\"n\":2,\"kind\":\"clip\",\"libraryId\":%1,\"path\":\"Leaf\",\"title\":\"Saved excerpt\"}]").arg(m_id).toUtf8();
        Chats::addMessage(a);
        Fake fake;
        const Draft d = draft(fake, [&](Drafter &dr) { dr.summarizeChat(chat, {QStringLiteral("qwen3.5:2b"), QStringLiteral("nomic-embed-text")}); });
        QCOMPARE(d.status, QStringLiteral("done"));
        QCOMPARE(d.title, QStringLiteral("Chat summary: Light"));
        QCOMPARE(d.streamed, QStringLiteral("Photosynthesis turns sunlight into sugar in green plants."));
        QCOMPARE(d.markdown, QStringLiteral("Photosynthesis turns sunlight into sugar in green plants.\n\n## Sources discussed\n\n"
                                            "- [Photosynthesis](zim://lib%1/Photosynthesis)\n").arg(m_id));  // the excerpt is not listed
        const QString prompt = fake.last(QStringLiteral("/api/chat")).value(QLatin1String("messages")).toArray().first().toObject()
                                   .value(QLatin1String("content")).toString();
        QVERIFY(prompt.contains(QLatin1String("Q: What absorbs light?\nA: Chlorophyll.")));
    }

    void aStoppedDraftKeepsNothing()
    {
        const qint64 p = Projects::create(QStringLiteral("Stop"));
        Projects::addArticle(p, {m_id, QStringLiteral("Leaf"), QStringLiteral("Leaf")});
        Fake fake;
        fake.hangOn = QStringLiteral("/api/chat");
        const Draft d = draft(fake, [&](Drafter &dr) { dr.summarizeProject(p, {QStringLiteral("qwen3.5:2b"), QStringLiteral("nomic-embed-text")}); }, true);
        QCOMPARE(d.status, QStringLiteral("stopped"));
        QVERIFY(d.markdown.isEmpty());
    }

    void anEmptyProjectHasNothingToSummarize()
    {
        const qint64 p = Projects::create(QStringLiteral("Empty"));
        Fake fake;
        const Draft d = draft(fake, [&](Drafter &dr) { dr.summarizeProject(p, {QStringLiteral("qwen3.5:2b"), QStringLiteral("nomic-embed-text")}); });
        QCOMPARE(d.status, QStringLiteral("error"));
        QVERIFY(d.error.contains(QLatin1String("Add some articles")));
        QVERIFY(fake.paths.isEmpty());
    }

    void cleanupTestCase()
    {
        ZimReader::close(m_id);
        Database::close();
    }
};

QTEST_GUILESS_MAIN(AnswererTest)
#include "answerertest.moc"
