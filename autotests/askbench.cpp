// SPDX-License-Identifier: GPL-3.0-or-later
// Measures Ask end to end on real libraries and the local Ollama (not a test; nothing is checked):
//   askbench [--answer MODEL] [--embed MODEL] [--url URL] file.zim... < questions.txt
// One question per line. A line starting with "+" is a follow-up to the previous question. Prints, per question, the
// time of each step, the sources, and the answer with its citations ([n], and [?reason] for a flagged sentence).
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

#include "core/answerer.h"
#include "core/database.h"
#include "core/extract.h"
#include "core/library.h"
#include "core/ollama.h"

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addOption({QStringLiteral("answer"), QStringLiteral("Answer model"), QStringLiteral("model"), QStringLiteral("qwen3.5:2b")});
    p.addOption({QStringLiteral("embed"), QStringLiteral("Embedding model"), QStringLiteral("model"), QStringLiteral("nomic-embed-text")});
    p.addOption({QStringLiteral("url"), QStringLiteral("Ollama"), QStringLiteral("url"), QStringLiteral("http://localhost:11434")});
    p.addPositionalArgument(QStringLiteral("zim"), QStringLiteral("Libraries to ask"));
    p.addHelpOption();
    p.process(app);

    initXmlParser();
    QTemporaryDir dir;
    QString error;
    if (!Database::open(dir.filePath(QStringLiteral("bench.sqlite")), &error))
        qFatal("%s", qPrintable(error));
    QList<qint64> ids;
    for (const QString &zim : p.positionalArguments()) {
        const auto id = Library::add(zim, &error);
        if (!id)
            qFatal("%s: %s", qPrintable(zim), qPrintable(error));
        ids << *id;
    }

    QTextStream out(stdout);
    QStringList questions;
    QTextStream in(stdin);
    while (!in.atEnd())
        if (const QString l = in.readLine().trimmed(); !l.isEmpty() && !l.startsWith(QLatin1Char('#')))
            questions << l;

    OllamaClient client;
    client.setBaseUrl(p.value(QStringLiteral("url")));
    Answerer answerer(&client);
    QStringList earlier;
    int next = 0;
    std::function<void()> askNext = [&] {
        if (next >= questions.size()) {
            app.quit();
            return;
        }
        QString q = questions.at(next++);
        if (q.startsWith(QLatin1Char('+')))
            q = q.mid(1).trimmed();
        else
            earlier.clear();
        out << "\n=== " << q << Qt::endl;
        answerer.start({q, earlier, ids, p.value(QStringLiteral("answer")), p.value(QStringLiteral("embed"))});
        earlier << q;
    };
    QObject::connect(&answerer, &Answerer::sourcesReady, [&](const QList<Rag::Source> &sources, bool degraded, const QString &note) {
        if (degraded)
            out << "  (ranked by words only: " << note << ")" << Qt::endl;
        for (const Rag::Source &s : sources)
            out << "  [" << s.n << "] " << s.title << (s.section.isEmpty() ? QString() : QStringLiteral(" - ") + s.section) << Qt::endl;
    });
    QObject::connect(&answerer, &Answerer::finished, [&](const AnswerResult &r) {
        const AnswerStats &s = r.stats;
        out << "  find " << s.findMs << " ms, embed " << s.embedMs << " ms (" << s.passages << " ranked), first word at " << s.firstTokenMs
            << " ms, cite " << s.citeMs << " ms, total " << s.totalMs << " ms, " << s.tokens << " tokens at " << QString::number(s.tokensPerSecond, 'f', 1)
            << " tok/s" << (s.truncated ? " TRUNCATED" : "") << Qt::endl;
        if (r.status != QLatin1String("done"))
            out << "  " << r.status.toUpper() << ": " << r.error << Qt::endl;
        QString shown = r.answer;
        for (auto it = r.citations.crbegin(); it != r.citations.crend(); ++it) {
            QString chips;
            for (int n : it->sources)
                chips += QStringLiteral("[%1]").arg(n);
            if (!it->reason.isEmpty())
                chips += QStringLiteral("[?%1 %2]").arg(it->reason, it->numbers.join(QLatin1Char(',')));
            chips += QStringLiteral("{%1}").arg(it->score, 0, 'f', 2);
            shown.insert(it->end, chips);
        }
        out << shown << Qt::endl;
        QMetaObject::invokeMethod(&app, askNext, Qt::QueuedConnection);
    });
    QMetaObject::invokeMethod(&app, askNext, Qt::QueuedConnection);
    return app.exec();
}
