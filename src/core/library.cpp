// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"

#include "database.h"
#include "projects.h"
#include "zimarchive.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

namespace
{
constexpr QLatin1StringView Columns(
    "id, path, name, title, description, language, creator, publisher, zim_date, flavour, scraper, uuid, "
    "article_count, size_bytes, has_fulltext, added_at");

LibraryRecord fromQuery(const QSqlQuery &q)
{
    LibraryRecord r;
    r.id = q.value(0).toLongLong();
    r.path = q.value(1).toString();
    r.name = q.value(2).toString();
    r.title = q.value(3).toString();
    r.description = q.value(4).toString();
    r.language = q.value(5).toString();
    r.creator = q.value(6).toString();
    r.publisher = q.value(7).toString();
    r.date = q.value(8).toString();
    r.flavour = q.value(9).toString();
    r.scraper = q.value(10).toString();
    r.uuid = q.value(11).toString();
    r.articleCount = q.value(12).toLongLong();
    r.sizeBytes = q.value(13).toLongLong();
    r.hasFulltextIndex = q.value(14).toBool();
    r.addedAt = QDateTime::fromString(q.value(15).toString(), Qt::ISODate);
    r.addedAt.setTimeZone(QTimeZone::UTC); // CURRENT_TIMESTAMP is UTC
    return r;
}
}

QString LibraryRecord::filename() const
{
    return QFileInfo(path).fileName();
}

bool LibraryRecord::fileExists() const
{
    return QFileInfo(path).isFile();
}

QList<LibraryRecord> Library::all()
{
    QList<LibraryRecord> out;
    QSqlQuery q(QStringLiteral("SELECT %1 FROM libraries ORDER BY lower(title), path").arg(Columns));
    while (q.next())
        out.append(fromQuery(q));
    return out;
}

std::optional<LibraryRecord> Library::byId(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM libraries WHERE id = ?").arg(Columns));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return fromQuery(q);
    return std::nullopt;
}

QByteArray Library::illustration(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT illustration FROM libraries WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.next() ? q.value(0).toByteArray() : QByteArray();
}

std::optional<qint64> Library::add(const QString &rawPath, QString *error)
{
    const QString path = QFileInfo(rawPath).canonicalFilePath();
    if (path.isEmpty()) {
        if (error)
            *error = i18n("the file does not exist");
        return std::nullopt;
    }
    const auto info = ZimArchive::readInfo(path, error);
    if (!info)
        return std::nullopt;

    // The id: the one it already has, the one it had before it was removed, or one never used (see removed_libraries)
    qint64 id = 0;
    {
        QSqlQuery existing;
        existing.prepare(QStringLiteral("SELECT id FROM libraries WHERE path = ?"));
        existing.addBindValue(path);
        if (existing.exec() && existing.next())
            id = existing.value(0).toLongLong();
    }
    if (id == 0) {
        QSqlQuery before;
        before.prepare(
            QStringLiteral("SELECT id FROM removed_libraries WHERE ((uuid <> '' AND uuid = ?) OR path = ?) "
                           "AND id NOT IN (SELECT id FROM libraries) ORDER BY (uuid = ?) DESC, removed_at DESC LIMIT 1"));
        before.addBindValue(Database::text(info->uuid));
        before.addBindValue(path);
        before.addBindValue(Database::text(info->uuid));
        if (before.exec() && before.next())
            id = before.value(0).toLongLong();
    }
    if (id == 0) {
        QSqlQuery next(QStringLiteral("SELECT max(coalesce((SELECT max(id) FROM libraries), 0), coalesce((SELECT max(id) FROM removed_libraries), 0)) + 1"));
        id = next.next() ? next.value(0).toLongLong() : 0;
    }
    if (id == 0) {
        if (error)
            *error = i18n("could not choose an id");
        return std::nullopt;
    }

    QSqlQuery q;
    q.prepare(
        QStringLiteral("INSERT INTO libraries (id, path, name, title, description, language, creator, publisher, zim_date, flavour, scraper, uuid,"
                       " article_count, size_bytes, has_fulltext, illustration) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                       " ON CONFLICT (path) DO UPDATE SET name = excluded.name, title = excluded.title, description = excluded.description,"
                       " language = excluded.language, creator = excluded.creator, publisher = excluded.publisher, zim_date = excluded.zim_date,"
                       " flavour = excluded.flavour, scraper = excluded.scraper, uuid = excluded.uuid, article_count = excluded.article_count,"
                       " size_bytes = excluded.size_bytes, has_fulltext = excluded.has_fulltext, illustration = excluded.illustration"
                       " RETURNING id"));
    q.addBindValue(id);
    q.addBindValue(path);
    q.addBindValue(Database::text(info->name));
    q.addBindValue(info->title.isEmpty() ? QFileInfo(path).completeBaseName() : info->title);
    q.addBindValue(Database::text(info->description));
    q.addBindValue(Database::text(info->language));
    q.addBindValue(Database::text(info->creator));
    q.addBindValue(Database::text(info->publisher));
    q.addBindValue(Database::text(info->date));
    q.addBindValue(Database::text(info->flavour));
    q.addBindValue(Database::text(info->scraper));
    q.addBindValue(Database::text(info->uuid));
    q.addBindValue(qint64(info->articleCount));
    q.addBindValue(QFileInfo(path).size());
    q.addBindValue(info->hasFulltextIndex);
    q.addBindValue(info->illustration.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray)) : QVariant(info->illustration));
    if (!q.exec() || !q.next()) {
        if (error)
            *error = q.lastError().text();
        return std::nullopt;
    }
    const qint64 added = q.value(0).toLongLong();
    q.finish();
    QSqlQuery back;
    back.prepare(QStringLiteral("DELETE FROM removed_libraries WHERE id = ?")); // it is installed again
    back.addBindValue(added);
    back.exec();
    return added;
}

