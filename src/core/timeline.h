// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "extract.h"
#include "projects.h"

#include <QList>
#include <QString>
#include <optional>

// Timelines: the dated sentences of articles, in date order. Read from the articles' own prose with patterns, not
// written by the model, so an event says exactly what its article says and nothing needs Ollama.
namespace Timeline
{
struct Date {
    double value = 0; // years, BC negative ("31 BC" -1 rounds to -31); a month and day add a fraction
    QString label; // as the article writes it: "14 October 1066", "the 1840s", "7,700 years ago"
    int precision = 1; // 0 approximate (a decade, a century, years ago), 1 a year, 2 a month, 3 a day
};
struct Event {
    Date date;
    QString text; // the sentence
    ArticleRef article;
    QString section;
};

// The first date a sentence gives, if any: a full date, a month and year, a year with AD or BC, a decade, a century,
// "N years ago" (counted back from `nowYear`), or a year on its own from 1000 to 2099 that is not a quantity ("1500
// metres"). Section numbers, page numbers and figures are not dates.
std::optional<Date> findDate(const QString &sentence, int nowYear);
// Every dated sentence of an article's passages (not the infobox's facts), in the order the article gives them
QList<Event> fromPassages(const QList<Passage> &passages, const ArticleRef &article, int nowYear, int max = 120);
// In date order (the more precise first for the same year); the same sentence twice is kept once
QList<Event> sorted(const QList<Event> &events);
// The heading an event goes under: its century ("1800s", "1st century BC"), its millennium before 1000 BC, and further
// back "Over 10,000 years ago", "Millions of years ago", "Billions of years ago"
QString period(double value);
// As Markdown: a heading per period, a line per event with its date and its article linked (zim://)
QString markdown(const QString &title, const QList<Event> &events);
}
