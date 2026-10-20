// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline.h"

#include "extract.h"
#include "rag.h"

#include <KLocalizedString>

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
constexpr QLatin1StringView Months("(January|February|March|April|May|June|July|August|September|October|November|December)");
constexpr QLatin1StringView Era("(BC|BCE|AD|CE|B\\.C\\.|A\\.D\\.)");

int monthNumber(const QString &name)
{
    static const QStringList names = {QStringLiteral("january"),
                                      QStringLiteral("february"),
                                      QStringLiteral("march"),
                                      QStringLiteral("april"),
                                      QStringLiteral("may"),
                                      QStringLiteral("june"),
                                      QStringLiteral("july"),
                                      QStringLiteral("august"),
                                      QStringLiteral("september"),
                                      QStringLiteral("october"),
                                      QStringLiteral("november"),
                                      QStringLiteral("december")};
    return int(names.indexOf(name.toLower())) + 1;
}

bool isBc(const QString &era)
{
    return era.startsWith(QLatin1Char('B'));
}

double year(const QString &digits, const QString &era)
{
    QString d = digits;
    d.remove(QLatin1Char(','));
    const double y = d.toDouble();
    return isBc(era) ? -y : y;
}

QRegularExpression re(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

// English ordinals differ by the number's last digits ("1st", "2nd", "3rd", "11th"), so each kind of period has one whole
// string per English suffix: translators get complete phrases and may translate all four alike
enum class Ordinal {
    St,
    Nd,
    Rd,
    Th
};

Ordinal ordinal(int n)
{
    const int lastTwo = n % 100, last = n % 10;
    if (lastTwo >= 11 && lastTwo <= 13)
        return Ordinal::Th;
    return last == 1 ? Ordinal::St : last == 2 ? Ordinal::Nd : last == 3 ? Ordinal::Rd : Ordinal::Th;
}

QString millenniumBC(int n)
{
    switch (ordinal(n)) {
    case Ordinal::St:
        return i18nc("@label timeline period, %1 is a millennium number ending in 1", "%1st millennium BC", n);
    case Ordinal::Nd:
        return i18nc("@label timeline period, %1 is a millennium number ending in 2", "%1nd millennium BC", n);
    case Ordinal::Rd:
        return i18nc("@label timeline period, %1 is a millennium number ending in 3", "%1rd millennium BC", n);
    case Ordinal::Th:
        break;
    }
    return i18nc("@label timeline period, %1 is a millennium number", "%1th millennium BC", n);
}

QString centuryBC(int n)
{
    switch (ordinal(n)) {
    case Ordinal::St:
        return i18nc("@label timeline period, %1 is a century number ending in 1", "%1st century BC", n);
    case Ordinal::Nd:
        return i18nc("@label timeline period, %1 is a century number ending in 2", "%1nd century BC", n);
    case Ordinal::Rd:
        return i18nc("@label timeline period, %1 is a century number ending in 3", "%1rd century BC", n);
    case Ordinal::Th:
        break;
    }
    return i18nc("@label timeline period, %1 is a century number", "%1th century BC", n);
}

QString centuryAD(int n)
{
    switch (ordinal(n)) {
    case Ordinal::St:
        return i18nc("@label timeline period, %1 is a century number ending in 1", "%1st century", n);
    case Ordinal::Nd:
        return i18nc("@label timeline period, %1 is a century number ending in 2", "%1nd century", n);
    case Ordinal::Rd:
        return i18nc("@label timeline period, %1 is a century number ending in 3", "%1rd century", n);
    case Ordinal::Th:
        break;
    }
    return i18nc("@label timeline period, %1 is a century number", "%1th century", n);
}
}

