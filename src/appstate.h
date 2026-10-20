// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Application-wide facts the UI shows: where data lives and whether the database opened.
class AppState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(QString databaseError READ databaseError CONSTANT)

public:
    static AppState *instance();
    static AppState *create(QQmlEngine *, QJSEngine *);

    QString databasePath() const { return m_databasePath; }
    QString databaseError() const { return m_databaseError; }

    // Called once from main() before QML loads.
    void setDatabase(const QString &path, const QString &error);

private:
    explicit AppState(QObject *parent = nullptr);
    QString m_databasePath;
    QString m_databaseError;
};
