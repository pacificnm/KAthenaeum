// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class Drafter;

// An AI draft for a project page: runs it, shows progress, and saves the result as a new document of the project
class DraftController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Drafting)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(int step READ step NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(QString label READ label NOTIFY stateChanged)
    Q_PROPERTY(QString streamed READ streamed NOTIFY stateChanged)  // chat notes as they are written

public:
    explicit DraftController(QObject *parent = nullptr);

    bool running() const;
    int step() const { return m_step; }
    int total() const { return m_total; }
    QString label() const { return m_label; }
    QString streamed() const { return m_streamed; }

    Q_INVOKABLE void summarizeProject(qint64 projectId);
    Q_INVOKABLE void summarizeChat(qint64 projectId, qint64 chatId);
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void stateChanged();
    void documentReady(qint64 documentId);
    void failed(const QString &message);

private:
    Drafter *m_drafter;
    qint64 m_projectId = 0;
    int m_step = 0;
    int m_total = 0;
    QString m_label;
    QString m_streamed;
};
