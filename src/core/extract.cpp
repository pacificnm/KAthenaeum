// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "extract.h"

#include <KLocalizedString>

#include <QRegularExpression>
#include <QSet>

#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

#include <mutex>

void initXmlParser()
{
    static std::once_flag once;
    std::call_once(once, [] {
        xmlInitParser();
    });
}

namespace
{
// Elements that are never prose: drop them (and their contents) before reading text
const char *DropXPath =
    "//style|//script|//table|//figure|//sup[contains(@class,'reference')]"
    "|//*[contains(@class,'mw-editsection')]|//*[contains(@class,'navbox')]"
    "|//*[contains(@class,'reflist')]|//*[contains(@class,'hatnote')]"
    "|//*[contains(@class,'thumb')]|//*[@role='note']|//*[contains(@class,'mw-empty-elt')]"
    "|//math|//*[contains(@class,'mwe-math-element')]";
const char *BlocksXPath =
    "//div[contains(@class,'mw-parser-output')]"
    "//*[self::p or self::h2 or self::h3 or self::h4 or self::li or self::dt or self::dd or self::blockquote]";
// A <p> or <li> inside one of these is already part of its container's text; reading both would give it twice
const char *Contained = "ancestor::li or ancestor::dd or ancestor::blockquote";
const char *ContainedQuote = "ancestor::dd or ancestor::blockquote";

const QSet<QString> &skipSections()
{
    static const QSet<QString> value = {QStringLiteral("references"), QStringLiteral("external links"), QStringLiteral("see also"),
                                        QStringLiteral("notes"), QStringLiteral("further reading"), QStringLiteral("bibliography"),
                                        QStringLiteral("citations"), QStringLiteral("sources"), QStringLiteral("footnotes")};
    return value;
}

struct Doc {
    htmlDocPtr doc = nullptr;
    ~Doc() { if (doc) xmlFreeDoc(doc); }
};

// `ok`, when given, says whether the query ran: a query libxml2 could not finish (a page past its limits) is not an empty
// one, and the drop list must not treat it as "nothing to drop" (the renderer's rule, XPathResult::ok)
QList<xmlNodePtr> nodes(htmlDocPtr doc, const char *expr, bool *ok = nullptr)
{
    QList<xmlNodePtr> out;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    xmlXPathObjectPtr obj = xmlXPathEvalExpression(reinterpret_cast<const xmlChar *>(expr), ctx);
    if (ok)
        *ok = obj != nullptr;
    if (obj && obj->nodesetval)
        for (int i = 0; i < obj->nodesetval->nodeNr; ++i)
            out.append(obj->nodesetval->nodeTab[i]);
    if (obj)
        xmlXPathFreeObject(obj);
    xmlXPathFreeContext(ctx);
    return out;
}

QList<xmlNodePtr> nodes(htmlDocPtr doc, xmlNodePtr context, const char *expr)
{
    QList<xmlNodePtr> out;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    ctx->node = context;
    xmlXPathObjectPtr obj = xmlXPathEvalExpression(reinterpret_cast<const xmlChar *>(expr), ctx);
    if (obj && obj->nodesetval)
        for (int i = 0; i < obj->nodesetval->nodeNr; ++i)
            out.append(obj->nodesetval->nodeTab[i]);
    if (obj)
        xmlXPathFreeObject(obj);
    xmlXPathFreeContext(ctx);
    return out;
}

bool test(htmlDocPtr doc, xmlNodePtr node, const char *expr)
{
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    ctx->node = node;
    xmlXPathObjectPtr obj = xmlXPathEvalExpression(reinterpret_cast<const xmlChar *>(expr), ctx);
    const bool result = obj && xmlXPathCastToBoolean(obj);
    if (obj)
        xmlXPathFreeObject(obj);
    xmlXPathFreeContext(ctx);
    return result;
}

QString clean(xmlNodePtr n)
{
    xmlChar *t = xmlNodeGetContent(n);
    if (!t)
        return {};
    QString s = QString::fromUtf8(reinterpret_cast<const char *>(t)).simplified();
    xmlFree(t);
    return s;
}

QString tag(xmlNodePtr n)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(n->name));
}

bool isInside(xmlNodePtr n, const QSet<xmlNodePtr> &set)
{
    for (xmlNodePtr p = n->parent; p; p = p->parent)
        if (set.contains(p))
            return true;
    return false;
}

// ---- infobox facts ----
// Not in Athenaeum. A mini library holds only each article's introduction and infobox, and the infobox is where the hard
// facts are (boiling point, density, born, instruments): dropped with the other tables, "What is the boiling point of
// ethanol?" was refused with the Ethanol article in the prompt. Its label/value rows become one "Key facts" passage.
const char *InfoboxRowsXPath = "(//div[contains(@class,'mw-parser-output')]//table[contains(@class,'infobox')])[1]//tr";

QString attr(xmlNodePtr n, const char *name)
{
    xmlChar *v = xmlGetProp(n, reinterpret_cast<const xmlChar *>(name));
    const QString s = v ? QString::fromUtf8(reinterpret_cast<const char *>(v)) : QString();
    xmlFree(v);
    return s;
}

