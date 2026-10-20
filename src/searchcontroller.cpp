// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchcontroller.h"

#include "core/library.h"
#include "core/zimreader.h"
#include "core/zimsearch.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QQmlEngine>
#include <QtConcurrentRun>

namespace
{
// Installed libraries whose file is there, with their titles, optionally just one
QHash<qint64, QString> searchable(qint64 only)
{
    QHash<qint64, QString> out;
    for (const auto rows = Library::all(); const LibraryRecord &r : rows)
        if (r.fileExists() && (only < 0 || r.id == only))
            out.insert(r.id, r.title);
    return out;
}
}

SearchController::SearchController(QObject *parent)
    : QObject(parent)
{
}

SearchController *SearchController::create(QQmlEngine *engine, QJSEngine *)
{
    return new SearchController(engine);
}

QVariantList SearchController::suggest(const QString &text, qint64 libraryId) const
{
    QVariantList out;
    if (text.trimmed().isEmpty())
        return out;
    const auto libraries = searchable(libraryId);
    const int each = libraries.size() > 1 ? 4 : 8;
    for (auto it = libraries.cbegin(); it != libraries.cend(); ++it)
        for (const auto suggestions = ZimReader::suggest(it.key(), text, each); const ZimEntry &e : suggestions)
            out.append(QVariantMap{{QStringLiteral("libraryId"), it.key()},
                                   {QStringLiteral("libraryTitle"), it.value()},
                                   {QStringLiteral("path"), e.path},
                                   {QStringLiteral("title"), e.title}});
    return out;
}

void SearchController::search(const QString &query, qint64 libraryId)
{
    const quint64 generation = ++m_generation;
    m_query = query.trimmed();
    if (m_query.isEmpty()) {
        m_results.clear();
        m_running = false;
        Q_EMIT resultsChanged();
        Q_EMIT runningChanged();
        return;
    }
    const auto libraries = searchable(libraryId);
    const QList<qint64> ids = libraries.keys();
    ZimSearch::prepare(ids);  // opens the archives here: the worker cannot read the database

    m_running = true;
    Q_EMIT runningChanged();
    auto *watcher = new QFutureWatcher<QList<ZimSearch::Hit>>(this);
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    connect(watcher, &QFutureWatcher<QList<ZimSearch::Hit>>::finished, this, [this, watcher, generation, libraries, timer] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;  // someone searched again meanwhile
        m_results.clear();
        for (const auto hits = watcher->result(); const ZimSearch::Hit &h : hits)
            m_results.append(QVariantMap{{QStringLiteral("libraryId"), h.libraryId},
                                         {QStringLiteral("libraryTitle"), libraries.value(h.libraryId)},
                                         {QStringLiteral("path"), h.path},
                                         {QStringLiteral("title"), h.title},
                                         {QStringLiteral("section"), h.section},
                                         {QStringLiteral("snippet"), h.snippet},
                                         {QStringLiteral("titleMatch"), h.titleMatch}});
        m_elapsedMs = int(timer->elapsed());
        m_running = false;
        Q_EMIT resultsChanged();
        Q_EMIT runningChanged();
    });
    watcher->setFuture(QtConcurrent::run([query = m_query, ids] {
        return ZimSearch::search(query, ids, 20);
    }));
}
