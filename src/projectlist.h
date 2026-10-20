// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/projects.h"

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Research projects, most recently used first; and adding to a project from anywhere (an article, a selection, an
// answer, a chat). Every instance refreshes when any of them changes a project.
class ProjectList : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { ProjectIdRole = Qt::UserRole + 1, TitleRole, DescriptionRole, UpdatedRole, ArticlesRole, ClipsRole, ChatsRole, DocumentsRole };
    Q_ENUM(Role)

    explicit ProjectList(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    // The new project's id, or 0 (an empty title)
    Q_INVOKABLE qint64 create(const QString &title, const QString &description = {});
    Q_INVOKABLE bool remove(qint64 projectId);
    // [projectId…] of the projects that have this article
    Q_INVOKABLE QVariantList containing(qint64 libraryId, const QString &path) const;

    Q_INVOKABLE bool addArticle(qint64 projectId, qint64 libraryId, const QString &path, const QString &title, const QString &note = {});
    Q_INVOKABLE bool addExcerpt(qint64 projectId, const QString &text, qint64 libraryId, const QString &path, const QString &title,
                                const QString &section = {}, const QString &note = {});
    // An answer, kept as an excerpt that names the articles it was drawn from (sources: the chat's [{libraryId, path, title}])
    Q_INVOKABLE bool addAnswer(qint64 projectId, const QString &text, const QVariantList &sources, const QString &note = {});
    Q_INVOKABLE bool attachChat(qint64 projectId, qint64 chatId);
    Q_INVOKABLE QString title(qint64 projectId) const;
    // [{projectId, title}], most recently used first (for pickers)
    Q_INVOKABLE QVariantList list() const;

Q_SIGNALS:
    void countChanged();

private:
    QList<ProjectRecord> m_projects;
};

// Tells every ProjectList and Project that something changed
class ProjectsNotifier : public QObject
{
    Q_OBJECT
public:
    static ProjectsNotifier *instance();
Q_SIGNALS:
    void changed();
    void documentSaved(qint64 projectId);
};
