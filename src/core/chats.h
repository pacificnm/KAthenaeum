// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

struct ChatRecord {
    qint64 id = 0;
    QString title;  // the first question
    QDateTime updatedAt;
    int questions = 0;
};

struct MessageRecord {
    qint64 id = 0;
    qint64 chatId = 0;
    QString role;  // user | assistant
    QString content;
    QString status = QStringLiteral("done");  // done | stopped | error
    QByteArray sources = "[]";                 // JSON, as the answer view shows them
    QByteArray citations = "[]";
    QByteArray meta = "{}";                    // timings, the models used, notes such as "ranked by words only"
    QDateTime createdAt;
};

// Saved conversations (tables `chats` and `messages`, schema v4)
namespace Chats
{
// The new chat's id, or 0 on failure. The title is cut to 200 characters.
qint64 create(const QString &title);
QList<ChatRecord> all();  // most recently used first
std::optional<ChatRecord> byId(qint64 id);
bool rename(qint64 id, const QString &title);
bool remove(qint64 id);  // and its messages

// Adds a message and marks the chat as used now; returns its id, or 0 on failure
qint64 addMessage(const MessageRecord &message);
QList<MessageRecord> messages(qint64 chatId);
}
