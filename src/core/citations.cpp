// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "citations.h"

#include "library.h"

#include <QCollator>
#include <QHash>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace
{
// Wikimedia's projects, as a ZIM's name starts ("wikipedia_en_all_maxi") and as they are named
const QHash<QString, QString> &wikimediaProjects()
{
    static const QHash<QString, QString> value = {
        {QStringLiteral("wikipedia"), QStringLiteral("Wikipedia")},   {QStringLiteral("wiktionary"), QStringLiteral("Wiktionary")},
        {QStringLiteral("wikiversity"), QStringLiteral("Wikiversity")}, {QStringLiteral("wikibooks"), QStringLiteral("Wikibooks")},
        {QStringLiteral("wikivoyage"), QStringLiteral("Wikivoyage")}, {QStringLiteral("wikinews"), QStringLiteral("Wikinews")},
        {QStringLiteral("wikiquote"), QStringLiteral("Wikiquote")},   {QStringLiteral("wikisource"), QStringLiteral("Wikisource")},
    };
    return value;
}

const char *const MlaMonths[] = {"Jan.", "Feb.", "Mar.", "Apr.", "May", "June", "July", "Aug.", "Sept.", "Oct.", "Nov.", "Dec."};
const char *const Months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

QString mlaDate(const QDate &d)
{
    return QStringLiteral("%1 %2 %3").arg(d.day()).arg(QLatin1String(MlaMonths[d.month() - 1])).arg(d.year());
}

QString longDate(const QDate &d)  // October 20, 2026
{
    return QStringLiteral("%1 %2, %3").arg(QLatin1String(Months[d.month() - 1])).arg(d.day()).arg(d.year());
}

// A sentence of a citation ends with one full stop, not two ("What Is It?." is wrong)
QString closed(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty() || t.endsWith(QLatin1Char('.')) || t.endsWith(QLatin1Char('?')) || t.endsWith(QLatin1Char('!')))
        return t;
    return t + QLatin1Char('.');
}

// MLA puts the full stop inside the quotation marks
QString quotedTitle(const QString &title)
{
    return QStringLiteral("“%1”").arg(closed(title));
}

QString italic(const QString &s, bool markdown)
{
    if (!markdown || s.isEmpty())
        return s;
    QString t = s;
    t.replace(QLatin1Char('*'), QStringLiteral("\\*"));
    return QLatin1Char('*') + t + QLatin1Char('*');
}

QString withoutScheme(const QString &url)
{
    QString u = url;
    if (u.startsWith(QLatin1String("https://")))
        u = u.mid(8);
    return u;
}

constexpr QLatin1StringView Offline("Offline copy via Kiwix.");

QString build(Citations::Style style, const Citations::Source &s, const QDate &accessed, const std::function<QString(const QString &)> &it)
{
    const QString title = s.title.trimmed().isEmpty() ? QString(s.path).replace(QLatin1Char('_'), QLatin1Char(' ')) : s.title.trimmed();
    QStringList parts;
    switch (style) {
    case Citations::Style::Mla: {
        // “Title.” Website, Publisher, Day Mon. Year, URL. Offline copy via Kiwix. Accessed Day Mon. Year.
        QStringList container{it(s.site)};
        if (!s.publisher.isEmpty() && s.publisher != s.site)
            container << s.publisher;
        if (s.copied.isValid())
            container << mlaDate(s.copied);
        if (!s.url.isEmpty())
            container << withoutScheme(s.url);
        parts << quotedTitle(title) << closed(container.join(QStringLiteral(", "))) << Offline;
        if (accessed.isValid())
            parts << QStringLiteral("Accessed %1.").arg(mlaDate(accessed));
        break;
    }
    case Citations::Style::Apa: {
        // Title. (Year, Month Day). In Website. Offline copy via Kiwix. Retrieved Month Day, Year, from URL
        parts << closed(title);
        parts << (s.copied.isValid() ? QStringLiteral("(%1, %2 %3).").arg(s.copied.year()).arg(QLatin1String(Months[s.copied.month() - 1])).arg(s.copied.day())
                                     : QStringLiteral("(n.d.)."));
        parts << QStringLiteral("In %1.").arg(it(s.site));
        if (!s.publisher.isEmpty() && s.publisher != s.site && s.url.isEmpty())
            parts << closed(s.publisher);
        parts << Offline;
        if (accessed.isValid() && !s.url.isEmpty())
            parts << QStringLiteral("Retrieved %1, from %2").arg(longDate(accessed), s.url);
        else if (accessed.isValid())
            parts << QStringLiteral("Retrieved %1.").arg(longDate(accessed));
        else if (!s.url.isEmpty())
            parts << s.url;
        break;
    }
    case Citations::Style::Chicago: {
        // Website. “Title.” Publisher. Offline copy of Month Day, Year, via Kiwix. Accessed Month Day, Year. URL.
        parts << closed(s.site) << QStringLiteral("“%1”").arg(closed(title));
        if (!s.publisher.isEmpty() && s.publisher != s.site)
            parts << closed(s.publisher);
        parts << (s.copied.isValid() ? QStringLiteral("Offline copy of %1, via Kiwix.").arg(longDate(s.copied)) : Offline);
        if (accessed.isValid())
            parts << QStringLiteral("Accessed %1.").arg(longDate(accessed));
        if (!s.url.isEmpty())
            parts << closed(s.url);
        break;
    }
    }
    return parts.join(QLatin1Char(' '));
}

