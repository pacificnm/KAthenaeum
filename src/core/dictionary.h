// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

struct LibraryRecord;

// Looking a word up in an installed Wiktionary. The entry comes from the ZIM's page for the word, read with libxml2:
// the full English Wiktionary groups an entry by language (h2 "English"), then by part of speech (h3/h4 "Noun"); the
// Simple English one has no language level (h2 "Noun"). Each part of speech is followed by a numbered list whose items
// are the senses, with examples in a <dl> and synonyms and quotations nested inside them.
namespace Dictionary
{
struct Sense {
    QString definition;
    QString example;  // the first usage example, if there is one
};
struct PartOfSpeech {
    QString name;  // "Noun"
    QList<Sense> senses;
};
struct Entry {
    QString word;
    QString pronunciation;  // the first IPA ("/ˈhæpi/")
    QList<PartOfSpeech> parts;
};

// The entry for `language` ("English") in a Wiktionary page; nothing if the page has no definitions for it. At most
// `maxParts` parts of speech and `maxSenses` senses in each.
std::optional<Entry> parse(const QByteArray &html, const QString &language = QStringLiteral("English"), int maxParts = 4, int maxSenses = 4);

// When every sense only points at another word ("The plural form of molecule", "The simple past of run"): that word,
// whose entry says what it means. Empty otherwise.
QString baseForm(const Entry &entry);

// The pages to try for a word as it was selected: in lower case, as written, with a capital, then without an ending
// ("Running" -> running, Running, run…, "studies" -> study); a word with capitals inside ("NASA") as written first.
// Punctuation round it is dropped.
QStringList candidates(const QString &selected);

// Is this library a dictionary (a Wiktionary)?
bool isDictionary(const LibraryRecord &library);
}
