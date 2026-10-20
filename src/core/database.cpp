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
    {
        // "Ask about this article": the article a chat is about (none: library_id NULL). No foreign key: a chat about an
        // article outlives its library, and says so when reopened.
        QStringLiteral("ALTER TABLE chats ADD COLUMN library_id INTEGER"),
        QStringLiteral("ALTER TABLE chats ADD COLUMN article_path TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("ALTER TABLE chats ADD COLUMN article_title TEXT NOT NULL DEFAULT ''"),
        QStringLiteral("CREATE INDEX chats_article ON chats (library_id, article_path)"),
    },
    {
        // Research projects: sources (articles, saved excerpts, attached chats), Markdown documents, and chats started
        // in a project (deleting the project keeps those chats)
        QStringLiteral("CREATE TABLE projects ("
                       " id INTEGER PRIMARY KEY,"
                       " title TEXT NOT NULL,"
                       " description TEXT NOT NULL DEFAULT '',"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE INDEX projects_recent ON projects (updated_at DESC)"),
        // kind: article | clip | chat. An article: library_id, path, title. A clip (a saved excerpt): text, section, and
        // refs (JSON [{libraryId, path, title}], the articles it came from; library_id/path/title repeat the first), with an
        // embedding (float32 BLOB) and the model that made it. A chat: chat_id.
        QStringLiteral("CREATE TABLE project_items ("
                       " id INTEGER PRIMARY KEY,"
                       " project_id INTEGER NOT NULL REFERENCES projects (id) ON DELETE CASCADE,"
                       " kind TEXT NOT NULL CHECK (kind IN ('article', 'clip', 'chat')),"
                       " library_id INTEGER,"
                       " path TEXT NOT NULL DEFAULT '',"
                       " title TEXT NOT NULL DEFAULT '',"
                       " section TEXT NOT NULL DEFAULT '',"
                       " text TEXT NOT NULL DEFAULT '',"
                       " refs TEXT NOT NULL DEFAULT '[]',"
                       " note TEXT NOT NULL DEFAULT '',"
                       " embedding BLOB,"
                       " embed_model TEXT NOT NULL DEFAULT '',"
                       " chat_id INTEGER REFERENCES chats (id) ON DELETE CASCADE,"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE UNIQUE INDEX project_articles ON project_items (project_id, library_id, path) WHERE kind = 'article'"),
        QStringLiteral("CREATE UNIQUE INDEX project_chats ON project_items (project_id, chat_id) WHERE kind = 'chat'"),
        QStringLiteral("CREATE INDEX project_items_project ON project_items (project_id, kind)"),
        QStringLiteral("CREATE INDEX project_items_article ON project_items (library_id, path)"),
        QStringLiteral("CREATE TABLE project_documents ("
                       " id INTEGER PRIMARY KEY,"
                       " project_id INTEGER NOT NULL REFERENCES projects (id) ON DELETE CASCADE,"
                       " title TEXT NOT NULL,"
                       " content TEXT NOT NULL DEFAULT '',"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE INDEX project_documents_project ON project_documents (project_id, updated_at DESC)"),
        QStringLiteral("ALTER TABLE chats ADD COLUMN project_id INTEGER REFERENCES projects (id) ON DELETE SET NULL"),
        QStringLiteral("CREATE INDEX chats_project ON chats (project_id)"),
    },
    {
        // Libraries that were removed. Chats, excerpts and document links keep a library's id (no foreign key, so they
        // outlive it); SQLite would hand that id to the next library added (max + 1, when the newest was removed), and they
        // would then point at another ZIM. Removed ids are never given to another library, and the same ZIM added again
        // (same uuid, else same path) gets its id back, so its chats and excerpts come back with it.
        QStringLiteral("CREATE TABLE removed_libraries ("
                       " id INTEGER PRIMARY KEY,"
                       " uuid TEXT NOT NULL DEFAULT '',"
                       " path TEXT NOT NULL DEFAULT '',"
                       " title TEXT NOT NULL DEFAULT '',"
                       " removed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
    },
    {
        // Map regions: each one's map file (cut from Protomaps' planet build) and the places in it that Wikipedia may have
        // articles about (from OpenStreetMap). `region` is Geofabrik's id ("us/oregon"), or "world" for the overview.
        QStringLiteral("CREATE TABLE map_regions ("
                       " id INTEGER PRIMARY KEY,"
                       " region TEXT NOT NULL UNIQUE,"
                       " name TEXT NOT NULL,"
                       " parent_name TEXT NOT NULL DEFAULT '',"
                       " max_zoom INTEGER NOT NULL,"
                       " shape TEXT NOT NULL DEFAULT '',"  // GeoJSON geometry: what the map is cut to
                       " pbf TEXT NOT NULL DEFAULT '',"  // its OpenStreetMap extract ('' for the world overview)
                       " west REAL NOT NULL, south REAL NOT NULL, east REAL NOT NULL, north REAL NOT NULL,"
                       " status TEXT NOT NULL DEFAULT 'queued',"  // queued | downloading | indexing | complete | failed | cancelled
                       " error TEXT NOT NULL DEFAULT '',"
                       " file TEXT NOT NULL DEFAULT '',"
                       " size INTEGER NOT NULL DEFAULT 0,"
                       " build TEXT NOT NULL DEFAULT '',"  // the planet build it was cut from ("20261019")
                       " places INTEGER NOT NULL DEFAULT 0,"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE TABLE map_places ("
                       " id INTEGER PRIMARY KEY,"
                       " region_id INTEGER NOT NULL REFERENCES map_regions (id) ON DELETE CASCADE,"
                       " osm TEXT NOT NULL,"  // n123 | w123 | r123
                       " name TEXT NOT NULL,"
                       " name_en TEXT NOT NULL DEFAULT '',"
                       " kind TEXT NOT NULL DEFAULT '',"
                       " lat REAL NOT NULL, lon REAL NOT NULL,"
                       " west REAL NOT NULL, south REAL NOT NULL, east REAL NOT NULL, north REAL NOT NULL,"
                       " area INTEGER NOT NULL DEFAULT 0,"
                       " wikipedia TEXT NOT NULL DEFAULT '',"
                       " wikipedia_en TEXT NOT NULL DEFAULT '',"
                       " wikidata TEXT NOT NULL DEFAULT '',"
                       " population INTEGER NOT NULL DEFAULT 0,"
                       " outline BLOB NOT NULL DEFAULT '')"),  // an area's edges, simplified (MapPlaces::encodeOutline)
        QStringLiteral("CREATE INDEX map_places_osm ON map_places (osm)"),
        QStringLiteral("CREATE INDEX map_places_region ON map_places (region_id)"),
        QStringLiteral("CREATE INDEX map_places_name ON map_places (name COLLATE NOCASE)"),
        // the places' extents, for "what is here" (an R*Tree has no foreign keys: rows are deleted with their place)
        QStringLiteral("CREATE VIRTUAL TABLE map_places_box USING rtree (id, west, east, south, north)"),
    },
    {
        // Highlights: passages the reader marked in an article, in a colour, with an optional note. Not tied to a project
        // (an excerpt is). No foreign key, as for excerpts: a highlight outlives its library and comes back with it.
        QStringLiteral("CREATE TABLE highlights ("
                       " id INTEGER PRIMARY KEY,"
                       " library_id INTEGER NOT NULL,"
                       " path TEXT NOT NULL,"
                       " title TEXT NOT NULL DEFAULT '',"
                       " text TEXT NOT NULL,"
                       " color TEXT NOT NULL DEFAULT 'yellow',"  // yellow | green | blue | pink
                       " note TEXT NOT NULL DEFAULT '',"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE INDEX highlights_article ON highlights (library_id, path)"),
        QStringLiteral("CREATE INDEX highlights_recent ON highlights (updated_at DESC)"),
        // "Show on Map" from an article: the places that name it
        QStringLiteral("CREATE INDEX map_places_wikipedia ON map_places (wikipedia)"),
        QStringLiteral("CREATE INDEX map_places_wikipedia_en ON map_places (wikipedia_en)"),
    },
    {
        // The classroom: the learner (one per computer), courses, their units and lessons. A lesson's readings are
        // articles of the installed libraries (JSON [{libraryId, path, title, score}]); its intro is the teacher's
        // Markdown, written when it is first opened. grade and level are keys (Classroom::levels, gradesCompleted).
        // AUTOINCREMENT: planning a course again replaces its lessons, and a lesson page still open must not show another
        // lesson that took its id (SQLite reuses the highest ids otherwise).
        QStringLiteral("CREATE TABLE learner ("
                       " id INTEGER PRIMARY KEY CHECK (id = 1),"
                       " name TEXT NOT NULL DEFAULT '',"
                       " grade TEXT NOT NULL DEFAULT 'none',"
                       " interests TEXT NOT NULL DEFAULT '',"
                       " goals TEXT NOT NULL DEFAULT '',"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE TABLE courses ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " subject TEXT NOT NULL,"
                       " title TEXT NOT NULL,"
                       " level TEXT NOT NULL,"
                       " focus TEXT NOT NULL DEFAULT '',"
                       " length TEXT NOT NULL DEFAULT 'standard',"  // short | standard | long
                       " status TEXT NOT NULL DEFAULT 'new',"  // new | planning | ready | failed
                       " error TEXT NOT NULL DEFAULT '',"
                       " created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')),"
                       " updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f', 'now')))"),
        QStringLiteral("CREATE TABLE course_units ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " course_id INTEGER NOT NULL REFERENCES courses (id) ON DELETE CASCADE,"
                       " position INTEGER NOT NULL,"
                       " title TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE INDEX course_units_course ON course_units (course_id, position)"),
        QStringLiteral("CREATE TABLE course_lessons ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " course_id INTEGER NOT NULL REFERENCES courses (id) ON DELETE CASCADE,"
                       " unit_id INTEGER NOT NULL REFERENCES course_units (id) ON DELETE CASCADE,"
                       " position INTEGER NOT NULL,"
                       " title TEXT NOT NULL,"
                       " objective TEXT NOT NULL DEFAULT '',"
                       " readings TEXT NOT NULL DEFAULT '[]',"
                       " intro TEXT NOT NULL DEFAULT '',"
                       " completed_at TEXT)"),
        QStringLiteral("CREATE INDEX course_lessons_course ON course_lessons (course_id, unit_id, position)"),
    },
    {
        // A course kept in a research project: its lessons' readings become sources, each introduction a document, and
        // highlights made in the readings excerpts. A source's lesson is a label ("Lesson 3: Fractions"), not an id, so
        // it survives planning the course again (which replaces the lessons).
        QStringLiteral("ALTER TABLE courses ADD COLUMN project_id INTEGER REFERENCES projects (id) ON DELETE SET NULL"),
        QStringLiteral("ALTER TABLE course_lessons ADD COLUMN document_id INTEGER REFERENCES project_documents (id) ON DELETE SET NULL"),
        QStringLiteral("ALTER TABLE project_items ADD COLUMN lesson TEXT NOT NULL DEFAULT ''"),
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
    // A database that cannot be used is closed, never left open half-usable: a newer build's database must not be written
    // by this one, and a failed migration must not leave the app saving into a schema it does not expect (the review found
    // both, 2026-10-19)
    auto refuse = [&db]() {
        db.close();
        return false;
    };
    if (!exec(db, QStringLiteral("PRAGMA foreign_keys = ON"), error) || !exec(db, QStringLiteral("PRAGMA journal_mode = WAL"), error))
        return refuse();

    const int current = schemaVersion();
    if (current > latestVersion()) {
        if (error)
            *error = QStringLiteral("The database was made by a newer KAthenaeum (schema %1, this build knows %2)").arg(current).arg(latestVersion());
        return refuse();
    }
    for (int v = current; v < latestVersion(); ++v) {
        bool ok = db.transaction();
        if (!ok && error)
            *error = db.lastError().text();
        for (const QString &sql : migrations[v])
            ok = ok && exec(db, sql, error);
        ok = ok && exec(db, QStringLiteral("PRAGMA user_version = %1").arg(v + 1), error);
        if (ok && !db.commit()) {
            ok = false;
            if (error)
                *error = db.lastError().text();
        }
        if (!ok) {
            db.rollback();
            return refuse();
        }
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
