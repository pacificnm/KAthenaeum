// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "notes.h"

#include "articlestore.h"
#include "core/highlights.h"
#include "core/projects.h"
#include "core/settings.h"
#include "projectlist.h"
#include "projectmodel.h"

#include <KLocalizedString>
#include <QQmlEngine>

namespace
{
QVariantMap toVariant(const HighlightRecord &h)
{
    return {{QStringLiteral("id"), h.id},
            {QStringLiteral("libraryId"), h.libraryId},
            {QStringLiteral("path"), h.path},
            {QStringLiteral("title"), h.title},
            {QStringLiteral("text"), h.text},
            {QStringLiteral("color"), h.color},
            {QStringLiteral("note"), h.note},
            {QStringLiteral("created"), h.createdAt.toLocalTime()},
            {QStringLiteral("articleKey"), QStringLiteral("%1/%2").arg(h.libraryId).arg(h.path)}};
}

QList<HighlightRecord> byIds(const QVariantList &ids)
{
    QList<HighlightRecord> out;
    for (const QVariant &id : ids)
        if (const auto h = Highlights::byId(id.toLongLong()))
            out << *h;
    // grouped by article, in the order shown
    QList<HighlightRecord> grouped;
    while (!out.isEmpty()) {
        const HighlightRecord first = out.takeFirst();
        grouped << first;
        for (qsizetype i = 0; i < out.size();)
            if (out.at(i).libraryId == first.libraryId && out.at(i).path == first.path)
                grouped << out.takeAt(i);
            else
                ++i;
    }
    return grouped;
}
}

Notes::Notes(QObject *parent)
    : QObject(parent)
{
}

Notes *Notes::create(QQmlEngine *engine, QJSEngine *)
{
    return new Notes(engine);
}

int Notes::count() const
{
    return Highlights::count();
}

QStringList Notes::colors() const
{
    return Highlights::colors();
}

void Notes::touched()
{
    ArticleStore::clear();  // the article shows its highlights as they now are
    Q_EMIT changed();
}

qint64 Notes::add(qint64 libraryId, const QString &path, const QString &title, const QString &text, const QString &color, const QString &note)
{
    if (Highlights::tooShort(text)) {
        Q_EMIT message(i18n("Select a few more words to highlight: a passage this short could not be found again in the article."));
        return 0;
    }
    const qint64 id = Highlights::add(libraryId, path, title, text, color, note);
    if (id > 0)
        touched();
    return id;
}

bool Notes::tooShort(const QString &text) const
{
    return Highlights::tooShort(text);
}

bool Notes::setNote(qint64 id, const QString &note)
{
    const bool ok = Highlights::setNote(id, note);
    if (ok)
        touched();
    return ok;
}

bool Notes::setColor(qint64 id, const QString &color)
{
    const bool ok = Highlights::setColor(id, color);
    if (ok)
        touched();
    return ok;
}

bool Notes::remove(qint64 id)
{
    const bool ok = Highlights::remove(id);
    if (ok)
        touched();
    return ok;
}

QVariantList Notes::forArticle(qint64 libraryId, const QString &path) const
{
    QVariantList out;
    for (const auto marks = Highlights::forArticle(libraryId, path); const HighlightRecord &h : marks)
        out << toVariant(h);
    return out;
}

QVariantList Notes::all(const QString &color, const QString &text) const
{
    QVariantList out;
    for (const auto rows = Highlights::all(color, text); const HighlightRecord &h : rows)
        out << toVariant(h);
    return out;
}

QString Notes::markdown(const QVariantList &ids) const
{
    return Highlights::markdown(byIds(ids));
}

QString Notes::exportTo(const QUrl &file, const QVariantList &ids) const
{
    const QString body = Projects::exportMarkdown(markdown(ids), Citations::style(AppSettings::citationStyle()));
    return writeTextFile(file, QStringLiteral("# %1\n\n%2\n").arg(i18n("Notes"), body.trimmed()));
}

qint64 Notes::toProject(qint64 projectId, const QString &title, const QVariantList &ids) const
{
    const qint64 id = Projects::createDocument(projectId, title, markdown(ids));
    if (id > 0)
        Q_EMIT ProjectsNotifier::instance()->changed();
    return id;
}

bool Notes::addToProject(qint64 projectId, qint64 id) const
{
    const auto h = Highlights::byId(id);
    if (!h)
        return false;
    const bool ok = Projects::addClip(projectId, h->text, {{h->libraryId, h->path, h->title}}, {}, h->note) > 0;
    Q_EMIT ProjectsNotifier::instance()->changed();
    return ok;
}
