// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// An installed library: a row in `libraries`, describing a ZIM file somewhere on disk.
struct LibraryRecord {
    qint64 id = 0;
    QString path;
    QString name;
    QString title;
    QString description;
    QString language;
    QString creator;
    QString publisher;
    QString date;
    QString flavour;
    QString scraper;
    QString uuid;
    qint64 articleCount = 0;
    qint64 sizeBytes = 0;
    bool hasFulltextIndex = false;
    QDateTime addedAt;

    QString filename() const;
    bool fileExists() const;
};

namespace Library
{
QList<LibraryRecord> all();
std::optional<LibraryRecord> byId(qint64 id);
QByteArray illustration(qint64 id); // 48x48 PNG, empty if none

// Registers the ZIM at `path` (or refreshes its row if it is already known). Returns its id, or nothing with `error` set.
std::optional<qint64> add(const QString &path, QString *error = nullptr);

struct ScanResult {
    QStringList added; // file names newly registered
    QStringList missing; // registered files that are gone (rows are kept: projects may refer to them)
    QStringList errors; // "name: problem" for files that could not be read
};
// Registers every complete .zim in `folder` (a file with a .aria2 sidecar is still downloading) and reports registered
// files from anywhere that no longer exist.
ScanResult scan(const QString &folder);

// Removes the library from KAthenaeum, and its file too when `deleteFile` is set. False (with `error`) if it could not be
// removed. True once it is removed, even if its file could then not be deleted: `error` then says so, and the file stays.
bool remove(qint64 id, bool deleteFile, QString *error = nullptr);
}
