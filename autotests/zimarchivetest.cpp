// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include "core/zimarchive.h"

// Real-ZIM checks run when KATHENAEUM_TEST_ZIM points at a Wikipedia (mwoffliner) ZIM; ZIM files are not kept in the repository.
class ZimArchiveTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void missingFileIsAnError()
    {
        QString error;
        QVERIFY(!ZimArchive::readInfo(QStringLiteral("/nonexistent/library.zim"), &error));
        QVERIFY(!error.isEmpty());
    }

    void garbageFileIsAnError()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write("this is not a ZIM file");
        file.close();
        QString error;
        QVERIFY(!ZimArchive::readInfo(file.fileName(), &error));
        QVERIFY(!error.isEmpty());
    }

    void readsRealMetadata()
    {
        const QString path = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (path.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        QString error;
        const auto info = ZimArchive::readInfo(path, &error);
        QVERIFY2(info, qPrintable(error));
        QVERIFY(!info->title.isEmpty());
        QVERIFY(!info->name.isEmpty());
        QVERIFY(info->articleCount > 0);
        QVERIFY(info->illustration.startsWith("\x89PNG"));
    }

    void verifyAcceptsAnIntactFileAndRejectsAChangedByte()
    {
        const QString source = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (source.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        QTemporaryDir dir;
        const QString copy = dir.filePath(QStringLiteral("copy.zim"));
        QVERIFY(QFile::copy(source, copy));
        QCOMPARE(ZimArchive::verify(copy, 0), QString());
        // the catalog's size, rounded up to the KiB, is accepted; a wrong one is not
        const qint64 size = QFileInfo(copy).size();
        QCOMPARE(ZimArchive::verify(copy, (size + 1023) / 1024 * 1024), QString());
        QVERIFY(ZimArchive::verify(copy, size + 5000).contains(QLatin1String("catalog says")));
        // one byte changed in the middle (not in the header): the checksum must catch it
        QFile f(copy);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.seek(size / 2));
        char c = 0;
        QVERIFY(f.getChar(&c));
        QVERIFY(f.seek(size / 2));
        QVERIFY(f.putChar(char(c ^ 0x5a)));
        f.close();
        QVERIFY(ZimArchive::verify(copy, 0).contains(QLatin1String("checksum")));
    }

    void verifyStopsWhenAsked()
    {
        const QString source = qEnvironmentVariable("KATHENAEUM_TEST_ZIM");
        if (source.isEmpty())
            QSKIP("Set KATHENAEUM_TEST_ZIM to a Wikipedia ZIM file to run this test");
        const std::atomic<bool> stop = true;
        QCOMPARE(ZimArchive::verify(source, 0, &stop), ZimArchive::Stopped);
    }
};

QTEST_GUILESS_MAIN(ZimArchiveTest)
#include "zimarchivetest.moc"
