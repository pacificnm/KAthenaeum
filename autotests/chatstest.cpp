// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/chats.h"
#include "core/database.h"

class ChatsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static MessageRecord question(qint64 chatId, const QString &text)
    {
        MessageRecord m;
        m.chatId = chatId;
        m.role = QStringLiteral("user");
        m.content = text;
        return m;
    }

private Q_SLOTS:
    void init()
    {
        QString error;
        QVERIFY2(Database::open(m_dir.filePath(QStringLiteral("%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), &error), qPrintable(error));
    }
    void cleanup() { Database::close(); }

    void aChatKeepsItsMessagesInOrder()
    {
        const qint64 id = Chats::create(QStringLiteral("  Why is   water polar?  "));
        QVERIFY(id > 0);
        QVERIFY(Chats::addMessage(question(id, QStringLiteral("Why is water polar?"))));
        MessageRecord answer;
        answer.chatId = id;
        answer.role = QStringLiteral("assistant");
        answer.content = QStringLiteral("Because oxygen pulls electrons.");
        answer.sources = "[{\"n\":1,\"title\":\"Water\"}]";
        answer.citations = "[{\"start\":0,\"end\":31,\"sources\":[1]}]";
        answer.meta = "{\"totalMs\":8400}";
        QVERIFY(Chats::addMessage(answer));
        MessageRecord stopped;
        stopped.chatId = id;
        stopped.role = QStringLiteral("assistant");
        stopped.status = QStringLiteral("stopped");
        stopped.sources.clear();  // empty JSON fields are stored as empty lists, not as nothing
        QVERIFY(Chats::addMessage(stopped));

        const auto ms = Chats::messages(id);
        QCOMPARE(ms.size(), 3);
        QCOMPARE(ms[0].role, QStringLiteral("user"));
        QCOMPARE(ms[1].sources, QByteArray("[{\"n\":1,\"title\":\"Water\"}]"));
        QCOMPARE(ms[1].meta, QByteArray("{\"totalMs\":8400}"));
        QCOMPARE(ms[2].status, QStringLiteral("stopped"));
        QCOMPARE(ms[2].content, QString());
        QCOMPARE(ms[2].sources, QByteArray("[]"));
        QVERIFY(ms[0].createdAt.isValid());

        const auto chat = Chats::byId(id);
        QVERIFY(chat);
        QCOMPARE(chat->title, QStringLiteral("Why is water polar?"));
        QCOMPARE(chat->questions, 1);
    }

    void theMostRecentlyUsedChatComesFirst()
    {
        const qint64 a = Chats::create(QStringLiteral("A"));
        const qint64 b = Chats::create(QStringLiteral("B"));
        QTest::qWait(5);
        QVERIFY(Chats::addMessage(question(a, QStringLiteral("again"))));
        const auto all = Chats::all();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].id, a);
        QCOMPARE(all[1].id, b);
    }

    void removingAChatRemovesItsMessages()
    {
        const qint64 id = Chats::create(QStringLiteral("Gone"));
        QVERIFY(Chats::addMessage(question(id, QStringLiteral("q"))));
        QVERIFY(Chats::remove(id));
        QVERIFY(!Chats::byId(id));
        QSqlQuery q(QStringLiteral("SELECT count(*) FROM messages"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(!Chats::remove(id));
    }

    void aMessageForAChatThatDoesNotExistIsRefused()
    {
        QCOMPARE(Chats::addMessage(question(999, QStringLiteral("q"))), 0);
    }

    void aChatCanBeAboutOneArticle()
    {
        const qint64 general = Chats::create(QStringLiteral("General"));
        const qint64 water = Chats::create(QStringLiteral("Why is it polar?"), 3, QStringLiteral("Water"), QStringLiteral("Water"));
        const qint64 water2 = Chats::create(QStringLiteral("Boiling point?"), 3, QStringLiteral("Water"), QStringLiteral("Water"));
        Chats::create(QStringLiteral("Other library"), 4, QStringLiteral("Water"), QStringLiteral("Water"));
        QTest::qWait(5);
        QVERIFY(Chats::addMessage(question(water, QStringLiteral("again"))));
        const auto about = Chats::aboutArticle(3, QStringLiteral("Water"));
        QCOMPARE(about.size(), 2);
        QCOMPARE(about[0].id, water);
        QCOMPARE(about[1].id, water2);
        QCOMPARE(about[0].articleLibraryId, 3);
        QCOMPARE(about[0].articleTitle, QStringLiteral("Water"));
        const auto g = Chats::byId(general);
        QCOMPARE(g->articleLibraryId, 0);
        QVERIFY(g->articlePath.isEmpty());
    }

    void renaming()
    {
        const qint64 id = Chats::create(QString(500, QLatin1Char('x')));
        QCOMPARE(Chats::byId(id)->title.size(), 200);
        QVERIFY(Chats::rename(id, QStringLiteral("Water")));
        QCOMPARE(Chats::byId(id)->title, QStringLiteral("Water"));
    }
};

QTEST_GUILESS_MAIN(ChatsTest)
#include "chatstest.moc"
