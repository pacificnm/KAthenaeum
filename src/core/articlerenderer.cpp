// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "articlerenderer.h"
#include "xmlutil_p.h"

#include "extract.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <cmath>

#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/xpath.h>

namespace
{
// Never useful (or never safe) in the reader. Their contents go too.
const char *DropXPath =
    "//script|//style|//link|//meta|//noscript|//iframe|//object|//embed|//form|//input|//button|//select|//textarea"
    "|//*[contains(@class,'mw-editsection')]|//*[contains(@class,'navbox')]|//*[contains(@class,'noprint')]"
    "|//*[contains(@class,'mw-empty-elt')]"
    "|//base|//area|//map|//video|//audio|//source|//track|//canvas|//dialog|//template|//svg";

// NOT target/rel: the code below adds exactly those itself
const QSet<QByteArray> &keepAttributes()
{
    static const QSet<QByteArray> value =
        {"class", "id", "href", "src", "alt", "title", "colspan", "rowspan", "scope", "style", "lang", "dir", "width", "height"};
    return value;
}

// A backslash anywhere: CSS escapes can spell anything ("u\\72l(" is url())
const QRegularExpression &badStyle()
{
    static const QRegularExpression value(
        QStringLiteral("\\\\|url\\s*\\(|image-set|src\\s*\\(|expression|javascript|@import|behavior|-moz-binding|position\\s*:|z-index|!\\s*important"),
        QRegularExpression::CaseInsensitiveOption);
    return value;
}
const QRegularExpression &external()
{
    static const QRegularExpression value(QStringLiteral("^(?:[a-z][a-z0-9+.-]*:)?//|^(?:https?|ftp|mailto):"), QRegularExpression::CaseInsensitiveOption);
    return value;
}
const QRegularExpression &scripting()
{
    static const QRegularExpression value(QStringLiteral("^\\s*(?:javascript|data|vbscript):"), QRegularExpression::CaseInsensitiveOption);
    return value;
}

struct Doc {
    htmlDocPtr doc = nullptr;
    ~Doc()
    {
        if (doc)
            xmlFreeDoc(doc);
    }
};

struct XPathResult {
    xmlXPathContextPtr ctx = nullptr;
    xmlXPathObjectPtr obj = nullptr;
    ~XPathResult()
    {
        if (obj)
            xmlXPathFreeObject(obj);
        if (ctx)
            xmlXPathFreeContext(ctx);
    }
    // A query libxml2 could not finish (a document past its limits) is not an empty one: the sanitiser must not treat
    // "nothing found to drop" as safe (it would fail open on a huge page)
    bool ok() const
    {
        return obj != nullptr;
    }
    QList<xmlNodePtr> nodes() const
    {
        QList<xmlNodePtr> out;
        if (obj && obj->nodesetval)
            for (int i = 0; i < obj->nodesetval->nodeNr; ++i)
                out.append(obj->nodesetval->nodeTab[i]);
        return out;
    }
};

std::unique_ptr<XPathResult> xpath(htmlDocPtr doc, xmlNodePtr context, const char *expr)
{
    auto r = std::make_unique<XPathResult>();
    r->ctx = xmlXPathNewContext(doc);
    r->ctx->node = context;
    r->obj = xmlXPathEvalExpression(reinterpret_cast<const xmlChar *>(expr), r->ctx);
    return r;
}

bool hasAttr(xmlNodePtr n, const char *name)
{
    return xmlHasProp(n, reinterpret_cast<const xmlChar *>(name)) != nullptr;
}

void setAttr(xmlNodePtr n, const char *name, const QString &value)
{
    xmlSetProp(n, reinterpret_cast<const xmlChar *>(name), reinterpret_cast<const xmlChar *>(value.toUtf8().constData()));
}

void removeAttr(xmlNodePtr n, const char *name)
{
    if (xmlAttrPtr a = xmlHasProp(n, reinterpret_cast<const xmlChar *>(name)))
        xmlRemoveProp(a);
}

void addClass(xmlNodePtr n, const char *cls)
{
    const QString current = XmlUtil::attr(n, "class");
    setAttr(n, "class", current.isEmpty() ? QString::fromLatin1(cls) : current + QLatin1Char(' ') + QLatin1String(cls));
}

bool isInside(xmlNodePtr n, xmlNodePtr ancestor)
{
    for (; n; n = n->parent)
        if (n == ancestor)
            return true;
    return false;
}

void dropTree(xmlNodePtr n)
{
    xmlUnlinkNode(n);
    xmlFreeNode(n);
}

QString name(xmlNodePtr n)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(n->name));
}