std::optional<Timeline::Date> Timeline::findDate(const QString &sentence, int nowYear)
{
    struct Found {
        qsizetype at;
        Date date;
    };
    QList<Found> found;
    auto add = [&found](qsizetype at, double value, const QString &label, int precision) {
        found.append({at, {value, label.simplified(), precision}});
    };

    // 14 October 1066 (AD), October 14, 1066
    static const QRegularExpression dayMonthYear =
        re(QStringLiteral("\\b(\\d{1,2})\\s+") + Months + QStringLiteral("\\s+(\\d{1,4})\\b(?:\\s*") + Era + QStringLiteral(")?"));
    static const QRegularExpression monthDayYear =
        re(QStringLiteral("\\b") + Months + QStringLiteral("\\s+(\\d{1,2}),\\s*(\\d{1,4})\\b(?:\\s*") + Era + QStringLiteral(")?"));
    // October 1066 (not "May 5": three or four digits)
    static const QRegularExpression monthYear = re(QStringLiteral("\\b") + Months + QStringLiteral("\\s+(\\d{3,4})\\b(?:\\s*") + Era + QStringLiteral(")?"));
    // 31 BC, 10,000 BCE, AD 476, 476 AD
    static const QRegularExpression yearEra = re(QStringLiteral("\\b(\\d{1,2},\\d{3}|\\d{1,4})\\s*") + Era + QStringLiteral("(?![a-z])"));
    static const QRegularExpression eraYear = re(QStringLiteral("\\b(AD|A\\.D\\.)\\s*(\\d{1,4})\\b"));
    // the 1840s, 1990s
    static const QRegularExpression decade = re(QStringLiteral("\\b((?:1\\d|20)\\d0)s\\b"));
    // the 4th century BC, 19th-century
    static const QRegularExpression century = re(QStringLiteral("\\b(\\d{1,2})(?:st|nd|rd|th)[\\s-]+century(?:\\s*") + Era + QStringLiteral(")?"));
    // 7,700 years ago, 66 million years ago
    static const QRegularExpression ago = re(QStringLiteral("\\b(\\d[\\d,]*(?:\\.\\d+)?)\\s*(million|billion|thousand)?\\s+years\\s+ago\\b"));
    // a year on its own, not a quantity or a part of a larger number
    static const QRegularExpression plainYear = re(QStringLiteral(
        "(?<![\\d,.$\\x{00A3}\\x{20AC}#\\-\\x{2013}/])\\b(1\\d{3}|20\\d{2})\\b(?![,.]\\d)(?!\\s*(?:km|m|kg|g|cm|mm|ft|feet|foot|miles?|metres?|meters?|"
        "kilometres?|kilometers?|people|persons|inhabitants|residents|%|per\\s?cent|square|sq|tons?|tonnes?|units|copies|pages|\\x{00B0}|"
        "years?\\s+old|years|months|weeks|days|hours|men|women|soldiers|troops|ships|cavalry|horses|children|students|words|votes|"
        "hectares|acres|calories|watts?|volts?|mph|km/h|BC|BCE|AD|CE)\\b)"));
    // "1000–2000 cavalry": a range too wide to be years is a count
    static const QRegularExpression range(QStringLiteral("^\\s*[\\-\\x{2013}]\\s*(\\d{4})\\b"));

    for (auto it = dayMonthYear.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        const double y = year(m.captured(3), m.captured(4));
        add(m.capturedStart(), y + (monthNumber(m.captured(2)) - 1) / 12.0 + (m.captured(1).toInt() - 1) / 365.0, m.captured(0), 3);
    }
    for (auto it = monthDayYear.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        const double y = year(m.captured(3), m.captured(4));
        add(m.capturedStart(), y + (monthNumber(m.captured(1)) - 1) / 12.0 + (m.captured(2).toInt() - 1) / 365.0, m.captured(0), 3);
    }
    for (auto it = monthYear.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        add(m.capturedStart(), year(m.captured(2), m.captured(3)) + (monthNumber(m.captured(1)) - 1) / 12.0, m.captured(0), 2);
    }
    for (auto it = yearEra.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        add(m.capturedStart(), year(m.captured(1), m.captured(2)), m.captured(0), 1);
    }
    for (auto it = eraYear.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        add(m.capturedStart(), m.captured(2).toDouble(), m.captured(0), 1);
    }
    for (auto it = decade.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        add(m.capturedStart(), m.captured(1).toDouble() + 5, m.captured(0), 0); // the middle of the decade
    }
    for (auto it = century.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        const int c = m.captured(1).toInt();
        if (c < 1 || c > 21)
            continue;
        add(m.capturedStart(), isBc(m.captured(2)) ? -(c - 1) * 100.0 - 50 : (c - 1) * 100.0 + 50, m.captured(0), 0);
    }
    for (auto it = ago.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        QString digits = m.captured(1);
        digits.remove(QLatin1Char(','));
        const QString scale = m.captured(2).toLower();
        const double multiplier = scale == QLatin1String("billion") ? 1e9
            : scale == QLatin1String("million")                     ? 1e6
            : scale == QLatin1String("thousand")                    ? 1e3
                                                                    : 1;
        const double years = digits.toDouble() * multiplier;
        if (years < 1)
            continue;
        add(m.capturedStart(), nowYear - years, m.captured(0), 0);
    }
    for (auto it = plainYear.globalMatch(sentence); it.hasNext();) {
        const auto m = it.next();
        const auto r = range.matchView(QStringView(sentence).mid(m.capturedEnd()));
        if (r.hasMatch() && std::abs(r.captured(1).toInt() - m.captured(1).toInt()) > 150)
            continue;
        add(m.capturedStart(), m.captured(1).toDouble(), m.captured(1), 1);
    }
    if (found.isEmpty())
        return std::nullopt;
    // the first in the sentence; at the same place, the most precise (a full date contains its year)
    std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        if (a.at != b.at)
            return a.at < b.at;
        return a.date.precision > b.date.precision;
    });
    // "14 October 1066": the day-month-year match starts at "14", the year alone at "1066"; a full date that contains an
    // earlier-starting partial match wins, so prefer a match that covers the first one found
    const Found first = found.first();
    for (const Found &f : std::as_const(found))
        if (f.date.precision > first.date.precision && f.at <= first.at + 2 && f.date.label.contains(first.date.label))
            return f.date;
    return first.date;
}

