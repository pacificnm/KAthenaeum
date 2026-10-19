// SPDX-License-Identifier: GPL-3.0-or-later
#include "extract.h"

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

const QSet<QString> SkipSections = {QStringLiteral("references"), QStringLiteral("external links"), QStringLiteral("see also"),
                                    QStringLiteral("notes"), QStringLiteral("further reading"), QStringLiteral("bibliography"),
                                    QStringLiteral("citations"), QStringLiteral("sources"), QStringLiteral("footnotes")};

struct Doc {
    htmlDocPtr doc = nullptr;
    ~Doc() { if (doc) xmlFreeDoc(doc); }
};

QList<xmlNodePtr> nodes(htmlDocPtr doc, const char *expr)
{
    QList<xmlNodePtr> out;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
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

QStringList Extract::splitLong(const QString &text)
{
    static const QRegularExpression sentenceEnd(QStringLiteral("(?<=[.!?])\\s+"));
    QStringList out;
    QString cur;
    for (QString sent : text.split(sentenceEnd)) {
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
    for (xmlNodePtr br : nodes(d.doc, "//br"))
        xmlAddNextSibling(br, xmlNewText(reinterpret_cast<const xmlChar *>(" ")));

    {
        // Decide before freeing (see ArticleRenderer): drop only the outermost matches. Never the root element: real pages
        // have <html class="... skin-thumbsize-clientpref-standard ...">, which the 'thumb' rule matches; dropping it threw
        // the whole article away, and merely counting it as matched made every table and style "nested" and kept them
        // all. lxml's getparent() check skipped the root the same way.
        QSet<xmlNodePtr> droppable;
        for (xmlNodePtr n : nodes(d.doc, DropXPath)) {
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
    for (xmlNodePtr el : nodes(d.doc, BlocksXPath)) {
        QString text = clean(el);
        const QString t = tag(el);
        if (t == QLatin1String("h2") || t == QLatin1String("h3") || t == QLatin1String("h4")) {
            const int level = t.mid(1).toInt();
            if (skipLevel && level > skipLevel)
                continue;  // a sub-heading of a skipped section (an h3 inside "Notes") stays skipped
            section = text;
            skipLevel = SkipSections.contains(text.toLower()) ? level : 0;  // a heading of the same or a higher level ends the skip
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
        for (const QString &piece : splitLong(b.text)) {
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
    return out;
}
