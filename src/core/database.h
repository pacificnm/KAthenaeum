// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

// The app's SQLite database: chats, projects, excerpts, settings, installed libraries (see CLAUDE.md).
// One file in the user's data directory, opened once at startup as Qt's default connection.
namespace Database
{
// Where the database lives by default: <AppDataLocation>/kathenaeum.sqlite
QString defaultPath();

// Opens (creating if needed) the database at `path` and brings its schema up to date.
// Returns false and fills `error` if the file cannot be opened or a migration fails.
bool open(const QString &path, QString *error);

// PRAGMA user_version: the number of migrations applied.
int schemaVersion();

// The schema version this build expects.
int latestVersion();

void close();

// A null QString binds as SQL NULL, which our NOT NULL text columns refuse (the statement then fails silently unless its
// result is checked). Wrap every string that may be empty in this before binding it.
inline QString text(const QString &s)
{
    return s.isNull() ? QString(QLatin1String("")) : s;
}
}
