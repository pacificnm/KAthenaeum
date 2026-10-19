// SPDX-License-Identifier: GPL-3.0-or-later
#include "history.h"

#include "database.h"

#include <QSqlQuery>

void History::record(qint64 libraryId, const QString &path, const QString &title)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO reading_history (library_id, path, title) VALUES (?, ?, ?)"
                             " ON CONFLICT (library_id, path) DO UPDATE SET title = excluded.title,"
                             " read_at = strftime('%Y-%m-%dT%H:%M:%f', 'now')"));
    q.addBindValue(libraryId);
    q.addBindValue(path);
    q.addBindValue(Database::text(title));
    q.exec();
}

QList<ZimEntry> History::recent(qint64 libraryId, int limit)
{
    QList<ZimEntry> out;
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT path, title FROM reading_history WHERE library_id = ? ORDER BY read_at DESC LIMIT ?"));
    q.addBindValue(libraryId);
    q.addBindValue(limit);
    if (q.exec())
        while (q.next())
            out.append({q.value(0).toString(), q.value(1).toString()});
    return out;
}