bool hidden(xmlNodePtr n)
{
    const QString name = tag(n);
    if (name == QLatin1String("style") || name == QLatin1String("script"))
        return true;
    if (name == QLatin1String("sup") && (attr(n, "class").contains(QLatin1String("reference")) || attr(n, "class").contains(QLatin1String("noprint"))))
        return true;  // footnote markers ([a], [1]), not exponents
    if (attr(n, "style").remove(QLatin1Char(' ')).contains(QLatin1String("display:none")))
        return true;
    // a link out of the encyclopedia: database identifiers (CAS Number, PubChem) and websites
    return name == QLatin1String("a") && attr(n, "rel").contains(QLatin1String("mw:ExtLink"));
}

void cellText(xmlNodePtr n, QString &out)
{
    for (xmlNodePtr c = n->children; c; c = c->next) {
        if (c->type == XML_TEXT_NODE) {
            out += QString::fromUtf8(reinterpret_cast<const char *>(c->content));
            continue;
        }
        if (c->type != XML_ELEMENT_NODE || hidden(c))
            continue;
        const QString name = tag(c);
        if (name == QLatin1String("sup")) {
            // g/cm^3, 10^-3: an exponent read as plain digits is a different number. A superscript that is only a link
            // out or the audio icon leaves nothing.
            QString inner;
            cellText(c, inner);
            inner = inner.remove(QChar(0x24D8)).trimmed();  // ⓘ
            if (!inner.isEmpty())
                out += QLatin1Char('^') + inner;
            continue;
        }
        if (name == QLatin1String("br"))
            out += QLatin1Char(' ');
        else if (name == QLatin1String("li") && !out.trimmed().isEmpty())
            out += QStringLiteral(", ");  // "Vocals, piano", not "Vocals piano"
        cellText(c, out);
        if (name == QLatin1String("div") || name == QLatin1String("p"))
            out += QLatin1Char(' ');
    }
}

// "Label: value" lines, grouped into passages of at most MaxChars
QStringList infoboxFacts(htmlDocPtr doc)
{
    QStringList lines;
    for (const auto rows = nodes(doc, InfoboxRowsXPath); xmlNodePtr row : rows) {
        QList<xmlNodePtr> cells;
        for (xmlNodePtr c = row->children; c; c = c->next)
            if (c->type == XML_ELEMENT_NODE && (tag(c) == QLatin1String("th") || tag(c) == QLatin1String("td")))
                cells << c;
        if (cells.size() != 2)
            continue;  // a heading, an image, a caption: not a label and a value
        if (!nodes(doc, cells[0], ".//img|.//*[@typeof='mw:File']").isEmpty())
            continue;  // pictures side by side with their captions
        QString label, value;
        cellText(cells[0], label);
        cellText(cells[1], value);
        label = label.remove(QChar(0x200B)).simplified();
        value = value.remove(QChar(0x200B)).simplified();
        static const QRegularExpression spaceComma(QStringLiteral("\\s+,"));
        label.replace(spaceComma, QStringLiteral(","));
        value.replace(spaceComma, QStringLiteral(","));
        while (value.startsWith(QLatin1Char(',')))
            value = value.mid(1).trimmed();
        if (label.isEmpty() || label.size() > 60 || value.isEmpty() || value == QLatin1String("Y"))
            continue;
        lines << QString(label + QStringLiteral(": ") + value).left(Extract::MaxChars);
    }
    QStringList out;
    QString cur;
    for (const QString &l : std::as_const(lines)) {
        if (!cur.isEmpty() && cur.size() + 1 + l.size() > Extract::MaxChars) {
            out << cur;
            cur.clear();
        }
        cur = cur.isEmpty() ? l : cur + QLatin1Char('\n') + l;
    }
    if (!cur.isEmpty() && cur.size() < Extract::MinChars && !out.isEmpty())
        out.last() += QLatin1Char('\n') + cur;  // a scrap is not worth a passage of its own
    else if (!cur.isEmpty())
        out << cur;
    return out;
}

// A passage under MinChars is joined to its neighbour instead of being thrown away, so a short section (or an article
// that is one short sentence) still gets a passage
QList<Passage> mergeSmall(const QList<Passage> &chunks)
{
    QList<Passage> out;
    QString pending;
    for (Passage p : chunks) {
        if (!pending.isEmpty()) {
            p.text = pending + QStringLiteral("\n\n") + p.text;
            pending.clear();
        }
        if (p.text.size() < Extract::MinChars) {
            pending = p.text;
            continue;
        }
        out.append(p);
    }
    if (!pending.isEmpty()) {
        if (!out.isEmpty())
            out.last().text += QStringLiteral("\n\n") + pending;
        else if (pending.size() >= 12)
            out.append({QString(), pending});
    }
    return out;
}
}

QString Extract::keyFactsSection()
{
    return i18nc("@label section of an article holding its infobox", "Key facts");
}

