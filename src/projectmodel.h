// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/projects.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// One research project for its page: title and goal, sources (articles, saved excerpts), chats (started in it or
// attached), documents
class ProjectModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Project)
    Q_PROPERTY(qint64 projectId READ projectId WRITE setProjectId NOTIFY changed)
    Q_PROPERTY(bool exists READ exists NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    // [{itemId, libraryId, path, title, libraryTitle, note, available}]
    Q_PROPERTY(QVariantList articles READ articles NOTIFY changed)
    // [{itemId, text, title, section, libraryId, path, note, available}]
    Q_PROPERTY(QVariantList excerpts READ excerpts NOTIFY changed)
    // [{chatId, title, updated, questions, itemId (0: started in the project)}]
    Q_PROPERTY(QVariantList chats READ chats NOTIFY changed)
    // [{documentId, title, updated, characters}]
    Q_PROPERTY(QVariantList documents READ documents NOTIFY changed)

public:
    explicit ProjectModel(QObject *parent = nullptr);

    qint64 projectId() const { return m_id; }
    void setProjectId(qint64 id);
    bool exists() const { return m_exists; }
    QString title() const { return m_title; }
    QString description() const { return m_description; }
    QVariantList articles() const { return m_articles; }
    QVariantList excerpts() const { return m_excerpts; }
    QVariantList chats() const { return m_chats; }
    QVariantList documents() const { return m_documents; }

    Q_INVOKABLE bool update(const QString &title, const QString &description);
    Q_INVOKABLE bool setNote(qint64 itemId, const QString &note);
    Q_INVOKABLE bool removeItem(qint64 itemId);
    Q_INVOKABLE qint64 createDocument(const QString &title);
    Q_INVOKABLE bool removeDocument(qint64 documentId);
    // Markdown to insert into a document: a link to a source article, or a saved excerpt as a quotation
    Q_INVOKABLE QString articleLink(qint64 itemId) const;
    Q_INVOKABLE QString quote(qint64 itemId) const;
    // The whole project as one Markdown file; an error message, or empty on success
    Q_INVOKABLE QString exportTo(const QUrl &file) const;
    Q_INVOKABLE QString exportName() const;  // "water.md"
    // The project's articles cited in each style, sorted: [{style, label, text, html}], one entry per paragraph
    Q_INVOKABLE QVariantList bibliography() const;
    // A document's Markdown for its preview, with the notes of the sources it cites
    Q_INVOKABLE QString preview(const QString &markdown) const;

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void changed();

private:
    qint64 m_id = 0;
    bool m_exists = false;
    QString m_title;
    QString m_description;
    QVariantList m_articles;
    QVariantList m_excerpts;
    QVariantList m_chats;
    QVariantList m_documents;
    QList<ProjectItem> m_items;
};

// Writes text to a local file atomically; an error message, or empty on success
QString writeTextFile(const QUrl &file, const QString &text);
