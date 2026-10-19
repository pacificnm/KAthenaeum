// SPDX-License-Identifier: GPL-3.0-or-later
#include "articlestore.h"

#include "core/zimreader.h"

#include "core/projects.h"

#include <QCache>
#include <QColor>

namespace
{
QCache<QString, ArticleStore::Article> cache(24);

QString key(qint64 libraryId, const QString &path)
{
    return QString::number(libraryId) + QLatin1Char('/') + path;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t));
}

// Article styling, adapted from Athenaeum's article.css, with the colours taken from the desktop
QString css(const QPalette &p)
{
    const QColor bg = p.color(QPalette::Base);
    const QColor text = p.color(QPalette::Text);
    const bool dark = bg.lightness() < 128;
    QString vars = QStringLiteral(":root { --bg: %1; --text: %2; --muted: %3; --border: %4; --surface: %5; --surface-2: %6;"
                                  " --link: %7; --link-visited: %8; --accent-soft: %9; color-scheme: %10; }")
                       .arg(bg.name(), text.name(), mix(text, bg, 0.4).name(), mix(text, bg, 0.82).name(), mix(text, bg, 0.97).name(),
                            mix(text, bg, 0.92).name(), p.color(QPalette::Link).name(), p.color(QPalette::LinkVisited).name(),
                            mix(p.color(QPalette::Highlight), bg, 0.85).name(), dark ? QStringLiteral("dark") : QStringLiteral("light"));
    QString rules = QStringLiteral(R"(
html { background: var(--bg); color: var(--text); }
body { margin: 0; padding: 24px 32px 64px; font: 17px/1.72 "Noto Serif", "Source Serif Pro", Georgia, serif; overflow-wrap: anywhere; }
main { max-width: 860px; margin: 0 auto; }
h1 { margin: 0 0 12px; font-weight: 600; font-size: 2.3rem; line-height: 1.15; padding-bottom: 12px; border-bottom: 1px solid var(--border); }
.article p { margin: 0.9em 0; }
.article h2, .article h3, .article h4 { font-weight: 600; line-height: 1.25; margin: 1.6em 0 0.5em; }
.article h2 { font-size: 1.6rem; padding-bottom: 0.2em; border-bottom: 1px solid var(--border); }
.article h3 { font-size: 1.25rem; }
.article a { color: var(--link); text-decoration: none; }
.article a:hover { text-decoration: underline; }
.article a:visited { color: var(--link-visited); }
.article a.wl-ext::after { content: '\2197'; font-size: 0.7em; margin-left: 2px; opacity: 0.6; }
.article .wl-missing { color: inherit; }
.article mark.excerpt-hl { background: var(--accent-soft); color: inherit; border-bottom: 2px solid var(--link); border-radius: 2px; cursor: help; }
.article ul, .article ol { padding-left: 1.5em; margin: 0.8em 0; }
.article li { margin: 0.25em 0; }
.article .plainlist ul, .article .plainlist ol, .article ul.plainlist { list-style: none; padding-left: 0; }
.article sup, .article sub { line-height: 0; font-size: 0.72em; }
.article img { max-width: 100%; height: auto; }
.article figure { margin: 1em 0; }
.article figcaption, .article .thumbcaption { font: 0.85rem/1.4 sans-serif; color: var(--muted); }
.article .hatnote { font-style: italic; padding-left: 1.6em; margin: 0.6em 0; color: var(--muted); font-size: 0.95em; }
.article .nowrap { white-space: nowrap; }
.article .reflist, .article ol.references { font-size: 0.88rem; line-height: 1.5; }
.article math, .article .mwe-math-element { overflow-x: auto; max-width: 100%; }
.article pre, .article code { font-family: monospace; font-size: 0.88em; }
.article pre { overflow-x: auto; padding: 12px; background: var(--surface-2); border-radius: 8px; }
.article table { border-collapse: collapse; font: 0.9rem/1.45 sans-serif; }
.article table.infobox, .article table.ib-chembox { float: right; clear: right; width: 22em; max-width: 100%; margin: 0.4em 0 1.2em 1.6em;
  background: var(--surface); border: 1px solid var(--border); border-radius: 10px; overflow: hidden; }
.article table.infobox caption, .article .infobox-above { font-weight: 700; font-size: 1.05rem; padding: 10px 12px; background: var(--accent-soft); }
.article table.infobox th, .article table.infobox td { padding: 5px 10px; border-top: 1px solid var(--border); vertical-align: top; text-align: left; }
.article table.infobox table { width: 100%; }
.article table.wikitable { margin: 1em 0; display: block; overflow-x: auto; max-width: 100%; }
.article table.wikitable th, .article table.wikitable td { border: 1px solid var(--border); padding: 6px 10px; }
.article table.wikitable th { background: var(--surface-2); }
@media (max-width: 640px) { body { padding: 16px; } .article table.infobox, .article table.ib-chembox { float: none; width: 100%; margin: 1em 0; } }
)");
    if (dark)  // Wikipedia hard-codes light backgrounds inline; keep their text readable
        rules += QStringLiteral(".article [style*='background'] { color: #1b1d21; } .article [style*='background'] a { color: #234a9e; }\n");
    return vars + rules;
}

