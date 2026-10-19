// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the core library: open libzim archives, shared by ZimReader and ZimSearch. Never include this outside
// src/core (libzim throws, and only the core library is built with exceptions).

#include <QtGlobal>
#include <memory>
#include <zim/archive.h>

namespace ZimArchives
{
// Opens the library's file on first use. Reads the database, so: UI thread only.
std::shared_ptr<zim::Archive> get(qint64 libraryId);
// An archive already opened by get(), or null. Never opens anything, so it is safe from worker threads (a libzim
// Archive itself is safe to read from several threads).
std::shared_ptr<zim::Archive> opened(qint64 libraryId);
void close(qint64 libraryId);
}
