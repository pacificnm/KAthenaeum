// SPDX-License-Identifier: GPL-3.0-or-later
#include "chatlist.h"

ChatList::ChatList(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(this, &ChatList::filterChanged, this, &ChatList::refresh);
    refresh();
}

int ChatList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_chats.size());
}

QHash<int, QByteArray> ChatList::roleNames() const
{
    return {{ChatIdRole, "chatId"},
            {TitleRole, "title"},
            {UpdatedRole, "updated"},
            {QuestionsRole, "questions"},
            {ArticleLibraryIdRole, "articleLibraryId"},
            {ArticlePathRole, "articlePath"},
            {ArticleTitleRole, "articleTitle"}};
}

QVariant ChatList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_chats.size())
        return {};
    const ChatRecord &c = m_chats.at(index.row());
    switch (role) {
    case ChatIdRole:
        return c.id;
    case TitleRole:
        return c.title;
    case UpdatedRole:
        return c.updatedAt.toLocalTime();
    case QuestionsRole:
        return c.questions;
    case ArticleLibraryIdRole:
        return c.articleLibraryId;
    case ArticlePathRole:
        return c.articlePath;
    case ArticleTitleRole:
        return c.articleTitle;
    }
    return {};
}

void ChatList::refresh()
{
    beginResetModel();
    m_chats = m_libraryId > 0 && !m_path.isEmpty() ? Chats::aboutArticle(m_libraryId, m_path) : Chats::all();
    endResetModel();
    Q_EMIT countChanged();
}

bool ChatList::remove(qint64 chatId)
{
    const bool ok = Chats::remove(chatId);
    refresh();
    return ok;
}

bool ChatList::rename(qint64 chatId, const QString &title)
{
    if (title.trimmed().isEmpty())
        return false;
    const bool ok = Chats::rename(chatId, title);
    refresh();
    return ok;
}