Library::ScanResult Library::scan(const QString &folder)
{
    ScanResult result;
    QStringList known;
    for (const auto rows = all(); const LibraryRecord &r : rows) {
        known << r.path;
        if (!r.fileExists())
            result.missing << r.filename();
    }
    const QDir dir(folder);
    for (const auto zimFiles = dir.entryInfoList({QStringLiteral("*.zim")}, QDir::Files); const QFileInfo &file : zimFiles) {
        if (QFileInfo::exists(file.filePath() + QStringLiteral(".aria2")))
            continue; // aria2 writes to the final name while downloading
        if (known.contains(file.canonicalFilePath()))
            continue;
        QString error;
        if (add(file.filePath(), &error))
            result.added << file.fileName();
        else
            result.errors << i18nc("@info %1 is a file name, %2 why it could not be added", "%1: %2", file.fileName(), error);
    }
    return result;
}

bool Library::remove(qint64 id, bool deleteFile, QString *error)
{
    const auto record = byId(id);
    if (!record) {
        if (error)
            *error = i18n("no such library");
        return false;
    }
    // forgotten, remembered as removed, and taken out of projects together, or none of it; the file only once that has
    // been committed (deleted first, a failed transaction left a library whose file was gone)
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM libraries WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        db.rollback();
        return false;
    }
    q.prepare(
        QStringLiteral("INSERT INTO removed_libraries (id, uuid, path, title) VALUES (?, ?, ?, ?) "
                       "ON CONFLICT (id) DO UPDATE SET uuid = excluded.uuid, path = excluded.path, title = excluded.title, "
                       "removed_at = strftime('%Y-%m-%dT%H:%M:%f', 'now')"));
    q.addBindValue(id);
    q.addBindValue(Database::text(record->uuid));
    q.addBindValue(Database::text(record->path));
    q.addBindValue(Database::text(record->title));
    // its articles leave projects; saved excerpts stay (they hold their own text)
    if (!q.exec() || !Projects::forgetLibrary(id)) {
        if (error)
            *error = q.lastError().text();
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        if (error)
            *error = db.lastError().text();
        db.rollback();
        return false;
    }
    if (deleteFile && record->fileExists() && !QFile::remove(record->path) && error)
        *error = i18n("could not delete %1", record->path); // removed all the same: see the header
    return true;
}
