// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>

// Small key/value settings kept in the database's `settings` table.
namespace AppSettings
{
QString value(const QString &key, const QString &fallback = {});
bool setValue(const QString &key, const QString &value);

// Where downloaded libraries go. Default: <AppDataLocation>/zim. Libraries added from elsewhere stay where they are.
QString libraryFolder();
QString defaultLibraryFolder();
bool setLibraryFolder(const QString &folder);

// Ollama and the two models Ask uses (see CLAUDE.md, "Models")
QString ollamaUrl();       // default http://localhost:11434
QString answerModel();     // default qwen3.5:2b
QString embeddingModel();  // default nomic-embed-text

// Library updates: checked against the Kiwix catalog once a week (on by default), and when that last happened
bool autoCheckLibraryUpdates();
bool setAutoCheckLibraryUpdates(bool on);
QDateTime lastLibraryUpdateCheck();  // invalid if never
bool setLastLibraryUpdateCheck(const QDateTime &when);

// Articles across the whole window (the default), or in a column of comfortable reading width
bool articleFullWidth();
bool setArticleFullWidth(bool full);
}
