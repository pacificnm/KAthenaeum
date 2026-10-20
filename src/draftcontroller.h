// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Drafter;

// AI drafts: runs one at a time, shows progress, and saves the result as a new document of its project. One for the
// whole app, not one per project page: leaving the page during a draft threw the draft away (the review, 2026-10-19).
class DraftController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Drafting)
    QML_SINGLETON
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(qint64 projectId READ projectId NOTIFY stateChanged)  // the project being drafted for
    Q_PROPERTY(int step READ step NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(QString label READ label NOTIFY stateChanged)
    Q_PROPERTY(QString streamed READ streamed NOTIFY stateChanged)  // chat notes as they are written

public:
    explicit DraftController(QObject *parent = nullptr);
    qint64 projectId() const { return m_projectId; }

    bool running() const;
    int step() const { return m_step; }
    int total() const { return m_total; }
    QString label() const { return m_label; }
    QString streamed() const { return m_streamed; }

    Q_INVOKABLE void summarizeProject(qint64 projectId);
    Q_INVOKABLE void summarizeChat(qint64 projectId, qint64 chatId);
    // A report to the user's instructions: `full` for up to 6 longer sections, else about 3
    Q_INVOKABLE void writeReport(qint64 projectId, const QString &title, const QString &instructions, bool full, bool alsoLibrary);
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void stateChanged();
    void documentReady(qint64 documentId, qint64 projectId, const QString &title);
    void failed(const QString &message);

private:
    Drafter *m_drafter;
    qint64 m_projectId = 0;
    int m_step = 0;
    int m_total = 0;
    QString m_label;
    QString m_streamed;
};
