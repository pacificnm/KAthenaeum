// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "articlerenderer.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// A passage the reader marked in an article, in a colour, with an optional note (table highlights, schema v9). Not tied
// to a project: a saved excerpt is (see Projects).
struct HighlightRecord {
    qint64 id = 0;
    qint64 libraryId = 0;
    QString path;
    QString title;  // the article's
    QString text;
    QString color;  // one of Highlights::colors()
    QString note;
    QDateTime createdAt;
    QDateTime updatedAt;
};

namespace Highlights
{
QStringList colors();  // yellow, green, blue, pink: the first is the default
// The same passage highlighted again in the same article keeps one highlight: its colour changes, and a note given
// replaces the old one (an empty one does not). Returns its id, or 0 (empty or too short to find again in the page).
qint64 add(qint64 libraryId, const QString &path, const QString &title, const QString &text, const QString &color = {}, const QString &note = {});
bool setNote(qint64 id, const QString &note);
bool setColor(qint64 id, const QString &color);
bool remove(qint64 id);
std::optional<HighlightRecord> byId(qint64 id);
QList<HighlightRecord> forArticle(qint64 libraryId, const QString &path);  // in the order they were made
// Every highlight, grouped by article (the article changed most recently first), each article's in the order they were
// made. `color` empty for all; `text` matches the passage, the note or the article's title.
QList<HighlightRecord> all(const QString &color = {}, const QString &text = {});
int count();
// How the article page marks them: <mark class="hl hl-yellow" id="hl-12" title="the note">
QList<Highlight> marksFor(qint64 libraryId, const QString &path);
// As Markdown, by article: a heading linking to it (zim://), each passage quoted with its note under it
QString markdown(const QList<HighlightRecord> &highlights);
// Too short to place in the page again reliably (fewer than 8 letters that are not spaces)
bool tooShort(const QString &text);
}
