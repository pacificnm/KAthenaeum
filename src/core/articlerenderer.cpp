// SPDX-License-Identifier: GPL-3.0-or-later
#include "articlerenderer.h"

#include "extract.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>

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
const QSet<QByteArray> KeepAttributes = {"class", "id", "href", "src", "alt", "title", "colspan", "rowspan", "scope",
                                         "style", "lang", "dir", "width", "height"};

const QRegularExpression BadStyle(QStringLiteral("url\\s*\\(|expression|javascript|@import|behavior|-moz-binding|position\\s*:|z-index|!\\s*important"),
                                  QRegularExpression::CaseInsensitiveOption);
const QRegularExpression External(QStringLiteral("^(?:[a-z][a-z0-9+.-]*:)?//|^(?:https?|ftp|mailto):"), QRegularExpression::CaseInsensitiveOption);
const QRegularExpression Scripting(QStringLiteral("^\\s*(?:javascript|data|vbscript):"), QRegularExpression::CaseInsensitiveOption);

struct Doc {
    htmlDocPtr doc = nullptr;
    ~Doc() { if (doc) xmlFreeDoc(doc); }
};

struct XPathResult {
    xmlXPathContextPtr ctx = nullptr;
    xmlXPathObjectPtr obj = nullptr;
    ~XPathResult()
    {
        if (obj) xmlXPathFreeObject(obj);
        if (ctx) xmlXPathFreeContext(ctx);
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

QString attr(xmlNodePtr n, const char *name)
{
    xmlChar *v = xmlGetProp(n, reinterpret_cast<const xmlChar *>(name));
    if (!v)
        return {};
    QString s = QString::fromUtf8(reinterpret_cast<const char *>(v));
    xmlFree(v);
    return s;
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
    const QString current = attr(n, "class");
    setAttr(n, "class", current.isEmpty() ? QString::fromLatin1(cls) : current + QLatin1Char(' ') + QLatin1String(cls));
}

QString textOf(xmlNodePtr n)
{
    xmlChar *t = xmlNodeGetContent(n);
    if (!t)
        return {};
    QString s = QString::fromUtf8(reinterpret_cast<const char *>(t)).simplified();
    xmlFree(t);
    return s;
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
    for (const QString &segment : target.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (segment == QLatin1String("."))
            continue;
        if (segment == QLatin1String("..") && !parts.isEmpty() && parts.last() != QLatin1String(".."))
            parts.removeLast();
        else
            parts.append(segment);  // a ".." that climbs out stays, so the path matches nothing in the ZIM
    }
    return parts.join(QLatin1Char('/'));
}

QString ArticleRenderer::encodePath(const QString &path)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(path, "/()_-.,!~*'"));
}

std::optional<RenderedArticle> ArticleRenderer::render(const QByteArray &raw, const QString &path, const QString &urlPrefix,
                                                       const HasEntry &hasEntry, QString *error)
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
    d.doc = htmlReadMemory(raw.constData(), int(raw.size()), nullptr, "UTF-8",
                           HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_NOBLANKS);
    if (!d.doc)
        return fail(QStringLiteral("not an HTML page"));

    RenderedArticle out;
    {
        auto h1 = xpath(d.doc, nullptr, "//h1");
        auto t = xpath(d.doc, nullptr, "//title");
        out.title = !h1->nodes().isEmpty() ? textOf(h1->nodes().first()) : QString();
        if (out.title.isEmpty() && !t->nodes().isEmpty())
            out.title = textOf(t->nodes().first());
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

    {
        // Drop only the outermost matches: freeing a node frees what it contains, so dropping a match that sits inside
        // another one (a script in a navbox) would touch freed memory.
        // Decide everything before freeing anything: reading a node to ask whether it is nested is itself a use after
        // free once its ancestor has gone (AddressSanitizer caught exactly that).
        const QList<xmlNodePtr> matches = xpath(d.doc, nullptr, DropXPath)->nodes();
        const QSet<xmlNodePtr> matched(matches.cbegin(), matches.cend());
        QList<xmlNodePtr> outermost;
        for (xmlNodePtr n : matches) {
            if (n == root || n->type != XML_ELEMENT_NODE || !isInside(n, root))
                continue;
            bool nested = false;
            for (xmlNodePtr p = n->parent; p && !nested; p = p->parent)
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
            if (!KeepAttributes.contains(lower) || lower.startsWith("on"))
                remove << n;
            else if (lower == "style" && BadStyle.match(attr(el, "style")).hasMatch())
                remove << n;
        }
        for (const QByteArray &n : remove)
            removeAttr(el, n.constData());
    });

    for (xmlNodePtr img : xpath(d.doc, root, ".//img")->nodes()) {
        const QString src = attr(img, "src");
        // Offline ZIMs without pictures embed an alt-text placeholder as a data: URI; it adds nothing
        const QString media = src.isEmpty() || src.startsWith(QLatin1String("data:")) || External.match(src).hasMatch() ? QString() : resolve(path, src);
        if (media.isEmpty() || !hasEntry(media)) {
            dropTree(img);
            continue;
        }
        setAttr(img, "src", urlPrefix + encodePath(media));
        setAttr(img, "loading", QStringLiteral("lazy"));
    }

    for (xmlNodePtr a : xpath(d.doc, root, ".//a")->nodes()) {
        if (!hasAttr(a, "href"))
            continue;
        const QString href = attr(a, "href").trimmed();
        if (href.startsWith(QLatin1Char('#')))
            continue;
        if (Scripting.match(href).hasMatch()) {
            removeAttr(a, "href");
        } else if (External.match(href).hasMatch()) {
            setAttr(a, "target", QStringLiteral("_blank"));
            setAttr(a, "rel", QStringLiteral("noopener noreferrer nofollow"));
            addClass(a, "wl-ext");
        } else {
            const int hash = href.indexOf(QLatin1Char('#'));
            const QString fragment = hash >= 0 ? href.mid(hash + 1) : QString();
            const QString target = resolve(path, href);
            if (!target.isEmpty() && hasEntry(target)) {
                setAttr(a, "href", urlPrefix + encodePath(target) + (fragment.isEmpty() ? QString() : QLatin1Char('#') + fragment));
            } else {  // the linked article is not in this library: plain, dimmed text
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

    for (xmlNodePtr h : xpath(d.doc, root, ".//h2|.//h3")->nodes()) {
        QString id = attr(h, "id");
        if (id.isEmpty() && h->parent && h->parent->type == XML_ELEMENT_NODE)
            id = attr(h->parent, "id");
        const QString text = textOf(h);
        if (!id.isEmpty() && !text.isEmpty())
            out.toc.append({id, text, name(h) == QLatin1String("h2") ? 2 : 3});
    }

    xmlBufferPtr buffer = xmlBufferCreate();
    for (xmlNodePtr c = root->children; c; c = c->next)
        htmlNodeDump(buffer, d.doc, c);
    out.html = QString::fromUtf8(reinterpret_cast<const char *>(xmlBufferContent(buffer)), xmlBufferLength(buffer));
    xmlBufferFree(buffer);
    return out;
}
