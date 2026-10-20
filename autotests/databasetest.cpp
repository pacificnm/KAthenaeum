// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"

class DatabaseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup()
    {
        Database::close();
    }

    void createsSchemaInNewFolder()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sub/folder/test.sqlite"));
        QString error;
        QVERIFY2(Database::open(path, &error), qPrintable(error));
        QCOMPARE(Database::schemaVersion(), Database::latestVersion());
        QSqlQuery q(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"));
        QStringList tables;
        while (q.next())
            tables << q.value(0).toString();
        QVERIFY(tables.contains(QStringLiteral("settings")));
        QVERIFY(tables.contains(QStringLiteral("libraries")));
        // the map regions and places from before Marble are gone (schema 13)
        for (const QString &old : {QStringLiteral("map_regions"), QStringLiteral("map_places"), QStringLiteral("map_places_box")})
            QVERIFY2(!tables.contains(old), qPrintable(old));
    }

    void reopeningKeepsData()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("test.sqlite"));
        QString error;
        QVERIFY2(Database::open(path, &error), qPrintable(error));
        QVERIFY(QSqlQuery().exec(QStringLiteral("INSERT INTO settings (key, value) VALUES ('k', 'v')")));
        Database::close();

        QVERIFY2(Database::open(path, &error), qPrintable(error)); // migrations must not run twice
        QCOMPARE(Database::schemaVersion(), Database::latestVersion());
        QSqlQuery q(QStringLiteral("SELECT value FROM settings WHERE key = 'k'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("v"));
    }

    void refusesNewerSchema()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("test.sqlite"));
        QString error;
        QVERIFY2(Database::open(path, &error), qPrintable(error));
        QVERIFY(QSqlQuery().exec(QStringLiteral("PRAGMA user_version = %1").arg(Database::latestVersion() + 1)));
        Database::close();

        QVERIFY(!Database::open(path, &error));
        QVERIFY(error.contains(QStringLiteral("newer")));
        // and left untouched: nothing can be written through the refused connection
        QVERIFY(!QSqlQuery().exec(QStringLiteral("INSERT INTO settings (key, value) VALUES ('after', 'refused')")));
        Database::close();
        QVERIFY(Database::open(path + QStringLiteral(".check"), &error));
        QVERIFY(QSqlQuery().exec(QStringLiteral("ATTACH DATABASE '%1' AS refused").arg(path)));
        QSqlQuery q(QStringLiteral("SELECT count(*) FROM refused.settings WHERE key = 'after'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    void aFailedMigrationLeavesNothingOpen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("test.sqlite"));
        QString error;
        QVERIFY2(Database::open(path, &error), qPrintable(error));
        // pretend the last migration has not run, and make the file read-only so running it fails
        QVERIFY(QSqlQuery().exec(QStringLiteral("PRAGMA user_version = %1").arg(Database::latestVersion() - 1)));
        Database::close();
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        QVERIFY(!Database::open(path, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QSqlQuery().exec(QStringLiteral("SELECT 1 FROM settings")));
    }
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "databasetest.moc"
