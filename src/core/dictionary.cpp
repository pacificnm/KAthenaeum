// SPDX-License-Identifier: GPL-3.0-or-later
#include "dictionary.h"

#include "extract.h"
#include "library.h"

#include <QRegularExpression>
#include <QSet>

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include <algorithm>
#include <functional>

namespace
{
// The headings Wiktionary puts a part of speech under (in lower case)
const QSet<QString> PartsOfSpeech = {
    QStringLiteral("noun"), QStringLiteral("proper noun"), QStringLiteral("verb"), QStringLiteral("adjective"), QStringLiteral("adverb"),
    QStringLiteral("pronoun"), QStringLiteral("preposition"), QStringLiteral("conjunction"), QStringLiteral("interjection"),
    QStringLiteral("determiner"), QStringLiteral("article"), QStringLiteral("numeral"), QStringLiteral("number"),
    QStringLiteral("cardinal number"), QStringLiteral("ordinal number"), QStringLiteral("phrase"), QStringLiteral("prepositional phrase"),
    QStringLiteral("prefix"), QStringLiteral("suffix"), QStringLiteral("affix"), QStringLiteral("abbreviation"), QStringLiteral("contraction"),
    QStringLiteral("particle"), QStringLiteral("idiom"), QStringLiteral("proverb"), QStringLiteral("symbol"), QStringLiteral("letter"),
    QStringLiteral("initialism"), QStringLiteral("acronym"), QStringLiteral("participle"),
};

QString nameOf(xmlNodePtr n)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(n->name)).toLower();
}

QString attr(xmlNodePtr n, const char *name)
{
    xmlChar *v = xmlGetProp(n, reinterpret_cast<const xmlChar *>(name));
    if (!v)
        return {};
    QString s = QString::fromUtf8(reinterpret_cast<const char *>(v));
    xmlFree(v);
    return s;
}

bool hasClass(xmlNodePtr n, const char *part)
{
    return attr(n, "class").contains(QLatin1String(part));
}

// The text of a node, leaving out what is not the definition itself: nested senses, examples, synonyms, quotations
QString ownText(xmlNodePtr n)
{
    QString out;
    std::function<void(xmlNodePtr)> walk = [&](xmlNodePtr node) {
        for (xmlNodePtr c = node->children; c; c = c->next) {
            if (c->type == XML_TEXT_NODE && c->content) {
                out += QString::fromUtf8(reinterpret_cast<const char *>(c->content));
            } else if (c->type == XML_ELEMENT_NODE) {
                static const QSet<QString> skip = {QStringLiteral("ol"), QStringLiteral("ul"), QStringLiteral("dl"), QStringLiteral("style"),
                                                   QStringLiteral("script"), QStringLiteral("link"), QStringLiteral("table"), QStringLiteral("figure")};
                if (skip.contains(nameOf(c)) || hasClass(c, "nyms") || hasClass(c, "citation") || hasClass(c, "quotation")
                    || hasClass(c, "reference") || hasClass(c, "mw-ref"))
                    continue;
                walk(c);
            }
        }
    };
    walk(n);
    return out.simplified().replace(QStringLiteral(" ."), QStringLiteral(".")).replace(QStringLiteral(" ,"), QStringLiteral(","));
}

// The first usage example under a sense: a <dd> in its own <dl> that is not a list of synonyms
QString exampleOf(xmlNodePtr li)
{
    for (xmlNodePtr dl = li->children; dl; dl = dl->next) {
        if (dl->type != XML_ELEMENT_NODE || nameOf(dl) != QLatin1String("dl"))
            continue;
        for (xmlNodePtr dd = dl->children; dd; dd = dd->next) {
            if (dd->type != XML_ELEMENT_NODE || nameOf(dd) != QLatin1String("dd"))
                continue;
            bool synonyms = false;
            for (xmlNodePtr c = dd->children; c; c = c->next)
                if (c->type == XML_ELEMENT_NODE && hasClass(c, "nyms"))
                    synonyms = true;
            if (synonyms)
                continue;
            const QString text = ownText(dd);
            if (!text.isEmpty())
                return text;
        }
    }
    return {};
}

