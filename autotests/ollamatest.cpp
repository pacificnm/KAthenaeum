// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include "core/embedding.h"
#include "core/ollama.h"

// A stand-in for Ollama on a local port: each test says what the next request gets back, in pieces with pauses between
// them, so a streamed line can arrive split in two.
class FakeOllama : public QObject
{
public:
    QTcpServer server;
    QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n\r\n";
    QList<QByteArray> pieces;
    QByteArray lastRequest;
    bool hang = false;  // send the head, then nothing

    FakeOllama()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = server.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, this, [this, s] {
                lastRequest += s->readAll();
                const qsizetype end = lastRequest.indexOf("\r\n\r\n");
                if (end < 0 || s->property("answered").toBool())
                    return;
                const QByteArray headers = lastRequest.left(end).toLower();
                const qsizetype at = headers.indexOf("content-length:");
                const int length = at < 0 ? 0 : headers.mid(at + 15, headers.indexOf("\r\n", at) - at - 15).trimmed().toInt();
                if (lastRequest.size() < end + 4 + length)
                    return;
                s->setProperty("answered", true);
                s->write(head);
                if (hang)
                    return;
                int delay = 0;
                for (const QByteArray &p : std::as_const(pieces)) {
                    delay += 20;
                    QTimer::singleShot(delay, s, [s, p] { s->write(p); s->flush(); });
                }
                QTimer::singleShot(delay + 20, s, [s] { s->disconnectFromHost(); });
            });
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    QJsonObject requestBody() const { return QJsonDocument::fromJson(lastRequest.mid(lastRequest.indexOf("\r\n\r\n") + 4)).object(); }
};

class OllamaTest : public QObject
{
    Q_OBJECT

    static QByteArray line(const QString &text, bool done = false)
    {
        QJsonObject o{{QStringLiteral("message"), QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), text}}},
                      {QStringLiteral("done"), done}};
        if (done) {
            o.insert(QStringLiteral("eval_count"), 3);
            o.insert(QStringLiteral("done_reason"), QStringLiteral("stop"));
        }
        return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    }

