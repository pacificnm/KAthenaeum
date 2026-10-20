// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/chats.h"
#include "core/database.h"
#include "core/library.h"
#include "core/projects.h"

#include <zim/writer/creator.h>
#include <zim/writer/item.h>

// Found by the review (2026-10-19): a removed library's id was handed to the next library added, so its chats and
// excerpts pointed at another ZIM; and the same ZIM added again got a new id, so they were lost.
class LibraryIdsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString makeZim(const QString &name)
    {
        const QString file = m_dir.filePath(name);
        zim::writer::Creator c;  // every ZIM gets its own uuid
        c.startZimCreation(file.toStdString());
        c.setMainPath("A");
        for (const auto &[k, v] : std::vector<std::pair<std::string, std::string>>{{"Name", name.toStdString()}, {"Title", name.toStdString()}, {"Language", "eng"}})
            c.addMetadata(k, v);
        c.addItem(zim::writer::StringItem::create(std::string("A"), std::string("text/html"), std::string("A"), zim::writer::Hints{{zim::writer::HintKeys::FRONT_ARTICLE, 1}},
                                                  std::string("<html><head><title>A</title></head><body><div class='mw-parser-output'><p>Text.</p></div></body></html>")));
        c.finishZimCreation();
        return file;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(Database::open(m_dir.filePath(QStringLiteral("t.sqlite")), nullptr));
    }

    void aRemovedLibrarysIdIsNeverGivenToAnotherAndComesBackWithIt()
    {
        const QString chemistry = makeZim(QStringLiteral("chemistry.zim"));
        const QString ray = makeZim(QStringLiteral("ray.zim"));
        const QString history = makeZim(QStringLiteral("history.zim"));
        const qint64 c = *Library::add(chemistry);
        const qint64 r = *Library::add(ray);
        QCOMPARE(r, c + 1);
        const qint64 chat = Chats::create(QStringLiteral("About Ray"), r, QStringLiteral("A"), QStringLiteral("A"));
        const qint64 project = Projects::create(QStringLiteral("P"));
        Projects::addClip(project, QStringLiteral("A saved passage of text."), {{r, QStringLiteral("A"), QStringLiteral("A")}});

        // remove the newest library, then add a different one: it must not inherit the id
        QVERIFY(Library::remove(r, false));
        const qint64 h = *Library::add(history);
        QVERIFY(h != r);
        QCOMPARE(h, r + 1);
        QVERIFY(Projects::highlightsFor(h, QStringLiteral("A")).isEmpty());
        QCOMPARE(Chats::aboutArticle(h, QStringLiteral("A")).size(), 0);

        // the same ZIM again, even from another place: its id, chats and excerpts come back
        QVERIFY(QFile::rename(ray, ray + QStringLiteral(".moved.zim")));
        QCOMPARE(*Library::add(ray + QStringLiteral(".moved.zim")), r);
        QCOMPARE(Chats::aboutArticle(r, QStringLiteral("A")).constFirst().id, chat);
        QCOMPARE(Projects::highlightsFor(r, QStringLiteral("A")).size(), 1);

        // adding it twice keeps its id
        QCOMPARE(*Library::add(ray + QStringLiteral(".moved.zim")), r);
        // ids keep growing past every id ever used
        QVERIFY(Library::remove(h, false));
        const QString music = makeZim(QStringLiteral("music.zim"));
        QCOMPARE(*Library::add(music), h + 1);
    }

    void cleanupTestCase() { Database::close(); }
};

QTEST_GUILESS_MAIN(LibraryIdsTest)
#include "libraryidstest.moc"
