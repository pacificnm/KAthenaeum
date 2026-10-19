// SPDX-License-Identifier: GPL-3.0-or-later
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
};

QTEST_GUILESS_MAIN(ZimArchiveTest)
#include "zimarchivetest.moc"
