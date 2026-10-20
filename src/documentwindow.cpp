// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentwindow.h"

#include "core/projects.h"
#include "core/settings.h"
#include "documentmodel.h"
#include "projectlist.h"

#include <KLocalizedString>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QQmlEngine>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

QVariant DocumentPreview::loadResource(int, const QUrl &)
{
    return {};  // a document may quote anything: its preview loads no images, from the web or from disk
}

DocumentWindow::DocumentWindow(qint64 documentId)
    : m_model(new DocumentModel(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);  // closing the main window quits, whatever documents are open
    m_model->setDocumentId(documentId);

    // Kate's editor, Markdown highlighted
    m_document = KTextEditor::Editor::instance()->createDocument(this);
    m_document->setText(m_model->content());
    m_document->setHighlightingMode(QStringLiteral("Markdown"));
    m_document->setModified(false);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_view = m_document->createView(m_splitter);
    m_preview = new DocumentPreview(m_splitter);
    m_preview->setOpenLinks(false);
    m_preview->document()->setDocumentMargin(24);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    // the toolbar: title, cite, preview, editor settings, export, saved
    auto *bar = addToolBar(i18n("Document"));
    bar->setObjectName(QStringLiteral("document"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_title = new QLineEdit(m_model->title(), bar);
    m_title->setPlaceholderText(i18n("Title"));
    m_title->setMinimumWidth(240);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    bar->addWidget(m_title);

    m_cite = new QMenu(this);
    auto *cite = new QToolButton(bar);
    cite->setText(i18n("Cite"));
    cite->setIcon(QIcon::fromTheme(QStringLiteral("insert-link")));
    cite->setToolTip(i18n("Insert a link to one of the project's articles, or quote a saved excerpt"));
    cite->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    cite->setPopupMode(QToolButton::InstantPopup);
    cite->setMenu(m_cite);
    bar->addWidget(cite);
    connect(m_cite, &QMenu::aboutToShow, this, &DocumentWindow::fillCiteMenu);

    QAction *preview = bar->addAction(QIcon::fromTheme(QStringLiteral("document-preview")), i18n("Preview"));
    preview->setCheckable(true);
    preview->setChecked(true);
    preview->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(preview, &QAction::toggled, m_preview, &QWidget::setVisible);

    QAction *exportAction = bar->addAction(QIcon::fromTheme(QStringLiteral("document-export")), i18n("Export…"));
    connect(exportAction, &QAction::triggered, this, &DocumentWindow::exportDocument);

    QAction *configure = bar->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure Editor…"));
    connect(configure, &QAction::triggered, this, [this] {
        KTextEditor::Editor::instance()->configDialog(this);  // Kate's own settings: fonts, colours, spell check, vi mode
    });

    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    m_status = new QLabel(bar);
    m_status->setContentsMargins(8, 0, 8, 0);
    bar->addWidget(m_status);

    // edits go to the document model, which saves them (900 ms after typing stops, and on closing)
    connect(m_document, &KTextEditor::Document::textChanged, this, [this] {
        m_model->setContent(m_document->text());
        m_previewTimer.start();
    });
    connect(m_title, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_model->setTitle(t);
        refreshTitle();
        m_previewTimer.start();
    });
    connect(m_model, &DocumentModel::savedChanged, this, [this] {
        if (!m_model->error().isEmpty()) {
            m_status->setText(m_model->error());
            m_status->setStyleSheet(QStringLiteral("color: #da4453"));  // Breeze's negative text
        } else {
            m_status->setText(m_model->saved() ? i18n("Saved") : i18n("Saving…"));
            m_status->setStyleSheet(QString());
        }
    });
    Q_EMIT m_model->savedChanged();

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(250);
    connect(&m_previewTimer, &QTimer::timeout, this, &DocumentWindow::refreshPreview);
    connect(m_preview, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("zim"))
            Q_EMIT openArticle(url);
        else if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("mailto"))
            QDesktopServices::openUrl(url);
    });
    connect(ProjectsNotifier::instance(), &ProjectsNotifier::changed, this, &DocumentWindow::projectChanged);

    refreshTitle();
    refreshPreview();
    resize(1200, 800);
    m_view->setFocus();
}

DocumentWindow::~DocumentWindow() = default;

qint64 DocumentWindow::documentId() const
{
    return m_model->documentId();
}

void DocumentWindow::refreshTitle()
{
    const auto project = Projects::byId(m_model->projectId());
    setWindowTitle(project ? i18nc("document — project", "%1 — %2", m_model->title(), project->title) : m_model->title());
}

void DocumentWindow::refreshPreview()
{
    const int scroll = m_preview->verticalScrollBar()->value();
    // the project's sources' notes shown beside where they are cited (the stored text is not changed)
    m_preview->setMarkdown(QStringLiteral("# %1\n\n%2").arg(m_model->title(), Projects::previewMarkdown(m_model->content(), Projects::items(m_model->projectId()))));
    m_preview->verticalScrollBar()->setValue(scroll);
}

