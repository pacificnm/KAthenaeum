// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// One Markdown document of a project, as its editor sees it. Changes are saved 900 ms after typing stops (Athenaeum's
// rule), and when the editor goes away.
class DocumentModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProjectDocument)
    Q_PROPERTY(qint64 documentId READ documentId WRITE setDocumentId NOTIFY loaded)
    Q_PROPERTY(qint64 projectId READ projectId NOTIFY loaded)
    Q_PROPERTY(bool exists READ exists NOTIFY loaded)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString content READ content WRITE setContent NOTIFY contentChanged)
    Q_PROPERTY(bool saved READ saved NOTIFY savedChanged)
    Q_PROPERTY(QString error READ error NOTIFY savedChanged)

public:
    explicit DocumentModel(QObject *parent = nullptr);
    ~DocumentModel() override;

    qint64 documentId() const
    {
        return m_id;
    }
    void setDocumentId(qint64 id);
    qint64 projectId() const
    {
        return m_projectId;
    }
    bool exists() const
    {
        return m_exists;
    }
    QString title() const
    {
        return m_title;
    }
    void setTitle(const QString &title);
    QString content() const
    {
        return m_content;
    }
    void setContent(const QString &content);
    bool saved() const
    {
        return !m_dirty;
    }
    QString error() const
    {
        return m_error;
    }

    Q_INVOKABLE void save();
    // The document as Markdown for outside the app (links to articles become a References list); an error, or empty
    Q_INVOKABLE QString exportTo(const QUrl &file) const;
    Q_INVOKABLE QString exportName() const;

Q_SIGNALS:
    void loaded();
    void titleChanged();
    void contentChanged();
    void savedChanged();

private:
    void changedByUser();

    qint64 m_id = 0;
    qint64 m_projectId = 0;
    bool m_exists = false;
    QString m_title;
    QString m_content;
    QString m_error;
    QTimer m_timer;
    bool m_dirty = false; // typed and not yet saved successfully (a failed save keeps it set, so leaving tries again)
};