QString headingText(xmlNodePtr h)
{
    xmlChar *t = xmlNodeGetContent(h);
    QString s = t ? QString::fromUtf8(reinterpret_cast<const char *>(t)).simplified() : QString();
    if (t)
        xmlFree(t);
    return s;
}

// What matters on the page, in document order
struct Event {
    enum Kind { Heading, List, Ipa } kind;
    xmlNodePtr node;
    int level = 0;  // a heading's
    QString text;   // a heading's, or an IPA's
};

void collect(xmlNodePtr node, QList<Event> &out)
{
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE)
            continue;
        const QString n = nameOf(c);
        if (n.size() == 2 && n.at(0) == QLatin1Char('h') && n.at(1) >= QLatin1Char('2') && n.at(1) <= QLatin1Char('6')) {
            out.append({Event::Heading, c, n.at(1).digitValue(), headingText(c)});
        } else if (n == QLatin1String("ol")) {
            out.append({Event::List, c, 0, {}});  // its own items are the senses; nested lists are sub-senses
        } else if (n == QLatin1String("span") && attr(c, "class").split(QLatin1Char(' ')).contains(QLatin1String("IPA"))) {
            out.append({Event::Ipa, c, 0, headingText(c)});
        } else if (n != QLatin1String("table") && n != QLatin1String("style") && n != QLatin1String("script")) {
            collect(c, out);
        }
    }
}

struct Doc {
    htmlDocPtr doc = nullptr;
    ~Doc() { if (doc) xmlFreeDoc(doc); }
};
}

