// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "appstate.h"

#include <QQmlEngine>

AppState::AppState(QObject *parent)
    : QObject(parent)
{
}

AppState *AppState::instance()
{
    static AppState state;
    return &state;
}

AppState *AppState::create(QQmlEngine *, QJSEngine *)
{
    auto *state = instance();
    QQmlEngine::setObjectOwnership(state, QQmlEngine::CppOwnership);  // a static: QML must never delete it
    return state;
}

void AppState::setDatabase(const QString &path, const QString &error)
{
    m_databasePath = path;
    m_databaseError = error;
}
