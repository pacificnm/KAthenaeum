// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "chats.h"

#include "database.h"

#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

namespace
{
QDateTime timestamp(const QVariant &v)
{
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    t.setTimeZone(QTimeZone::UTC);
    return t;
}

ChatRecord chat(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toString(), timestamp(q.value(2)), q.value(3).toInt(),
            q.value(4).toLongLong(), q.value(5).toString(), q.value(6).toString(), q.value(7).toLongLong(), q.value(8).toLongLong()};
}

const char *ChatSelect = "SELECT c.id, c.title, c.updated_at, (SELECT count(*) FROM messages m WHERE m.chat_id = c.id AND m.role = 'user'), "
                         "coalesce(c.library_id, 0), c.article_path, c.article_title, coalesce(c.project_id, 0), coalesce(c.lesson_id, 0) FROM chats c";
}

qint64 Chats::create(const QString &title, qint64 articleLibraryId, const QString &articlePath, const QString &articleTitle, qint64 projectId,
                     qint64 lessonId)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO chats (title, library_id, article_path, article_title, project_id, lesson_id) VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(Database::text(title.simplified().left(200)));
    q.addBindValue(articleLibraryId > 0 ? QVariant(articleLibraryId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(Database::text(articleLibraryId > 0 ? articlePath : QString()));
    q.addBindValue(Database::text(articleLibraryId > 0 ? articleTitle : QString()));
    q.addBindValue(projectId > 0 ? QVariant(projectId) : QVariant(QMetaType::fromType<qint64>()));
    q.addBindValue(lessonId > 0 ? QVariant(lessonId) : QVariant(QMetaType::fromType<qint64>()));
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

QList<ChatRecord> Chats::aboutLesson(qint64 lessonId)
{
    QList<ChatRecord> out;
    QSqlQuery q;
    q.prepare(QLatin1String(ChatSelect) + QStringLiteral(" WHERE c.lesson_id = ? ORDER BY c.updated_at DESC, c.id DESC"));
    q.addBindValue(lessonId);
    if (q.exec())
        while (q.next())
            out.append(chat(q));
    return out;
}

QList<ChatRecord> Chats::all()
{
    QList<ChatRecord> out;
    QSqlQuery q;
    if (q.exec(QLatin1String(ChatSelect) + QStringLiteral(" ORDER BY c.updated_at DESC, c.id DESC")))
        while (q.next())
            out.append(chat(q));
    return out;
}

QList<ChatRecord> Chats::aboutArticle(qint64 libraryId, const QString &path)
{
    QList<ChatRecord> out;
    QSqlQuery q;
    q.prepare(QLatin1String(ChatSelect) + QStringLiteral(" WHERE c.library_id = ? AND c.article_path = ? ORDER BY c.updated_at DESC, c.id DESC"));
    q.addBindValue(libraryId);
    q.addBindValue(path);
    if (q.exec())
        while (q.next())
            out.append(chat(q));
    return out;
}

QList<ChatRecord> Chats::inProject(qint64 projectId)
{
    QList<ChatRecord> out;
    QSqlQuery q;
    q.prepare(QLatin1String(ChatSelect) + QStringLiteral(" WHERE c.project_id = ? ORDER BY c.updated_at DESC, c.id DESC"));
    q.addBindValue(projectId);
    if (q.exec())
        while (q.next())
            out.append(chat(q));
    return out;
}

std::optional<ChatRecord> Chats::byId(qint64 id)
{
    QSqlQuery q;
    q.prepare(QLatin1String(ChatSelect) + QStringLiteral(" WHERE c.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return chat(q);
    return std::nullopt;
}

bool Chats::rename(qint64 id, const QString &title)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE chats SET title = ? WHERE id = ?"));
    q.addBindValue(Database::text(title.simplified().left(200)));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

bool Chats::remove(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM chats WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

qint64 Chats::addMessage(const MessageRecord &m)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO messages (chat_id, role, content, status, sources, citations, meta) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(m.chatId);
    q.addBindValue(Database::text(m.role));
    q.addBindValue(Database::text(m.content));
    q.addBindValue(Database::text(m.status));
    q.addBindValue(QString::fromUtf8(m.sources.isEmpty() ? QByteArray("[]") : m.sources));
    q.addBindValue(QString::fromUtf8(m.citations.isEmpty() ? QByteArray("[]") : m.citations));
    q.addBindValue(QString::fromUtf8(m.meta.isEmpty() ? QByteArray("{}") : m.meta));
    if (!q.exec())
        return 0;
    const qint64 id = q.lastInsertId().toLongLong();
    QSqlQuery touch;
    touch.prepare(QStringLiteral("UPDATE chats SET updated_at = strftime('%Y-%m-%dT%H:%M:%f', 'now') WHERE id = ?"));
    touch.addBindValue(m.chatId);
    touch.exec();
    return id;
}

QList<MessageRecord> Chats::messages(qint64 chatId)
{
    QList<MessageRecord> out;
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT id, chat_id, role, content, status, sources, citations, meta, created_at FROM messages WHERE chat_id = ? ORDER BY id"));
    q.addBindValue(chatId);
    if (!q.exec())
        return out;
    while (q.next()) {
        MessageRecord m;
        m.id = q.value(0).toLongLong();
        m.chatId = q.value(1).toLongLong();
        m.role = q.value(2).toString();
        m.content = q.value(3).toString();
        m.status = q.value(4).toString();
        m.sources = q.value(5).toString().toUtf8();
        m.citations = q.value(6).toString().toUtf8();
        m.meta = q.value(7).toString().toUtf8();
        m.createdAt = timestamp(q.value(8));
        out.append(m);
    }
    return out;
}
