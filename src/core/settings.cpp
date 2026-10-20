// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include "database.h"

#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>

QString AppSettings::value(const QString &key, const QString &fallback)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(key);
    return q.exec() && q.next() ? q.value(0).toString() : fallback;
}

bool AppSettings::setValue(const QString &key, const QString &value)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT (key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(Database::text(value)); // a null QString would bind as NULL, and the column is NOT NULL
    return q.exec();
}

QString AppSettings::defaultLibraryFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("zim"));
}

QString AppSettings::libraryFolder()
{
    return value(QStringLiteral("library.folder"), defaultLibraryFolder());
}

bool AppSettings::setLibraryFolder(const QString &folder)
{
    return setValue(QStringLiteral("library.folder"), QDir::cleanPath(folder));
}

QString AppSettings::ollamaUrl()
{
    return value(QStringLiteral("ollama.url"), QStringLiteral("http://localhost:11434"));
}

QString AppSettings::answerModel()
{
    return value(QStringLiteral("models.answer"), QStringLiteral("qwen3.5:2b"));
}

QString AppSettings::embeddingModel()
{
    return value(QStringLiteral("models.embedding"), QStringLiteral("nomic-embed-text"));
}

bool AppSettings::articleFullWidth()
{
    return value(QStringLiteral("reading.width"), QStringLiteral("full")) == QLatin1String("full");
}

bool AppSettings::setArticleFullWidth(bool full)
{
    return setValue(QStringLiteral("reading.width"), full ? QStringLiteral("full") : QStringLiteral("column"));
}

bool AppSettings::autoCheckLibraryUpdates()
{
    return value(QStringLiteral("library.autoUpdateCheck"), QStringLiteral("on")) == QLatin1String("on");
}

bool AppSettings::setAutoCheckLibraryUpdates(bool on)
{
    return setValue(QStringLiteral("library.autoUpdateCheck"), on ? QStringLiteral("on") : QStringLiteral("off"));
}

QDateTime AppSettings::lastLibraryUpdateCheck()
{
    return QDateTime::fromString(value(QStringLiteral("library.lastUpdateCheck")), Qt::ISODate);
}

bool AppSettings::setLastLibraryUpdateCheck(const QDateTime &when)
{
    return setValue(QStringLiteral("library.lastUpdateCheck"), when.toUTC().toString(Qt::ISODate));
}

QString AppSettings::citationStyle()
{
    return value(QStringLiteral("citation.style"), QStringLiteral("mla"));
}

bool AppSettings::setCitationStyle(const QString &style)
{
    return setValue(QStringLiteral("citation.style"), style);
}
