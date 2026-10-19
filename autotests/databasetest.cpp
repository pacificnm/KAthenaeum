// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"

class DatabaseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup() { Database::close(); }

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
    }

    void reopeningKeepsData()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("test.sqlite"));
        QString error;
        QVERIFY2(Database::open(path, &error), qPrintable(error));
        QVERIFY(QSqlQuery().exec(QStringLiteral("INSERT INTO settings (key, value) VALUES ('k', 'v')")));
        Database::close();

        QVERIFY2(Database::open(path, &error), qPrintable(error));  // migrations must not run twice
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
    }
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "databasetest.moc"
