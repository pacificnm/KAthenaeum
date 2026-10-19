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

public:
    enum Role { ChatIdRole = Qt::UserRole + 1, TitleRole, UpdatedRole, QuestionsRole };
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

private:
    QList<ChatRecord> m_chats;
};
