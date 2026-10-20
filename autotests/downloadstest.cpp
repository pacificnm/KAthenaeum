// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"
#include "core/downloads.h"
#include "core/library.h"
#include "core/zimarchive.h"

// Real downloads from kiwix.org run only with KATHENAEUM_NETWORK_TESTS=1 (a ~2 MB and a ~22 MB library).
class DownloadsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_folder;

    void write(const QString &path, const QByteArray &data)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    static QUrl kiwix(const QString &filename)
    {
        return QUrl(QStringLiteral("https://lb.download.kiwix.org/zim/wikipedia/%1.meta4").arg(filename));
    }

private Q_SLOTS:
    void init()
    {
        QString error;
        QVERIFY2(Database::open(m_dir.filePath(QStringLiteral("%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), &error), qPrintable(error));
        m_folder = m_dir.filePath(QStringLiteral("zim-%1").arg(QString::fromLatin1(QTest::currentTestFunction())));
        QVERIFY(QDir().mkpath(m_folder));
    }
    void cleanup()
    {
        Database::close();
    }

    void verifyCatchesBadFiles()
    {
        QVERIFY(ZimArchive::verify(m_folder + QStringLiteral("/missing.zim"), 0).contains(QStringLiteral("missing")));

        const QString partial = m_folder + QStringLiteral("/partial.zim");
        write(partial, "data");
        write(partial + QStringLiteral(".aria2"), "");
        QVERIFY(ZimArchive::verify(partial, 0).contains(QStringLiteral("did not finish")));

        const QString garbage = m_folder + QStringLiteral("/garbage.zim");
        write(garbage, QByteArray(5000, 'x'));
        QVERIFY(ZimArchive::verify(garbage, 9000).contains(QStringLiteral("catalog says")));
        QVERIFY(ZimArchive::verify(garbage, 5000).contains(QStringLiteral("not a valid ZIM")));
    }

    void verifyAcceptsRealZim()
    {
        const QString zim = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (zim.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        const qint64 size = QFileInfo(zim).size();
        QCOMPARE(ZimArchive::verify(zim, 0), QString());
        QCOMPARE(ZimArchive::verify(zim, size), QString());
        QCOMPARE(ZimArchive::verify(zim, size + 1023), QString()); // the catalog rounds up to the next KiB
        QVERIFY(!ZimArchive::verify(zim, size + 1024).isEmpty());
        QVERIFY(!ZimArchive::verify(zim, size - 1).isEmpty());
    }

    void startRefusesBadRequests()
    {
        DownloadManager dm;
        const QString f = QStringLiteral("wikipedia_en_ray-charles_nopic_2026-08.zim");
        QVERIFY(dm.start(QUrl(QStringLiteral("https://evil.example/%1").arg(f)), f, {}, 0, m_folder).contains(QStringLiteral("kiwix.org")));
        QVERIFY(dm.start(kiwix(f), QStringLiteral("../escape.zim"), {}, 0, m_folder).contains(QStringLiteral("not a valid ZIM file name")));
        QVERIFY(dm.start(kiwix(f), f, {}, qint64(1) << 60, m_folder).contains(QStringLiteral("not enough free space")));
        QVERIFY(dm.list().isEmpty()); // nothing was recorded for refused requests
    }

    void reconcileMarksInterruptedDownloads()
    {
        QSqlQuery q;
        QVERIFY(
            q.exec(QStringLiteral("INSERT INTO downloads (url, filename, folder, status) VALUES ('https://download.kiwix.org/a_2026-01.zim', 'a_2026-01.zim', "
                                  "'/x', 'downloading')")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO downloads (url, filename, folder, status) VALUES ('https://download.kiwix.org/b_2026-01.zim', 'b_2026-01.zim', '/x', 'complete')")));
        DownloadManager dm;
        dm.reconcile();
        const auto list = dm.list();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[1].status, QStringLiteral("failed"));
        QVERIFY(list[1].error.contains(QStringLiteral("Resume")));
        QCOMPARE(list[0].status, QStringLiteral("complete"));
    }

    void aDownloadLeftVerifyingIsVerifiedAgainAtStart()
    {
        // the app closed during verification (which stops at once): the file is complete, so check it again
        QSqlQuery q;
        QVERIFY(q.exec(
            QStringLiteral(
                "INSERT INTO downloads (url, filename, folder, status) VALUES ('https://download.kiwix.org/c_2026-01.zim', 'c_2026-01.zim', '%1', 'verifying')")
                .arg(m_folder)));
        DownloadManager dm;
        QSignalSpy failed(&dm, &DownloadManager::failed);
        dm.reconcile();
        QTRY_COMPARE(failed.size(), 1); // the file is not there, so this verification fails, and says why
        QVERIFY(failed.first().at(1).toString().contains(QLatin1String("missing")));
    }

    void removeKeepsInstalledFiles()
    {
        const QString zim = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (zim.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        const QString path = m_folder + QStringLiteral("/lib_2026-01.zim");
        QVERIFY(QFile::copy(zim, path));
        QVERIFY(Library::add(path));
        QSqlQuery q;
        q.prepare(QStringLiteral(
            "INSERT INTO downloads (url, filename, folder, status) VALUES ('https://download.kiwix.org/lib_2026-01.zim', 'lib_2026-01.zim', ?, 'failed')"));
        q.addBindValue(m_folder);
        QVERIFY(q.exec());
        DownloadManager dm;
        QVERIFY(dm.remove(dm.list().constFirst().id));
        QVERIFY(QFile::exists(path)); // the row said failed, but a library uses this file
        QVERIFY(dm.list().isEmpty());
    }

    void downloadsVerifiesAndRegisters()
    {
        if (qEnvironmentVariable("KATHENAEUM_NETWORK_TESTS") != QLatin1String("1"))
            QSKIP("Set KATHENAEUM_NETWORK_TESTS=1 to download from kiwix.org");
        DownloadManager dm;
        QSignalSpy completed(&dm, &DownloadManager::completed);
        QSignalSpy failed(&dm, &DownloadManager::failed);
        const QString f = QStringLiteral("wikipedia_en_ray-charles_nopic_2026-08.zim");
        QCOMPARE(dm.start(kiwix(f), f, QStringLiteral("Ray Charles"), 1839104, m_folder), QString());
        QCOMPARE(dm.activeCount(), 1);
        QVERIFY(dm.start(kiwix(f), f, {}, 1839104, m_folder).contains(QStringLiteral("already downloading")));
        QVERIFY(completed.wait(120000) || !failed.isEmpty());
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        const auto r = dm.list().constFirst();
        QCOMPARE(r.status, QStringLiteral("complete"));
        QCOMPARE(r.progress(), 1.0);
        QVERIFY(QFile::exists(r.path()));
        QVERIFY(!QFile::exists(r.path() + QStringLiteral(".aria2")));
        QVERIFY(!QFile::exists(r.path() + QStringLiteral(".meta4")));
        const auto lib = Library::byId(completed.first().at(1).toLongLong());
        QVERIFY(lib);
        QCOMPARE(lib->title, QStringLiteral("Ray Charles"));
        QVERIFY(dm.start(kiwix(f), f, {}, 1839104, m_folder).contains(QStringLiteral("already installed")));
    }

    void cancelThenResume()
    {
        if (qEnvironmentVariable("KATHENAEUM_NETWORK_TESTS") != QLatin1String("1"))
            QSKIP("Set KATHENAEUM_NETWORK_TESTS=1 to download from kiwix.org");
        DownloadManager dm;
        QSignalSpy changed(&dm, &DownloadManager::changed);
        QSignalSpy completed(&dm, &DownloadManager::completed);
        const QString f = QStringLiteral("wikipedia_en_chemistry_mini_2026-10.zim");
        QCOMPARE(dm.start(kiwix(f), f, QStringLiteral("Chemistry"), 23322624, m_folder), QString());
        const qint64 id = dm.list().constFirst().id;
        QTRY_VERIFY_WITH_TIMEOUT(dm.byId(id)->doneBytes > 0, 60000); // some data has arrived
        QVERIFY(dm.cancel(id));
        QTRY_COMPARE_WITH_TIMEOUT(dm.byId(id)->status, QStringLiteral("cancelled"), 10000);
        QVERIFY(QFile::exists(dm.byId(id)->path() + QStringLiteral(".aria2"))); // kept for resuming

        QCOMPARE(dm.resume(id), QString());
        QVERIFY(completed.wait(180000));
        QCOMPARE(dm.byId(id)->status, QStringLiteral("complete"));
        QCOMPARE(ZimArchive::verify(dm.byId(id)->path(), 23322624), QString());
    }
};

QTEST_GUILESS_MAIN(DownloadsTest)
#include "downloadstest.moc"