QList<Timeline::Event> Timeline::fromPassages(const QList<Passage> &passages, const ArticleRef &article, int nowYear, int max)
{
    QList<Event> out;
    for (const Passage &p : passages) {
        if (p.section == Extract::keyFactsSection()) // the infobox: labels and values, not sentences
            continue;
        for (const auto splitSentences = Rag::splitSentences(p.text); const auto &[a, b] : splitSentences) {
            QString sentence = p.text.mid(a, b - a).simplified();
            if (sentence.size() < 25)
                continue;
            const auto date = findDate(sentence, nowYear);
            if (!date)
                continue;
            if (sentence.size() > 450)
                sentence = sentence.left(447).trimmed() + QStringLiteral("…");
            out.append({*date, sentence, article, p.section});
            if (out.size() >= max)
                return out;
        }
    }
    return out;
}

QList<Timeline::Event> Timeline::sorted(const QList<Event> &events)
{
    QList<Event> out;
    QSet<QString> seen;
    for (const Event &e : events) {
        const QString key = e.text.simplified().toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out << e;
    }
    std::stable_sort(out.begin(), out.end(), [](const Event &a, const Event &b) {
        if (std::floor(a.date.value) != std::floor(b.date.value))
            return a.date.value < b.date.value;
        if (a.date.precision != b.date.precision)
            return a.date.precision > b.date.precision;
        return a.date.value < b.date.value;
    });
    return out;
}

QString Timeline::period(double value)
{
    if (value < -1e9)
        return i18nc("@label timeline period", "Billions of years ago");
    if (value < -1e6)
        return i18nc("@label timeline period", "Millions of years ago");
    if (value < -10000)
        return i18nc("@label timeline period", "Over 10,000 years ago");
    if (value < -1000)
        return millenniumBC(int(std::ceil(-value / 1000.0)));
    // there is no year 0: 100 BC to 1 BC is the 1st century BC, AD 1 to 100 the 1st century
    if (value < 0)
        return centuryBC(std::max(1, int(std::ceil(-value / 100.0))));
    if (value < 1000)
        return centuryAD(std::max(1, int(std::ceil(value / 100.0))));
    // the year as digits, without the locale's thousands separator ("1800s", not "1,800s")
    return i18nc("@label timeline period, %1 is the year a century starts, such as 1800", "%1s", QString::number(int(std::floor(value / 100.0)) * 100));
}

QString Timeline::markdown(const QString &title, const QList<Event> &events)
{
    QStringList out{QStringLiteral("# ") + i18nc("@title document heading, %1 is an article or project title", "Timeline: %1", title), QString()};
    QString current;
    for (const Event &e : events) {
        const QString p = period(e.date.value);
        if (p != current) {
            current = p;
            out << QStringLiteral("## ") + p << QString();
        }
        out << QStringLiteral("- **%1**: %2 (%3)").arg(e.date.label, e.text, Projects::articleLink(e.article));
    }
    return out.join(QLatin1Char('\n')).trimmed() + QLatin1Char('\n');
}
