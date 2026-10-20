// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// A passage of an article's prose, with the heading of the section it is in (empty for the introduction).
struct Passage {
    QString section;
    QString text;
};

struct ExtractedArticle {
    QString title;
    QList<Passage> passages;
};

// Turns a mwoffliner Wikipedia page into prose passages: a port of Athenaeum's extract.py (same rules, same parser
// family). Tables, figures, citations, maths and reference-like sections are dropped; passages are at most MaxChars and
// short ones are merged into their neighbours. Search snippets and Ask both read articles through this. Thread-safe.
namespace Extract
{
constexpr int MaxChars = 1400; // ~350 tokens: comfortably inside the embedding model's context
constexpr int MinChars = 40;

// Unparseable input gives an empty result, never an error
ExtractedArticle extract(const QByteArray &raw);

// Breaks an over-long block on sentence boundaries (hard-cut as a last resort)
QStringList splitLong(const QString &text);

// The section name given to an article's infobox passage ("Key facts"), shown to the user as a source's section
QString keyFactsSection();
}

// libxml2 must be initialised once, on one thread, before threads use it. Every core entry point that parses HTML calls
// this; it is cheap after the first time.
void initXmlParser();
