// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chats.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// Saved chats, most recently used first, for the Chats page
class ChatList : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // Only the chats about this article (both set), or every chat
    Q_PROPERTY(qint64 articleLibraryId MEMBER m_libraryId NOTIFY filterChanged)
    Q_PROPERTY(QString articlePath MEMBER m_path NOTIFY filterChanged)

public:
    enum Role { ChatIdRole = Qt::UserRole + 1, TitleRole, UpdatedRole, QuestionsRole, ArticleLibraryIdRole, ArticlePathRole, ArticleTitleRole,
                ProjectIdRole, ProjectTitleRole };
    Q_ENUM(Role)

    explicit ChatList(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool remove(qint64 chatId);
    Q_INVOKABLE bool rename(qint64 chatId, const QString &title);

Q_SIGNALS:
    void countChanged();
    void filterChanged();

private:
    QList<ChatRecord> m_chats;
    qint64 m_libraryId = 0;
    QString m_path;
    QHash<qint64, QString> m_projectTitles;
};