std::optional<Dictionary::Entry> Dictionary::parse(const QByteArray &html, const QString &language, int maxParts, int maxSenses)
{
    initXmlParser();
    if (html.trimmed().isEmpty())
        return std::nullopt;
    Doc d;
    d.doc = htmlReadMemory(html.constData(), int(html.size()), nullptr, "UTF-8",
                           HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (!d.doc)
        return std::nullopt;

    xmlNodePtr root = nullptr;
    {
        xmlXPathContextPtr ctx = xmlXPathNewContext(d.doc);
        xmlXPathObjectPtr r = xmlXPathEvalExpression(
            reinterpret_cast<const xmlChar *>("//div[contains(concat(' ', normalize-space(@class), ' '), ' mw-parser-output ')]|//body"), ctx);
        if (r && r->nodesetval && r->nodesetval->nodeNr > 0)
            root = r->nodesetval->nodeTab[0];  // document order: the wrapper is inside <body>, so <body> comes first
        if (r && r->nodesetval)
            for (int i = 0; i < r->nodesetval->nodeNr; ++i)
                if (nameOf(r->nodesetval->nodeTab[i]) == QLatin1String("div"))
                    root = r->nodesetval->nodeTab[i];
        if (r)
            xmlXPathFreeObject(r);
        xmlXPathFreeContext(ctx);
    }
    if (!root)
        return std::nullopt;

    QList<Event> events;
    collect(root, events);

    // The full Wiktionary has a heading per language: read only the one asked for. The Simple English one has none.
    // A page with neither the language nor a part of speech at the top level is another language's word only.
    bool byLanguage = false, partsOnTop = false;
    for (const Event &e : std::as_const(events)) {
        if (e.kind != Event::Heading || e.level != 2)
            continue;
        byLanguage = byLanguage || e.text.compare(language, Qt::CaseInsensitive) == 0;
        partsOnTop = partsOnTop || PartsOfSpeech.contains(e.text.toLower());
    }
    if (!byLanguage && !partsOnTop)
        return std::nullopt;
    bool active = !byLanguage;

    Entry entry;
    PartOfSpeech *current = nullptr;
    for (const Event &e : std::as_const(events)) {
        if (e.kind == Event::Heading) {
            if (byLanguage && e.level == 2) {
                active = e.text.compare(language, Qt::CaseInsensitive) == 0;
                current = nullptr;
                continue;
            }
            if (!active)
                continue;
            current = nullptr;
            if (PartsOfSpeech.contains(e.text.toLower()) && entry.parts.size() < maxParts) {
                entry.parts.append({e.text, {}});
                current = &entry.parts.last();
            }
        } else if (!active) {
            continue;
        } else if (e.kind == Event::Ipa) {
            if (entry.pronunciation.isEmpty())
                entry.pronunciation = e.text;
        } else if (current && current->senses.isEmpty()) {
            for (xmlNodePtr li = e.node->children; li && current->senses.size() < maxSenses; li = li->next) {
                if (li->type != XML_ELEMENT_NODE || nameOf(li) != QLatin1String("li"))
                    continue;
                const QString definition = ownText(li);
                if (!definition.isEmpty())
                    current->senses.append({definition, exampleOf(li)});
            }
        }
    }
    // a part of speech whose list was not found (or was empty) says nothing
    entry.parts.removeIf([](const PartOfSpeech &p) {
        return p.senses.isEmpty();
    });
    if (entry.parts.isEmpty())
        return std::nullopt;
    return entry;
}

QString Dictionary::baseForm(const Entry &entry)
{
    // "The plural form of molecule; more than one (kind of) molecule.", "The present participle of run.",
    // "(archaic) simple past tense of run": a kind of form near the start, "of", the word
    // labels first ("(archaic)"), then at most three words ("simple past", "third-person singular")
    static const QRegularExpression formOf(QStringLiteral("^(?:\\([^)]*\\)\\s*)*(?:(?:the|an?)\\s+)?(?:[\\w-]+\\s+){0,3}?"
                                                          "(?:form|plural|singular|participle|past|tense|comparative|superlative|spelling|"
                                                          "gerund|inflection)\\s+of\\s+([\\w'-]+)"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QString base;
    for (const PartOfSpeech &p : entry.parts)
        for (const Sense &s : p.senses) {
            const auto m = formOf.match(s.definition);
            if (!m.hasMatch())
                return {};  // a sense of its own
            if (base.isEmpty())
                base = m.captured(1);
            else if (base.compare(m.captured(1), Qt::CaseInsensitive) != 0)
                return {};  // forms of different words
        }
    return base;
}

QStringList Dictionary::candidates(const QString &selected)
{
    QString word = selected.simplified();
    auto trimmable = [](QChar c) {
        return c.isPunct() || c.isSymbol() || c.isSpace();
    };
    while (!word.isEmpty() && trimmable(word.front()))
        word.remove(0, 1);
    while (!word.isEmpty() && trimmable(word.back()))
        word.chop(1);
    if (word.isEmpty())
        return {};

    QStringList out;
    auto add = [&out](const QString &w) {
        if (w.size() >= 2 && !out.contains(w))
            out << w;
    };
    // the ordinary word first: a capital is usually just the start of a sentence ("Water is…"), and Wiktionary's capitalised
    // page is often a name. A word with capitals inside it ("NASA", "iPhone") is looked up as written first.
    const QString lower = word.toLower();
    const bool innerCapitals = std::any_of(word.cbegin() + 1, word.cend(), [](QChar c) {
        return c.isUpper();
    });
    if (innerCapitals)
        add(word);
    add(lower);
    add(word);
    add(lower.left(1).toUpper() + lower.mid(1));

    // without an ending: the word a form comes from ("studies" -> study, "running" -> run, "making" -> make)
    auto stem = [&add](const QString &base) {
        if (base.size() < 2)
            return;
        add(base);
        add(base + QLatin1Char('e'));
        if (base.size() >= 3 && base.back() == base.at(base.size() - 2))
            add(base.chopped(1));  // a doubled consonant: "running" -> runn -> run
    };
    if (!lower.contains(QLatin1Char(' '))) {
        if (lower.endsWith(QLatin1String("ies")) || lower.endsWith(QLatin1String("ied")))
            add(lower.chopped(3) + QLatin1Char('y'));
        for (const char *ending : {"ing", "est", "ed", "er", "es", "ly", "s"})
            if (lower.endsWith(QLatin1String(ending)) && lower.size() > int(qstrlen(ending)) + 1)
                stem(lower.chopped(int(qstrlen(ending))));
    }
    return out;
}

bool Dictionary::isDictionary(const LibraryRecord &library)
{
    return library.name.startsWith(QLatin1String("wiktionary_"));
}