template<typename F>
void forEachElement(xmlNodePtr root, F &&f)
{
    for (xmlNodePtr c = root->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE)
            continue;
        f(c);
        forEachElement(c, f);
    }
}
}

std::optional<std::pair<double, double>> ArticleRenderer::parseGeo(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(';'));
    if (parts.size() != 2)
        return std::nullopt;
    bool a = false, b = false;
    const double lat = parts.at(0).trimmed().toDouble(&a), lon = parts.at(1).trimmed().toDouble(&b);
    if (!a || !b || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180)
        return std::nullopt;
    return std::pair{lat, lon};
}

QString ArticleRenderer::resolve(const QString &basePath, const QString &href)
{
    QString target = href;
    if (const int hash = target.indexOf(QLatin1Char('#')); hash >= 0)
        target.truncate(hash);
    if (const int query = target.indexOf(QLatin1Char('?')); query >= 0)
        target.truncate(query);
    target = QUrl::fromPercentEncoding(target.toUtf8());
    if (target.isEmpty())
        return {};

    QStringList parts;
    if (!target.startsWith(QLatin1Char('/'))) {
        const int slash = basePath.lastIndexOf(QLatin1Char('/'));
        if (slash > 0)
            parts = basePath.left(slash).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    }
    for (const auto qLatin1Char = target.split(QLatin1Char('/'), Qt::SkipEmptyParts); const QString &segment : qLatin1Char) {
        if (segment == QLatin1String("."))
            continue;
        if (segment == QLatin1String("..") && !parts.isEmpty() && parts.last() != QLatin1String(".."))
            parts.removeLast();
        else
            parts.append(segment); // a ".." that climbs out stays, so the path matches nothing in the ZIM
    }
    return parts.join(QLatin1Char('/'));
}

QString ArticleRenderer::encodePath(const QString &path)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(path, "/()_-.,!~*'"));
}

namespace
{
void textNodes(xmlNodePtr n, QList<xmlNodePtr> &out)
{
    for (xmlNodePtr c = n->children; c; c = c->next) {
        if (c->type == XML_TEXT_NODE)
            out << c;
        else if (c->type == XML_ELEMENT_NODE)
            textNodes(c, out);
    }
}

QString contentOf(xmlNodePtr n)
{
    return n->content ? QString::fromUtf8(reinterpret_cast<const char *>(n->content)) : QString();
}

// Wraps [from, to) of a text node in <mark>. The node keeps the text before it. libxml2 merges a text node added next to
// another text node into that node (xmlAddPrevSibling put the "before" part into the original node, which was then
// freed: the text after every mark vanished), so only the mark, an element, is added next to the original.
void wrap(xmlNodePtr node, qsizetype from, qsizetype to, const Highlight &h, bool first)
{
    const QString text = contentOf(node);
    const QByteArray before = text.left(from).toUtf8();
    xmlNodeSetContentLen(node, reinterpret_cast<const xmlChar *>(before.constData()), int(before.size()));
    xmlNodePtr mark = xmlNewNode(nullptr, reinterpret_cast<const xmlChar *>("mark"));
    xmlNewProp(mark, reinterpret_cast<const xmlChar *>("class"), reinterpret_cast<const xmlChar *>(h.cssClass.toUtf8().constData()));
    if (first && !h.anchor.isEmpty())
        xmlNewProp(mark, reinterpret_cast<const xmlChar *>("id"), reinterpret_cast<const xmlChar *>(h.anchor.toUtf8().constData()));
    if (!h.label.isEmpty())
        xmlNewProp(mark, reinterpret_cast<const xmlChar *>("title"), reinterpret_cast<const xmlChar *>(h.label.toUtf8().constData()));
    xmlAddChild(mark, xmlNewText(reinterpret_cast<const xmlChar *>(text.mid(from, to - from).toUtf8().constData())));
    xmlAddNextSibling(node, mark);
    if (to < text.size()) // may merge into a text node after the mark: the same text, in the same place
        xmlAddNextSibling(mark, xmlNewText(reinterpret_cast<const xmlChar *>(text.mid(to).toUtf8().constData())));
}

// One highlight: find it in the text (whitespace and case ignored) and mark every text node it covers
bool highlight(xmlNodePtr root, const Highlight &h)
{
    QString needle;
    for (const QChar c : h.text)
        if (!c.isSpace())
            needle += c.toLower();
    if (needle.size() < 8)
        return false; // too short to place reliably
    QList<xmlNodePtr> nodes;
    textNodes(root, nodes);
    QString hay;
    QList<std::pair<int, int>> where; // for each character of `hay`: (node, offset in it)
    QList<QString> texts;
    for (int i = 0; i < nodes.size(); ++i) {
        texts << contentOf(nodes.at(i));
        const QString &t = texts.last();
        for (int k = 0; k < t.size(); ++k)
            if (!t.at(k).isSpace()) {
                hay += t.at(k).toLower();
                where.append({i, k});
            }
    }
    const qsizetype at = hay.indexOf(needle);
    if (at < 0)
        return false;
    const auto [firstNode, firstOffset] = where.at(at);
    const auto [lastNode, lastOffset] = where.at(at + needle.size() - 1);
    // decide every range first, then change nodes from the last back: each node is replaced on its own
    QList<std::tuple<xmlNodePtr, qsizetype, qsizetype>> ranges;
    for (int i = firstNode; i <= lastNode; ++i) {
        const qsizetype from = i == firstNode ? firstOffset : 0;
        const qsizetype to = i == lastNode ? lastOffset + 1 : texts.at(i).size();
        if (!QStringView(texts.at(i)).mid(from, to - from).trimmed().isEmpty())
            ranges.append({nodes.at(i), from, to});
    }
    for (auto it = ranges.crbegin(); it != ranges.crend(); ++it)
        wrap(std::get<0>(*it), std::get<1>(*it), std::get<2>(*it), h, it == std::prev(ranges.crend()));
    return true;
}
}

