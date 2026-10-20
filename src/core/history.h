// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include "zimreader.h"

// Articles read, per library, for "Recently read". Rows go when their library is removed.
namespace History
{
void record(qint64 libraryId, const QString &path, const QString &title);
QList<ZimEntry> recent(qint64 libraryId, int limit);
}