void DocumentWindow::fillCiteMenu()
{
    m_cite->clear();
    // the articles this document links to, cited in the chosen style (Settings › Reading), at the cursor
    const Citations::Style style = Citations::style(AppSettings::citationStyle());
    QAction *list = m_cite->addAction(QIcon::fromTheme(QStringLiteral("format-list-unordered")),
                                      i18n("Insert %1 (%2)", Citations::listHeading(style), Citations::label(style)));
    list->setToolTip(i18n("A list of the articles this document links to; Export then keeps it instead of adding its own"));
    list->setEnabled(!Projects::citedSources(m_document->text()).isEmpty());
    connect(list, &QAction::triggered, this, [this, style] {
        insert(QStringLiteral("\n\n") + Projects::sourceList(m_document->text(), style));
    });
    m_cite->addSeparator();
    const QList<ProjectItem> items = Projects::items(m_model->projectId());
    auto shorten = [](QString s, int n) {
        s = s.simplified();
        return s.size() > n ? s.left(n - 1) + QStringLiteral("…") : s;
    };
    bool any = false;
    for (const ProjectItem &i : items) {
        if (i.kind != QLatin1String("article"))
            continue;
        QAction *a = m_cite->addAction(QIcon::fromTheme(QStringLiteral("text-html")),
                                       i.note.isEmpty() ? i.article.title : i18n("%1 (has a note)", i.article.title));
        a->setToolTip(i.note);
        connect(a, &QAction::triggered, this, [this, i] {
            insert(Projects::articleLink(i.article));
        });
        any = true;
    }
    bool separated = false;
    for (const ProjectItem &i : items) {
        if (i.kind != QLatin1String("clip"))
            continue;
        if (any && !separated) {
            m_cite->addSection(i18n("Saved excerpts"));
            separated = true;
        }
        QAction *a = m_cite->addAction(QIcon::fromTheme(QStringLiteral("format-text-blockquote")), QStringLiteral("“%1”").arg(shorten(i.text, 70)));
        connect(a, &QAction::triggered, this, [this, i] {
            insert(Projects::quote(i));
        });
        any = true;
    }
    if (!any)
        m_cite->addAction(i18n("The project has no articles or excerpts yet"))->setEnabled(false);
}

void DocumentWindow::insert(const QString &markdown)
{
    QString text = markdown;
    if (text.startsWith(QLatin1Char('>'))) {  // a quotation is a block of its own
        const KTextEditor::Cursor at = m_view->cursorPosition();
        const bool lineHasText = !m_document->line(at.line()).trimmed().isEmpty();
        text = (lineHasText ? QStringLiteral("\n\n") : QString()) + text + QStringLiteral("\n\n");
    }
    m_view->insertText(text);
    m_view->setFocus();
}

void DocumentWindow::exportDocument()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, i18n("Export Document"), folder + QLatin1Char('/') + m_model->exportName(),
                                                      i18n("Markdown (*.md)"));
    if (path.isEmpty())
        return;
    m_model->save();
    const QString error = m_model->exportTo(QUrl::fromLocalFile(path));
    statusBar()->showMessage(error.isEmpty() ? i18n("Exported to %1", path) : error, 8000);
}

void DocumentWindow::projectChanged()
{
    if (!Projects::document(m_model->documentId())) {  // deleted, or its project was
        m_document->setReadWrite(false);
        close();
        return;
    }
    refreshTitle();
    m_previewTimer.start();  // a note may have changed
}

void DocumentWindow::closeEvent(QCloseEvent *event)
{
    m_model->save();  // what was typed is kept (it says so if it could not be)
    event->accept();
}

// ---- the windows ----

namespace
{
DocumentWindows *s_windows = nullptr;
}

DocumentWindows::DocumentWindows(QObject *parent)
    : QObject(parent)
{
    // the main window has closed and the app is quitting: the documents' windows go too (each saves on closing)
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &DocumentWindows::closeAll);
}

DocumentWindows *DocumentWindows::instance()
{
    if (!s_windows)
        s_windows = new DocumentWindows(QCoreApplication::instance());
    return s_windows;
}

DocumentWindows *DocumentWindows::create(QQmlEngine *, QJSEngine *)
{
    DocumentWindows *w = instance();
    QQmlEngine::setObjectOwnership(w, QQmlEngine::CppOwnership);
    return w;
}

void DocumentWindows::open(qint64 documentId)
{
    if (const auto it = m_windows.constFind(documentId); it != m_windows.cend() && *it) {
        (*it)->show();
        (*it)->raise();
        (*it)->activateWindow();
        return;
    }
    if (!Projects::document(documentId))
        return;
    auto *w = new DocumentWindow(documentId);
    connect(w, &DocumentWindow::openArticle, this, &DocumentWindows::openArticle);
    m_windows.insert(documentId, w);
    w->show();
}

void DocumentWindows::closeAll()
{
    for (const QPointer<DocumentWindow> &w : std::as_const(m_windows))
        if (w)
            w->close();
    m_windows.clear();
}