QStringList Extract::splitLong(const QString &text)
{
    static const QRegularExpression sentenceEnd(QStringLiteral("(?<=[.!?])\\s+"));
    QStringList out;
    QString cur;
    for (const auto parts = text.split(sentenceEnd); QString sent : parts) {
        while (sent.size() > MaxChars) {
            if (!cur.isEmpty()) {
                out.append(cur);
                cur.clear();
            }
            out.append(sent.left(MaxChars));
            sent = sent.mid(MaxChars);
        }
        if (!cur.isEmpty() && cur.size() + 1 + sent.size() > MaxChars) {
            out.append(cur);
            cur = sent;
        } else {
            cur = QString(cur + QLatin1Char(' ') + sent).trimmed();
        }
    }
    if (!cur.isEmpty())
        out.append(cur);
    return out;
}

ExtractedArticle Extract::extract(const QByteArray &raw)
{
    initXmlParser();
    ExtractedArticle out;
    if (raw.trimmed().isEmpty())
        return out;
    Doc d;
    d.doc = htmlReadMemory(raw.constData(), int(raw.size()), nullptr, "UTF-8",
                           HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (!d.doc)
        return out;

    if (const auto titles = nodes(d.doc, "//title"); !titles.isEmpty())
        out.title = clean(titles.first());

    // a line break is a word break: "one<br>two" must not become "onetwo"
    for (const auto breaks = nodes(d.doc, "//br"); xmlNodePtr br : breaks)
        xmlAddNextSibling(br, xmlNewText(reinterpret_cast<const xmlChar *>(" ")));

    const QStringList facts = infoboxFacts(d.doc);  // before the tables are dropped

    {
        // Decide before freeing (see ArticleRenderer): drop only the outermost matches. Never the root element: real pages
        // have <html class="... skin-thumbsize-clientpref-standard ...">, which the 'thumb' rule matches; dropping it threw
        // the whole article away, and merely counting it as matched made every table and style "nested" and kept them
        // all. lxml's getparent() check skipped the root the same way.
        QSet<xmlNodePtr> droppable;
        bool ran = false;
        const QList<xmlNodePtr> matches = nodes(d.doc, DropXPath, &ran);
        if (!ran)
            return {};  // fail closed: scripts and styles must never reach passages or a prompt
        for (xmlNodePtr n : matches) {
            const bool root = !n->parent || n->parent->type == XML_HTML_DOCUMENT_NODE || n->parent->type == XML_DOCUMENT_NODE;
            if (n->type == XML_ELEMENT_NODE && !root)
                droppable.insert(n);
        }
        QList<xmlNodePtr> outermost;
        for (xmlNodePtr n : std::as_const(droppable))
            if (!isInside(n, droppable))
                outermost.append(n);
        for (xmlNodePtr n : std::as_const(outermost)) {
            xmlUnlinkNode(n);
            xmlFreeNode(n);
        }
    }

    QString section;
    int skipLevel = 0;  // heading level of the skipped section we are inside, 0 if none
    QList<Passage> blocks;
    for (const auto elements = nodes(d.doc, BlocksXPath); xmlNodePtr el : elements) {
        QString text = clean(el);
        const QString t = tag(el);
        if (t == QLatin1String("h2") || t == QLatin1String("h3") || t == QLatin1String("h4")) {
            const int level = QStringView(t).mid(1).toInt();
            if (skipLevel && level > skipLevel)
                continue;  // a sub-heading of a skipped section (an h3 inside "Notes") stays skipped
            section = text;
            skipLevel = skipSections().contains(text.toLower()) ? level : 0;  // a heading of the same or a higher level ends the skip
            continue;
        }
        if (skipLevel || text.isEmpty())
            continue;
        if ((t == QLatin1String("p") || t == QLatin1String("li")) && test(d.doc, el, Contained))
            continue;
        if ((t == QLatin1String("dd") || t == QLatin1String("li") || t == QLatin1String("blockquote")) && test(d.doc, el, ContainedQuote))
            continue;
        if (t == QLatin1String("li"))
            text = QStringLiteral("- ") + text;
        blocks.append({section, text});
    }

    QList<Passage> chunks;
    QString curSection, cur;
    for (const Passage &b : std::as_const(blocks)) {
        for (const auto pieces = splitLong(b.text); const QString &piece : pieces) {
            if (!cur.isEmpty() && (b.section != curSection || cur.size() + 2 + piece.size() > MaxChars)) {
                chunks.append({curSection, cur});
                cur.clear();
            }
            curSection = b.section;
            cur = cur.isEmpty() ? piece : cur + QStringLiteral("\n\n") + piece;
        }
    }
    if (!cur.isEmpty())
        chunks.append({curSection, cur});
    out.passages = mergeSmall(chunks);
    if (!facts.isEmpty()) {
        // after the introduction, which stays first (it defines the topic, and Ask always keeps passage 0)
        qsizetype at = !out.passages.isEmpty() && out.passages.first().section.isEmpty() ? 1 : 0;
        for (const QString &piece : facts)
            out.passages.insert(at++, {Extract::keyFactsSection(), piece});
    }
    return out;
}
