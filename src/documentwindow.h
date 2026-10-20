// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QTextBrowser>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class DocumentModel;
class QLabel;
class QLineEdit;
class QMenu;
class QQmlEngine;
class QJSEngine;
class QSplitter;
class QToolButton;
namespace KTextEditor
{
class Document;
class View;
}

// The preview: Markdown as rich text, fetching nothing (images, anywhere), links handed to the window
class DocumentPreview : public QTextBrowser
{
    Q_OBJECT
public:
    using QTextBrowser::QTextBrowser;
    QVariant loadResource(int type, const QUrl &name) override;
};

// A project document in a window of its own: Kate's editor (KTextEditor) beside a live Markdown preview. Widgets, not
// QML, because Kate's editor is a widget, and embedded in a QML page it covered every menu, notification and dialog drawn
// over it (tried 2026-10-19).
class DocumentWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DocumentWindow(qint64 documentId);
    ~DocumentWindow() override;

    qint64 documentId() const;

Q_SIGNALS:
    void openArticle(const QUrl &url);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void refreshPreview();
    void refreshTitle();
    void fillCiteMenu();
    void insert(const QString &markdown);
    void exportDocument();
    void projectChanged();

    DocumentModel *m_model;
    KTextEditor::Document *m_document;
    KTextEditor::View *m_view;
    DocumentPreview *m_preview;
    QSplitter *m_splitter;
    QLineEdit *m_title;
    QLabel *m_status;
    QMenu *m_cite;
    QTimer m_previewTimer;
};

// Opens documents in their windows, one window per document: opening one that is already open brings it forward
class DocumentWindows : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static DocumentWindows *create(QQmlEngine *, QJSEngine *);
    static DocumentWindows *instance();

    Q_INVOKABLE void open(qint64 documentId);
    void closeAll();

Q_SIGNALS:
    // a link in a preview: show the article in the main window
    void openArticle(const QUrl &url);

private:
    explicit DocumentWindows(QObject *parent);
    QHash<qint64, QPointer<DocumentWindow>> m_windows;
};