// What an entry is sorted by: its words, without the marks it starts with
QString sortKey(const QString &entry)
{
    QString k = entry;
    k.remove(QLatin1Char('*')).remove(QStringLiteral("“")).remove(QStringLiteral("\""));
    return k.trimmed();
}
}

QStringList Citations::styleNames()
{
    return {QStringLiteral("mla"), QStringLiteral("apa"), QStringLiteral("chicago")};
}

Citations::Style Citations::style(const QString &name)
{
    if (name == QLatin1String("apa"))
        return Style::Apa;
    if (name == QLatin1String("chicago"))
        return Style::Chicago;
    return Style::Mla;
}

QString Citations::name(Style style)
{
    return styleNames().at(int(style));
}

QString Citations::label(Style style)
{
    switch (style) {
    case Style::Apa:
        return QStringLiteral("APA (7th edition)");
    case Style::Chicago:
        return QStringLiteral("Chicago (17th edition)");
    case Style::Mla:
        break;
    }
    return QStringLiteral("MLA (9th edition)");
}

QString Citations::listHeading(Style style)
{
    switch (style) {
    case Style::Apa:
        return QStringLiteral("References");
    case Style::Chicago:
        return QStringLiteral("Bibliography");
    case Style::Mla:
        break;
    }
    return QStringLiteral("Works Cited");
}

Citations::Source Citations::source(const LibraryRecord &library, const QString &path, const QString &title)
{
    Source s;
    s.title = title;
    s.path = path;
    s.copied = QDate::fromString(library.date.left(10), Qt::ISODate);
    // "wikipedia_en_all_maxi", "wiktionary_en-simple_all": project, language, selection
    const QStringList name = library.name.split(QLatin1Char('_'));
    const QString project = name.value(0).toLower();
    if (wikimediaProjects().contains(project) && name.size() >= 2) {
        s.site = wikimediaProjects().value(project);
        s.publisher = QStringLiteral("Wikimedia Foundation");
        QString language = name.at(1).toLower();
        if (language.endsWith(QLatin1String("-simple")) || language == QLatin1String("simple"))
            language = QStringLiteral("simple");  // Simple English Wikipedia lives at simple.wikipedia.org
        // the characters Wikipedia's own addresses keep as they are: "Portland,_Oregon", "Ray_(film)"
        const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(path, "/_-.,:()!~*'$&;=+@"));
        if (!language.isEmpty() && !path.isEmpty())
            s.url = QStringLiteral("https://%1.%2.org/wiki/%3").arg(language, project, encoded);
    } else {
        s.site = library.title.isEmpty() ? library.name : library.title;
        s.publisher = library.creator;
    }
    return s;
}

QString Citations::format(Style style, const Source &source, const QDate &accessed, bool markdown)
{
    return build(style, source, accessed, [markdown, style](const QString &s) {
        return style == Style::Chicago ? s : italic(s, markdown);  // Chicago sets website names in roman
    });
}

QString Citations::html(Style style, const Source &source, const QDate &accessed)
{
    // every piece escaped, then the website's name put in <i>: nothing from a ZIM becomes markup
    Source escaped = source;
    escaped.title = source.title.toHtmlEscaped();
    escaped.path = source.path.toHtmlEscaped();
    escaped.site = source.site.toHtmlEscaped();
    escaped.publisher = source.publisher.toHtmlEscaped();
    escaped.url = source.url.toHtmlEscaped();
    return build(style, escaped, accessed, [style](const QString &s) {
        return style == Style::Chicago || s.isEmpty() ? s : QStringLiteral("<i>%1</i>").arg(s);
    });
}

QList<Citations::Source> Citations::sortedSources(Style style, const QList<Source> &sources, const QDate &accessed)
{
    QList<std::pair<QString, Source>> keyed;
    QSet<QString> seen;
    for (const Source &s : sources) {
        const QString entry = format(style, s, accessed, false);
        if (!seen.contains(entry)) {
            seen.insert(entry);
            keyed.append({sortKey(entry), s});
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(keyed.begin(), keyed.end(), [&collator](const auto &a, const auto &b) {
        return collator.compare(a.first, b.first) < 0;
    });
    QList<Source> out;
    for (const auto &k : std::as_const(keyed))
        out << k.second;
    return out;
}

QStringList Citations::sorted(Style style, const QList<Source> &sources, const QDate &accessed, bool markdown)
{
    QStringList out;
    for (const auto ordered = sortedSources(style, sources, accessed); const Source &s : ordered)
        out << format(style, s, accessed, markdown);
    return out;
}
