// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>

namespace
{
// Migration N brings the schema from version N to N+1. Never edit one that has shipped: append a new one.
// QSQLITE runs one statement per exec(), so each migration is a list of statements.
const QList<QStringList> migrations = {
    {
        QStringLiteral("CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE libraries ("
                       " id INTEGER PRIMARY KEY,"
                       " path TEXT NOT NULL UNIQUE,"
                       " name TEXT NOT NULL DEFAULT '',"
                       " title TEXT NOT NULL DEFAULT '',"
                       " added_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"),
    },
    {
        // What the library manager shows, read from the ZIM once when it is added
        QStringLiteral("ALTER TABLE libraries ADD COLUMN description TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN language TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN creator TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN publisher TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN zim_date TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN flavour TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN scraper TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN uuid TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN article_count INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN size_bytes INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN has_fulltext INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE libraries ADD COLUMN illustration BLOB"),
        // status: queued | downloading | verifying | complete | failed | cancelled
        QStringLiteral("CREATE TABLE downloads ("
                       " id INTEGER PRIMARY KEY,"
                       " url TEXT NOT NULL,"
                       " filename TEXT NOT NULL,"
                       " folder TEXT NOT NULL,"
                       " title TEXT NOT NULL DEFAULT '',"
                       " total_bytes INTEGER NOT NULL DEFAULT 0,"
                       " done_bytes INTEGER NOT NULL DEFAULT 0,"
                       " status TEXT NOT NULL DEFAULT 'queued',"
                       " error TEXT NOT NULL DEFAULT '',"
                       " started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                       " finished_at TEXT)"),
        // one active download per file, even if two clicks race
        QStringLiteral("CREATE UNIQUE INDEX downloads_active ON downloads (filename) WHERE status IN ('queued', 'downloading', 'verifying')"),
    },
    {
        // "Recently read" on a library's home; goes with its library
        QStringLiteral("CREATE TABLE reading_history ("
                       " library_id INTEGER NOT NULL REFERENCES libraries (id) ON DELETE CASCADE,"
                       " path TEXT NOT NULL,"
                       " title TEXT NOT NULL,"
                       " read_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " PRIMARY KEY (library_id, path))"),
        QStringLiteral("CREATE INDEX reading_history_recent ON reading_history (library_id, read_at DESC)"),
    },
    {
        // Ask: conversations and their messages. An assistant message keeps the sources it was shown and the citations
        // computed for it (JSON), so a saved chat reads exactly as it did. status: done | stopped | error
        QStringLiteral("CREATE TABLE chats ("
                       " id INTEGER PRIMARY KEY,"
                       " title TEXT NOT NULL DEFAULT '',"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE INDEX chats_recent ON chats (updated_at DESC)"),
        QStringLiteral("CREATE TABLE messages ("
                       " id INTEGER PRIMARY KEY,"
                       " chat_id INTEGER NOT NULL REFERENCES chats (id) ON DELETE CASCADE,"
                       " role TEXT NOT NULL,"
                       " content TEXT NOT NULL DEFAULT '',"
                       " status TEXT NOT NULL DEFAULT 'done',"
                       " sources TEXT NOT NULL DEFAULT '[]',"
                       " citations TEXT NOT NULL DEFAULT '[]',"
                       " meta TEXT NOT NULL DEFAULT '{}',"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE INDEX messages_chat ON messages (chat_id, id)"),
    },
};

bool exec(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery q(db);
    if (q.exec(sql))
        return true;
    if (error)
        *error = QStringLiteral("%1 (%2)").arg(q.lastError().text(), sql.left(80));
    return false;
}
}

QString Database::defaultPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("kathenaeum.sqlite"));
}

int Database::latestVersion()
{
    return int(migrations.size());
}

int Database::schemaVersion()
{
    QSqlQuery q(QSqlDatabase::database());
    return q.exec(QStringLiteral("PRAGMA user_version")) && q.next() ? q.value(0).toInt() : -1;
}

bool Database::open(const QString &path, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create the folder for %1").arg(path);
        return false;
    }
    QSqlDatabase db = QSqlDatabase::contains() ? QSqlDatabase::database(QLatin1String(QSqlDatabase::defaultConnection), false)
                                               : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(path);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    if (!exec(db, QStringLiteral("PRAGMA foreign_keys = ON"), error) || !exec(db, QStringLiteral("PRAGMA journal_mode = WAL"), error))
        return false;

    const int current = schemaVersion();
    if (current > latestVersion()) {
        if (error)
            *error = QStringLiteral("The database was made by a newer KAthenaeum (schema %1, this build knows %2)").arg(current).arg(latestVersion());
        return false;
    }
    for (int v = current; v < latestVersion(); ++v) {
        db.transaction();
        bool ok = true;
        for (const QString &sql : migrations[v])
            ok = ok && exec(db, sql, error);
        ok = ok && exec(db, QStringLiteral("PRAGMA user_version = %1").arg(v + 1), error);
        if (!ok) {
            db.rollback();
            return false;
        }
        db.commit();
    }
    return true;
}

void Database::close()
{
    if (!QSqlDatabase::contains())
        return;
    QSqlDatabase::database(QLatin1String(QSqlDatabase::defaultConnection), false).close();
    QSqlDatabase::removeDatabase(QLatin1String(QSqlDatabase::defaultConnection));
}
