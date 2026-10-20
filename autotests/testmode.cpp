// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
// Linked into every test: QStandardPaths points at test folders (~/.qttest), never at the user's own data, before a
// test's first line runs

#include <QCoreApplication>
#include <QStandardPaths>

namespace
{
void enableTestMode()
{
    QStandardPaths::setTestModeEnabled(true);
}
}

Q_COREAPP_STARTUP_FUNCTION(enableTestMode)