private Q_SLOTS:
    // ---- parsing ----

    void chatLinesAreParsed()
    {
        auto l = Ollama::parseChatLine("{\"model\":\"m\",\"message\":{\"role\":\"assistant\",\"content\":\"Hi\"},\"done\":false}");
        QCOMPARE(l.text, QStringLiteral("Hi"));
        QVERIFY(!l.done);
        QVERIFY(l.error.isEmpty());
        l = Ollama::parseChatLine("{\"message\":{\"content\":\"\"},\"done\":true,\"eval_count\":12,\"done_reason\":\"length\"}");
        QVERIFY(l.done);
        QCOMPARE(l.json.value(QLatin1String("eval_count")).toInt(), 12);
        QCOMPARE(Ollama::parseChatLine("{\"error\":\"model ran out of memory\"}").error, QStringLiteral("model ran out of memory"));
        QVERIFY(!Ollama::parseChatLine("not json").error.isEmpty());
    }

    void embeddingsAreParsed()
    {
        QString error;
        const auto v = Ollama::parseEmbeddings("{\"model\":\"m\",\"embeddings\":[[0.5,-1],[2,3]]}", &error);
        QVERIFY(v);
        QCOMPARE(v->size(), 2);
        QCOMPARE(v->at(1), (Vector{2.0f, 3.0f}));
        QVERIFY(!Ollama::parseEmbeddings("{\"error\":\"boom\"}", &error));
        QCOMPARE(error, QStringLiteral("boom"));
    }

    void errorsSayWhatToDo()
    {
        const QString base = QStringLiteral("http://localhost:11434");
        QVERIFY(Ollama::errorMessage(QNetworkReply::ConnectionRefusedError, 0, {}, base).contains(QLatin1String("Is it running?")));
        QCOMPARE(Ollama::errorMessage(QNetworkReply::ContentNotFoundError, 404, "{\"error\":\"model 'qwen9' not found\"}", base, QStringLiteral("qwen9")),
                 QStringLiteral("The model “qwen9” is not installed in Ollama. Install it with: ollama pull qwen9"));
        QCOMPARE(Ollama::errorMessage(QNetworkReply::InternalServerError, 500, "{\"error\":\"out of memory\"}", base), QStringLiteral("Ollama: out of memory"));
    }

    // ---- against a fake server ----

    void aStreamedAnswerArrivesInPiecesEvenWhenALineIsSplit()
    {
        FakeOllama fake;
        const QByteArray second = line(QStringLiteral(" world"));
        fake.pieces = {line(QStringLiteral("Hello")), second.left(10), second.mid(10), line(QString(), true)};
        OllamaClient client;
        client.setBaseUrl(fake.url() + QStringLiteral("/"));
        QStringList texts;
        QJsonObject last;
        QString error = QStringLiteral("unset");
        client.chat({{QStringLiteral("model"), QStringLiteral("m")}}, this, [&](const QString &t) { texts << t; },
                    [&](const QJsonObject &l, const QString &e) { last = l; error = e; });
        QTRY_VERIFY(error != QLatin1String("unset"));
        QCOMPARE(error, QString());
        QCOMPARE(texts, (QStringList{QStringLiteral("Hello"), QStringLiteral(" world")}));
        QCOMPARE(last.value(QLatin1String("eval_count")).toInt(), 3);
        QCOMPARE(fake.requestBody().value(QLatin1String("stream")).toBool(), true);
        QVERIFY(fake.lastRequest.startsWith("POST /api/chat "));
    }

    void anErrorInTheMiddleOfAnAnswerIsReported()
    {
        FakeOllama fake;
        fake.pieces = {line(QStringLiteral("Partial")), "{\"error\":\"llama runner process has terminated\"}" "\n"};
        OllamaClient client;
        client.setBaseUrl(fake.url());
        QString error;
        bool finished = false;
        client.chat({{QStringLiteral("model"), QStringLiteral("m")}}, this, [](const QString &) {}, [&](const QJsonObject &, const QString &e) {
            error = e;
            finished = true;
        });
        QTRY_VERIFY(finished);
        QCOMPARE(error, QStringLiteral("Ollama: llama runner process has terminated"));
    }

    void aMissingModelIsReportedWithTheCommandToInstallIt()
    {
        FakeOllama fake;
        fake.head = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n";
        fake.pieces = {"{\"error\":\"model \\\"qwen9\\\" not found, try pulling it first\"}"};
        OllamaClient client;
        client.setBaseUrl(fake.url());
        QString error;
        client.embed(QStringLiteral("qwen9"), {QStringLiteral("x")}, this, [&](const QList<Vector> &, const QString &e) { error = e; });
        QTRY_VERIFY(!error.isEmpty());
        QVERIFY2(error.contains(QLatin1String("ollama pull qwen9")), qPrintable(error));
    }

    void embeddingsComeBackOnePerText()
    {
        FakeOllama fake;
        fake.head = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n";
        fake.pieces = {"{\"embeddings\":[[1,0],[0,1]]}"};
        OllamaClient client;
        client.setBaseUrl(fake.url());
        QList<Vector> got;
        QString error = QStringLiteral("unset");
        client.embed(QStringLiteral("nomic-embed-text"), {QStringLiteral("a"), QStringLiteral("b")}, this, [&](const QList<Vector> &v, const QString &e) {
            got = v;
            error = e;
        });
        QTRY_VERIFY(error != QLatin1String("unset"));
        QCOMPARE(error, QString());
        QCOMPARE(got.size(), 2);
        QCOMPARE(fake.requestBody().value(QLatin1String("input")).toArray().size(), 2);

        // a count that does not match the input is an error, not a guess
        fake.pieces = {"{\"embeddings\":[[1,0]]}"};
        fake.lastRequest.clear();
        error = QStringLiteral("unset");
        client.embed(QStringLiteral("m"), {QStringLiteral("a"), QStringLiteral("b")}, this, [&](const QList<Vector> &, const QString &e) { error = e; });
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(!error.isEmpty());
    }

    void aCancelledRequestCallsNothing()
    {
        FakeOllama fake;
        fake.hang = true;
        OllamaClient client;
        client.setBaseUrl(fake.url());
        bool called = false;
        QNetworkReply *reply = client.chat({{QStringLiteral("model"), QStringLiteral("m")}}, this, [&](const QString &) { called = true; },
                                           [&](const QJsonObject &, const QString &) { called = true; });
        QTRY_VERIFY(!fake.lastRequest.isEmpty());
        OllamaClient::cancel(reply);
        QTest::qWait(100);
        QVERIFY(!called);
    }

    void nothingListeningMeansOllamaIsNotRunning()
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost);
        const quint16 port = probe.serverPort();
        probe.close();  // a port nobody is listening on
        OllamaClient client;
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
        QString error;
        client.installed(this, [&](const QStringList &, const QString &e) { error = e; });
        QTRY_VERIFY(!error.isEmpty());
        QVERIFY2(error.contains(QLatin1String("Is it running?")), qPrintable(error));
    }

    void capabilitiesAreAskedForOnce()
    {
        FakeOllama fake;
        fake.head = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n";
        fake.pieces = {"{\"capabilities\":[\"completion\",\"thinking\"]}"};
        OllamaClient client;
        client.setBaseUrl(fake.url());
        QStringList caps;
        client.capabilities(QStringLiteral("qwen3.5:2b"), this, [&](const QStringList &c, const QString &) { caps = c; });
        QTRY_VERIFY(!caps.isEmpty());
        QVERIFY(caps.contains(QStringLiteral("thinking")));
        fake.server.close();  // the second answer must come from the cache
        caps.clear();
        client.capabilities(QStringLiteral("qwen3.5:2b"), this, [&](const QStringList &c, const QString &) { caps = c; });
        QVERIFY(caps.contains(QStringLiteral("thinking")));
    }

    // ---- the real thing (KATHENAEUM_OLLAMA_TESTS=1, with Ollama running and nomic-embed-text installed) ----

    void realOllamaEmbedsWithTheMeasuredThresholds()
    {
        if (qEnvironmentVariable("KATHENAEUM_OLLAMA_TESTS") != QLatin1String("1"))
            QSKIP("Set KATHENAEUM_OLLAMA_TESTS=1 to run against the local Ollama");
        OllamaClient client;
        const QString model = QStringLiteral("nomic-embed-text");
        QList<Vector> v;
        QString error = QStringLiteral("unset");
        client.embed(model,
                     {Embedding::formatQuery(model, QStringLiteral("why is water a polar molecule")),
                      Embedding::formatDocument(model, QStringLiteral("Water is a polar molecule: the oxygen atom pulls the shared electrons "
                                                                      "towards itself, leaving the hydrogen atoms slightly positive.")),
                      Embedding::formatDocument(model, QStringLiteral("Ray Charles was an American singer and pianist who helped shape soul music."))},
                     this, [&](const QList<Vector> &r, const QString &e) {
                         v = r;
                         error = e;
                     });
        QTRY_VERIFY_WITH_TIMEOUT(error != QLatin1String("unset"), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(v.first().size(), 768);
        QVERIFY(Embedding::cosine(v[0], v[1]) > Embedding::cosine(v[0], v[2]));
    }
};

QTEST_GUILESS_MAIN(OllamaTest)
#include "ollamatest.moc"
