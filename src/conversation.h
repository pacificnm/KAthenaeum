// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "core/answerer.h"

// One chat on the Ask page: its messages, and the question being answered now. Questions and answers are saved as they
// finish; leaving the page (or quitting) during an answer saves what was written as stopped.
class Conversation : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qint64 chatId READ chatId WRITE setChatId NOTIFY chatIdChanged)
    Q_PROPERTY(QString title READ title NOTIFY chatIdChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // A chat about one article answers from it alone. Set before the first question (the article panel does); a saved
    // chat brings its own.
    Q_PROPERTY(qint64 articleLibraryId READ articleLibraryId WRITE setArticleLibraryId NOTIFY articleChanged)
    Q_PROPERTY(QString articlePath READ articlePath WRITE setArticlePath NOTIFY articleChanged)
    Q_PROPERTY(QString articleTitle READ articleTitle WRITE setArticleTitle NOTIFY articleChanged)

public:
    enum Role {
        RoleRole = Qt::UserRole + 1,  // user | assistant
        TextRole,       // the question as typed, or the answer as Markdown with citation links
        StatusRole,     // working | done | stopped | error
        PhaseRole,      // what is happening now, while working
        ErrorRole,
        NoteRole,       // e.g. why the sources were ranked by words only
        SourcesRole,    // [{n, libraryId, libraryTitle, path, title, section, snippet}]
        CitationsRole,  // [{start, end, sources, score, reason, numbers}]
        StatsRole,      // a line of timings
    };
    Q_ENUM(Role)

    explicit Conversation(QObject *parent = nullptr);
    ~Conversation() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 chatId() const { return m_chatId; }
    void setChatId(qint64 id);
    QString title() const { return m_title; }
    bool busy() const;
    qint64 articleLibraryId() const { return m_article.libraryId; }
    void setArticleLibraryId(qint64 id);
    QString articlePath() const { return m_article.path; }
    void setArticlePath(const QString &path);
    QString articleTitle() const { return m_article.title; }
    void setArticleTitle(const QString &title);

    Q_INVOKABLE void ask(const QString &question);
    Q_INVOKABLE void stop();
    // A source of an answer, by number: {libraryId, path, title, section, libraryTitle, available}
    Q_INVOKABLE QVariantMap source(int row, int n) const;
    // Why a sentence was flagged (the i of a check:i link)
    Q_INVOKABLE QString flagReason(int row, int citation) const;

Q_SIGNALS:
    void chatIdChanged();
    void busyChanged();
    void articleChanged();
    // A chat was created or a message saved: the Chats list is out of date
    void saved();

private:
    struct Message {
        qint64 id = 0;
        QString role;
        QString content;  // raw: what was typed, or the answer as the model wrote it (tidied)
        QString status = QStringLiteral("done");
        QString phase;
        QString error;
        QString note;
        QVariantList sources;
        QVariantList citations;
        QVariantMap meta;
    };

    void load();
    void append(const Message &m);
    void changed(int row);
    void finished(const AnswerResult &result);
    QString displayText(const Message &m) const;
    QString statsLine(const Message &m) const;

    Answerer *m_answerer;
    qint64 m_chatId = 0;
    ArticleScope m_article;
    QString m_title;
    QList<Message> m_messages;
    int m_answering = -1;  // the row being answered
};