QByteArray document(const QString &title, const QString &body, const QPalette &palette)
{
    // No scripts at all (JavaScript is off in the view too), and nothing may load from anywhere but the ZIM
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                          "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src zim:; style-src 'unsafe-inline'\">"
                          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                          "<title>%1</title><style>%2</style></head><body><main><h1>%1</h1><div class=\"article\">%3</div></main></body></html>")
        .arg(title.toHtmlEscaped(), css(palette), body)
        .toUtf8();
}
}

QUrl ArticleStore::url(qint64 libraryId, const QString &path, const QString &fragment)
{
    QUrl u(QStringLiteral("zim://lib%1/%2").arg(libraryId).arg(ArticleRenderer::encodePath(path)));
    if (!fragment.isEmpty())
        u.setFragment(fragment);
    return u;
}

bool ArticleStore::parse(const QUrl &url, qint64 *libraryId, QString *path)
{
    if (url.scheme() != QLatin1String("zim") || !url.host().startsWith(QLatin1String("lib")))
        return false;
    bool ok = false;
    *libraryId = url.host().mid(3).toLongLong(&ok);
    *path = url.path(QUrl::FullyDecoded).mid(1);
    return ok && !path->isEmpty();
}

std::optional<ArticleStore::Article> ArticleStore::article(qint64 libraryId, const QString &path)
{
    if (const Article *a = cache.object(key(libraryId, path)))
        return *a;
    const auto content = ZimReader::content(libraryId, path);
    if (!content || !content->mimetype.startsWith(QLatin1String("text/html")))
        return std::nullopt;
    // saved excerpts are marked in the page (cleared from the cache whenever a project changes)
    auto rendered = ArticleRenderer::render(
        content->data, content->path, QStringLiteral("zim://lib%1/").arg(libraryId),
        [libraryId](const QString &p) {
            return ZimReader::hasEntry(libraryId, p);
        },
        nullptr, Projects::highlightsFor(libraryId, content->path));
    if (!rendered)
        return std::nullopt;
    auto *a = new Article{libraryId, content->path, std::move(*rendered)};
    const Article copy = *a;
    cache.insert(key(libraryId, path), a);
    return copy;
}

void ArticleStore::clear()
{
    cache.clear();
}

QByteArray ArticleStore::page(const Article &article, const QPalette &palette)
{
    return document(article.rendered.title, article.rendered.html, palette);
}

QByteArray ArticleStore::notFoundPage(const QString &path, const QPalette &palette)
{
    return document(QStringLiteral("Not found"),
                    QStringLiteral("<p>This library has no article at <code>%1</code>.</p>").arg(path.toHtmlEscaped()), palette);
}
