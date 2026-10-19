// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/database.h"
#include "core/library.h"
#include "core/settings.h"

// Tests that need a real library read KATHENAEUM_TEST_ZIM (a Wikipedia ZIM) and are skipped without it.
class LibraryTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString copyTestZim(const QString &name)
    {
        const QString source = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (source.isEmpty())
            return {};
        const QString target = m_dir.filePath(QStringLiteral("zim/") + name);
        QFile::remove(target);
        return QFile::copy(source, target) ? target : QString();
    }

private Q_SLOTS:
    void init()
    {
        QString error;
        QVERIFY2(Database::open(m_dir.filePath(QStringLiteral("db/%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), &error), qPrintable(error));
        QDir(m_dir.filePath(QStringLiteral("zim"))).removeRecursively();
        QVERIFY(QDir().mkpath(m_dir.filePath(QStringLiteral("zim"))));
    }
    void cleanup() { Database::close(); }

    void settingsRoundTrip()
    {
        QCOMPARE(AppSettings::value(QStringLiteral("nope"), QStringLiteral("fallback")), QStringLiteral("fallback"));
        QVERIFY(AppSettings::setValue(QStringLiteral("k"), QStringLiteral("one")));
        QVERIFY(AppSettings::setValue(QStringLiteral("k"), QStringLiteral("two")));
        QCOMPARE(AppSettings::value(QStringLiteral("k")), QStringLiteral("two"));
        QCOMPARE(AppSettings::libraryFolder(), AppSettings::defaultLibraryFolder());
        QVERIFY(AppSettings::setLibraryFolder(QStringLiteral("/data/zim/")));
        QCOMPARE(AppSettings::libraryFolder(), QStringLiteral("/data/zim"));
    }

    void scanSkipsPartialAndBrokenFiles()
    {
        const QString folder = m_dir.filePath(QStringLiteral("zim"));
        QFile broken(folder + QStringLiteral("/broken_2026-01.zim"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a zim");
        broken.close();
        QFile partial(folder + QStringLiteral("/partial_2026-01.zim"));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("half a download");
        partial.close();
        QFile control(folder + QStringLiteral("/partial_2026-01.zim.aria2"));
        QVERIFY(control.open(QIODevice::WriteOnly));
        control.close();

        const auto result = Library::scan(folder);
        QVERIFY(result.added.isEmpty());
        QCOMPARE(result.errors.size(), 1);  // the broken file; the partial one is not even looked at
        QVERIFY(result.errors.first().startsWith(QStringLiteral("broken_2026-01.zim: ")));
        QVERIFY(Library::all().isEmpty());
    }

    void addsScansAndRemoves()
    {
        const QString path = copyTestZim(QStringLiteral("test_2026-01.zim"));
        if (path.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");

        auto result = Library::scan(m_dir.filePath(QStringLiteral("zim")));
        QCOMPARE(result.added, QStringList{QStringLiteral("test_2026-01.zim")});
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(Library::scan(m_dir.filePath(QStringLiteral("zim"))).added.size(), 0);  // nothing new the second time

        const auto all = Library::all();
        QCOMPARE(all.size(), 1);
        const LibraryRecord r = all.first();
        QVERIFY(!r.title.isEmpty());
        QVERIFY(r.articleCount > 0);
        QCOMPARE(r.sizeBytes, QFileInfo(path).size());
        QVERIFY(r.scraper.startsWith(QStringLiteral("mwoffliner")));
        QVERIFY(r.hasFulltextIndex);
        QVERIFY(!r.uuid.isEmpty());
        QVERIFY(r.addedAt.isValid());
        QVERIFY(Library::illustration(r.id).startsWith("\x89PNG"));

        // adding the same file again refreshes its row instead of duplicating it
        QCOMPARE(Library::add(path), std::optional<qint64>(r.id));
        QCOMPARE(Library::all().size(), 1);

        // a file that disappears is reported, and its row is kept
        QVERIFY(QFile::rename(path, path + QStringLiteral(".moved")));
        result = Library::scan(m_dir.filePath(QStringLiteral("zim")));
        QCOMPARE(result.missing, QStringList{QStringLiteral("test_2026-01.zim")});
        QVERIFY(!Library::all().first().fileExists());
        QVERIFY(QFile::rename(path + QStringLiteral(".moved"), path));

        // removing without deleting keeps the file; with deleting, it goes
        QVERIFY(Library::remove(r.id, false));
        QVERIFY(QFile::exists(path));
        QVERIFY(Library::all().isEmpty());
        const auto id = Library::add(path);
        QVERIFY(id);
        QVERIFY(Library::remove(*id, true));
        QVERIFY(!QFile::exists(path));
        QVERIFY(!Library::remove(*id, true));
    }
};

QTEST_GUILESS_MAIN(LibraryTest)
#include "librarytest.moc"
