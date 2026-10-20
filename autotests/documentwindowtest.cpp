// SPDX-License-Identifier: GPL-3.0-or-later
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>

#include <KLocalizedString>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include "core/database.h"
#include "core/projects.h"
#include "documentwindow.h"
#include "projectlist.h"

// The document window: Kate's editor saving into the project document, Cite inserting the project's sources, the
// preview, and the window going away with its document
class DocumentWindowTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    qint64 m_project = 0;
    qint64 m_doc = 0;

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("kathenaeum");
    }

    void init()
    {
        QVERIFY(Database::open(m_dir.filePath(QStringLiteral("%1.sqlite").arg(QString::fromLatin1(QTest::currentTestFunction()))), nullptr));
        QVERIFY(QSqlQuery().exec(QStringLiteral("INSERT INTO libraries (id, path, title) VALUES (1, '/x/chem.zim', 'Chemistry')")));
        m_project = Projects::create(QStringLiteral("Acids"));
        Projects::addArticle(m_project, {1, QStringLiteral("Acid"), QStringLiteral("Acid")}, QStringLiteral("start here"));
        Projects::addClip(m_project, QStringLiteral("An acid donates a proton."), {{1, QStringLiteral("Acid"), QStringLiteral("Acid")}});
        m_doc = Projects::createDocument(m_project, QStringLiteral("Notes"), QStringLiteral("First line."));
    }
    void cleanup() { Database::close(); }

    void typingIsSavedAndShownInThePreview()
    {
        DocumentWindow w(m_doc);
        auto *view = w.findChild<KTextEditor::View *>();
        QVERIFY(view);
        QCOMPARE(view->document()->text(), QStringLiteral("First line."));
        QCOMPARE(view->document()->highlightingMode(), QStringLiteral("Markdown"));
        view->document()->setText(QStringLiteral("## Strong acids\n\nThey dissociate completely."));
        auto *title = w.findChild<QLineEdit *>();
        title->setText(QStringLiteral("Acid notes"));
        Q_EMIT title->textEdited(title->text());
        QTRY_COMPARE_WITH_TIMEOUT(Projects::document(m_doc)->content, QStringLiteral("## Strong acids\n\nThey dissociate completely."), 3000);
        QCOMPARE(Projects::document(m_doc)->title, QStringLiteral("Acid notes"));
        QVERIFY(w.windowTitle().startsWith(QLatin1String("Acid notes")));
        auto *preview = w.findChild<QTextBrowser *>();
        QTRY_VERIFY(preview->toPlainText().contains(QLatin1String("They dissociate completely.")));
        QVERIFY(preview->toPlainText().startsWith(QLatin1String("Acid notes")));
    }

    void citeInsertsALinkOrAQuotation()
    {
        DocumentWindow w(m_doc);
        auto *view = w.findChild<KTextEditor::View *>();
        view->setCursorPosition({0, int(view->document()->line(0).size())});
        QMenu *cite = nullptr;
        for (auto *b : w.findChildren<QToolButton *>())
            if (b->menu())
                cite = b->menu();
        QVERIFY(cite);
        Q_EMIT cite->aboutToShow();
        QList<QAction *> actions;
        for (QAction *a : cite->actions())
            if (!a->isSeparator())
                actions << a;
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions[0]->text(), QStringLiteral("Acid (has a note)"));
        actions[0]->trigger();
        QCOMPARE(view->document()->text(), QStringLiteral("First line.[Acid](zim://lib1/Acid)"));
        actions[1]->trigger();  // a quotation goes on lines of its own
        QVERIFY2(view->document()->text().contains(QStringLiteral("[Acid](zim://lib1/Acid)\n\n> An acid donates a proton.\n>\n> — Acid\n\n")),
                 qPrintable(view->document()->text()));
        // the preview shows the article's note beside the link, and never loads images
        w.findChild<QTextBrowser *>()->setMarkdown(QString());
        QTRY_VERIFY(w.findChild<QTextBrowser *>()->toPlainText().contains(QLatin1String("(note: start here)")));
        QVERIFY(!w.findChild<QTextBrowser *>()->loadResource(QTextDocument::ImageResource, QUrl(QStringLiteral("http://x/y.png"))).isValid());
    }

    void closingSavesWhatWasTyped()
    {
        auto *w = new DocumentWindow(m_doc);
        w->findChild<KTextEditor::View *>()->document()->setText(QStringLiteral("Typed just before closing."));
        w->close();  // deletes itself
        QCOMPARE(Projects::document(m_doc)->content, QStringLiteral("Typed just before closing."));
    }

    void theWindowClosesWithItsDocument()
    {
        QPointer<DocumentWindow> w = new DocumentWindow(m_doc);
        w->show();
        QVERIFY(Projects::removeDocument(m_doc));
        Q_EMIT ProjectsNotifier::instance()->changed();
        QTRY_VERIFY(w.isNull());
    }

    void oneWindowPerDocument()
    {
        DocumentWindows::instance()->open(m_doc);
        DocumentWindows::instance()->open(m_doc);
        int open = 0;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (qobject_cast<DocumentWindow *>(top) && top->isVisible())
                ++open;
        QCOMPARE(open, 1);
        DocumentWindows::instance()->closeAll();
    }
};

QTEST_MAIN(DocumentWindowTest)
#include "documentwindowtest.moc"