std::optional<RenderedArticle> ArticleRenderer::render(const QByteArray &raw,
                                                       const QString &path,
                                                       const QString &urlPrefix,
                                                       const HasEntry &hasEntry,
                                                       QString *error,
                                                       const QList<Highlight> &highlights)
{
    auto fail = [error](const QString &why) -> std::optional<RenderedArticle> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    initXmlParser();
    if (raw.trimmed().isEmpty())
        return fail(QStringLiteral("empty page"));

    Doc d;
    d.doc = htmlReadMemory(raw.constData(),
                           int(raw.size()),
                           nullptr,
                           "UTF-8",
                           HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_NOBLANKS);
    if (!d.doc)
        return fail(QStringLiteral("not an HTML page"));

    RenderedArticle out;
    {
        auto h1 = xpath(d.doc, nullptr, "//h1");
        auto t = xpath(d.doc, nullptr, "//title");
        out.title = !h1->nodes().isEmpty() ? XmlUtil::text(h1->nodes().first()) : QString();
        if (out.title.isEmpty() && !t->nodes().isEmpty())
            out.title = XmlUtil::text(t->nodes().first());
        if (out.title.isEmpty())
            out.title = path;
    }

    // Wikipedia (mwoffliner) articles keep their text in this wrapper. A whole page without it (an older or custom page)
    // falls back to its body; a bare fragment ("<p>hi</p>", "just text") is not a page at all and is refused.
    xmlNodePtr root = nullptr;
    {
        auto r = xpath(d.doc, nullptr, "//div[contains(concat(' ', normalize-space(@class), ' '), ' mw-parser-output ')]");
        auto title = xpath(d.doc, nullptr, "//head/title");
        auto body = xpath(d.doc, nullptr, "//body");
        if (!r->nodes().isEmpty())
            root = r->nodes().first();
        else if (!title->nodes().isEmpty() && !body->nodes().isEmpty())
            root = body->nodes().first();
    }
    if (!root)
        return fail(QStringLiteral("not an article page"));

    // the subject's coordinates, before anything is dropped: Wikipedia keeps the decimal ones in a hidden span
    for (const auto nodes = xpath(d.doc, root, ".//span[@class='geo']")->nodes(); xmlNodePtr g : nodes)
        if (const auto ll = parseGeo(XmlUtil::text(g))) {
            out.hasCoordinates = true;
            out.lat = ll->first;
            out.lon = ll->second;
            break;
        }

    {
        // Drop only the outermost matches: freeing a node frees what it contains, so dropping a match that sits inside
        // another one (a script in a navbox) would touch freed memory.
        // Decide everything before freeing anything: reading a node to ask whether it is nested is itself a use after
        // free once its ancestor has gone (AddressSanitizer caught exactly that).
        const auto dropped = xpath(d.doc, nullptr, DropXPath);
        if (!dropped->ok())
            return fail(QStringLiteral("the page is too large or too complex to make safe"));
        const QList<xmlNodePtr> matches = dropped->nodes();
        const QSet<xmlNodePtr> matched(matches.cbegin(), matches.cend());
        QList<xmlNodePtr> outermost;
        for (xmlNodePtr n : matches) {
            if (n == root || n->type != XML_ELEMENT_NODE || !isInside(n, root))
                continue;
            // nested in another match inside the content, that is: the root itself, or anything above it, may match too
            // (a page whose wrapper is classed "navbox", or sits in a "noprint" div), and must not shield what it holds
            bool nested = false;
            for (xmlNodePtr p = n->parent; p && p != root && !nested; p = p->parent)
                nested = matched.contains(p);
            if (!nested)
                outermost.append(n);
        }
        for (xmlNodePtr n : std::as_const(outermost))
            dropTree(n);
    }

    // the allowlist first; the attributes added below (rel, target, rewritten href and src) must survive
    forEachElement(root, [](xmlNodePtr el) {
        QList<QByteArray> remove;
        for (xmlAttrPtr a = el->properties; a; a = a->next) {
            const QByteArray n = reinterpret_cast<const char *>(a->name);
            const QByteArray lower = n.toLower();
            if (!keepAttributes().contains(lower) || lower.startsWith("on"))
                remove << n;
            else if (lower == "style" && badStyle().match(XmlUtil::attr(el, "style")).hasMatch())
                remove << n;
        }
        for (const QByteArray &n : std::as_const(remove))
            removeAttr(el, n.constData());
    });

    const auto images = xpath(d.doc, root, ".//img");
    const auto links = xpath(d.doc, root, ".//a");
    if (!images->ok() || !links->ok())
        return fail(QStringLiteral("the page is too large or too complex to make safe"));
    for (const auto nodes = images->nodes(); xmlNodePtr img : nodes) {
        const QString src = XmlUtil::attr(img, "src");
        // Offline ZIMs without pictures embed an alt-text placeholder as a data: URI; it adds nothing
        const QString media = src.isEmpty() || src.startsWith(QLatin1String("data:")) || external().match(src).hasMatch() ? QString() : resolve(path, src);
        if (media.isEmpty() || !hasEntry(media)) {
            dropTree(img);
            continue;
        }
        setAttr(img, "src", urlPrefix + encodePath(media));
        setAttr(img, "loading", QStringLiteral("lazy"));
    }

    for (const auto nodes = links->nodes(); xmlNodePtr a : nodes) {
        if (!hasAttr(a, "href"))
            continue;
        const QString href = XmlUtil::attr(a, "href").trimmed();
        if (href.startsWith(QLatin1Char('#')))
            continue;
        if (scripting().match(href).hasMatch()) {
            removeAttr(a, "href");
        } else if (external().match(href).hasMatch()) {
            setAttr(a, "target", QStringLiteral("_blank"));
            setAttr(a, "rel", QStringLiteral("noopener noreferrer nofollow"));
            addClass(a, "wl-ext");
        } else {
            const int hash = href.indexOf(QLatin1Char('#'));
            const QString fragment = hash >= 0 ? href.mid(hash + 1) : QString();
            const QString target = resolve(path, href);
            if (!target.isEmpty() && hasEntry(target)) {
                setAttr(a, "href", urlPrefix + encodePath(target) + (fragment.isEmpty() ? QString() : QLatin1Char('#') + fragment));
            } else { // the linked article is not in this library: plain, dimmed text
                xmlNodeSetName(a, reinterpret_cast<const xmlChar *>("span"));
                removeAttr(a, "href");
                addClass(a, "wl-missing");
            }
        }
    }

    // a href or src on any other element is a way to smuggle in a URL
    forEachElement(root, [](xmlNodePtr el) {
        const QString n = name(el);
        if (n != QLatin1String("a"))
            removeAttr(el, "href");
        if (n != QLatin1String("img"))
            removeAttr(el, "src");
    });

    for (const auto nodes = xpath(d.doc, root, ".//h2|.//h3")->nodes(); xmlNodePtr h : nodes) {
        QString id = XmlUtil::attr(h, "id");
        if (id.isEmpty() && h->parent && h->parent->type == XML_ELEMENT_NODE)
            id = XmlUtil::attr(h->parent, "id");
        const QString text = XmlUtil::text(h);
        if (!id.isEmpty() && !text.isEmpty())
            out.toc.append({id, text, name(h) == QLatin1String("h2") ? 2 : 3});
    }

    for (const Highlight &h : highlights)
        out.highlighted += highlight(root, h);

    xmlBufferPtr buffer = xmlBufferCreate();
    for (xmlNodePtr c = root->children; c; c = c->next)
        htmlNodeDump(buffer, d.doc, c);
    out.html = QString::fromUtf8(reinterpret_cast<const char *>(xmlBufferContent(buffer)), xmlBufferLength(buffer));
    xmlBufferFree(buffer);
    return out;
}
