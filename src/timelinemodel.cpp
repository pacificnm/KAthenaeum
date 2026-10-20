// SPDX-License-Identifier: GPL-3.0-or-later
#include "timelinemodel.h"

#include "core/extract.h"
#include "core/projects.h"
#include "core/settings.h"
#include "core/zimreader.h"
#include "core/zimsearch.h"
#include "projectlist.h"
#include "projectmodel.h"

#include <KLocalizedString>
#include <QDate>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace
{
QString keyOf(const ArticleRef &a)
{
    return QString::number(a.libraryId) + QLatin1Char('/') + a.path;
}
}

TimelineModel::TimelineModel(QObject *parent)
    : QObject(parent)
{
}

void TimelineModel::loadArticle(qint64 libraryId, const QString &path, const QString &title)
{
    load(title, {{libraryId, path, title}});
}

void TimelineModel::loadProject(qint64 projectId)
{
    const auto p = Projects::byId(projectId);
    QList<ArticleRef> articles;
    for (const auto qStringLiteral = Projects::items(projectId, QStringLiteral("article")); const ProjectItem &i : qStringLiteral)
        articles << i.article;
    std::reverse(articles.begin(), articles.end());  // as they were added
    load(p ? p->title : QString(), articles);
}

void TimelineModel::load(const QString &title, const QList<ArticleRef> &articles)
{
    const quint64 generation = ++m_generation;
    m_title = title;
    m_events.clear();
    m_article.clear();
    m_loading = true;
    Q_EMIT loadingChanged();
    Q_EMIT loaded();
    Q_EMIT filterChanged();
    QList<qint64> open;
    for (const ArticleRef &a : articles)
        if (!open.contains(a.libraryId))
            open << a.libraryId;
    ZimSearch::prepare(open);  // archives opened here: opening reads the database
    const int now = QDate::currentDate().year();
    auto *watcher = new QFutureWatcher<QList<Timeline::Event>>(this);
    connect(watcher, &QFutureWatcher<QList<Timeline::Event>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_events = watcher->result();
        m_loading = false;
        Q_EMIT loadingChanged();
        Q_EMIT loaded();
        Q_EMIT filterChanged();
    });
    watcher->setFuture(QtConcurrent::run([articles, now] {
        QList<Timeline::Event> out;
        for (const ArticleRef &a : articles) {
            const auto content = ZimReader::content(a.libraryId, a.path);
            if (!content)
                continue;
            out += Timeline::fromPassages(Extract::extract(content->data).passages, {a.libraryId, content->path, a.title}, now, 400);
        }
        return Timeline::sorted(out);
    }));
}

QVariantList TimelineModel::articles() const
{
    QList<std::pair<QString, QString>> order;
    QHash<QString, int> counts;
    for (const Timeline::Event &e : m_events) {
        const QString key = keyOf(e.article);
        if (!counts.contains(key))
            order.append({key, e.article.title});
        ++counts[key];
    }
    std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) {
        return a.second.localeAwareCompare(b.second) < 0;
    });
    QVariantList out;
    for (const auto &[key, title] : order)
        out.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("title"), title}, {QStringLiteral("count"), counts.value(key)}});
    return out;
}

void TimelineModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    Q_EMIT filterChanged();
}

void TimelineModel::setArticle(const QString &key)
{
    if (key == m_article)
        return;
    m_article = key;
    Q_EMIT filterChanged();
}

QList<Timeline::Event> TimelineModel::shown() const
{
    const QString q = m_query.simplified();
    QList<Timeline::Event> out;
    for (const Timeline::Event &e : m_events) {
        if (!m_article.isEmpty() && keyOf(e.article) != m_article)
            continue;
        if (!q.isEmpty() && !e.text.contains(q, Qt::CaseInsensitive) && !e.date.label.contains(q, Qt::CaseInsensitive))
            continue;
        out << e;
    }
    return out;
}

QVariantList TimelineModel::events() const
{
    QVariantList out;
    for (const auto events = shown(); const Timeline::Event &e : events)
        out.append(QVariantMap{{QStringLiteral("period"), Timeline::period(e.date.value)},
                               {QStringLiteral("label"), e.date.label},
                               {QStringLiteral("approximate"), e.date.precision == 0},
                               {QStringLiteral("text"), e.text},
                               {QStringLiteral("libraryId"), e.article.libraryId},
                               {QStringLiteral("path"), e.article.path},
                               {QStringLiteral("articleTitle"), e.article.title},
                               {QStringLiteral("section"), e.section}});
    return out;
}

qint64 TimelineModel::toProject(qint64 projectId) const
{
    QString md = Timeline::markdown(m_title, shown());
    md = md.section(QLatin1Char('\n'), 2);  // the document's own title is its heading
    const qint64 id = Projects::createDocument(projectId, i18n("Timeline: %1", m_title), md.trimmed() + QLatin1Char('\n'));
    if (id > 0)
        Q_EMIT ProjectsNotifier::instance()->changed();
    return id;
}

QString TimelineModel::exportTo(const QUrl &file) const
{
    return writeTextFile(file, Projects::exportMarkdown(Timeline::markdown(m_title, shown()), Citations::style(AppSettings::citationStyle())));
}

QString TimelineModel::exportName() const
{
    return QStringLiteral("timeline-") + Projects::slug(m_title, QStringLiteral("timeline")) + QStringLiteral(".md");
}
