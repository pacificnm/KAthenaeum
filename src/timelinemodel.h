// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/timeline.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// A timeline for the Timeline page: the dated sentences of one article or of a project's articles, read in a worker,
// in date order, filtered by words and (a project's) by article.
class TimelineModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString title READ title NOTIFY loaded)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int total READ total NOTIFY loaded)
    Q_PROPERTY(QVariantList articles READ articles NOTIFY loaded)  // [{key, title, count}] the events come from
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filterChanged)
    Q_PROPERTY(QString article READ article WRITE setArticle NOTIFY filterChanged)  // a key, or "" for all
    // [{period, label, text, libraryId, path, articleTitle, section, approximate}], filtered
    Q_PROPERTY(QVariantList events READ events NOTIFY filterChanged)

public:
    explicit TimelineModel(QObject *parent = nullptr);

    QString title() const { return m_title; }
    bool loading() const { return m_loading; }
    int total() const { return int(m_events.size()); }
    QVariantList articles() const;
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString article() const { return m_article; }
    void setArticle(const QString &key);
    QVariantList events() const;

    Q_INVOKABLE void loadArticle(qint64 libraryId, const QString &path, const QString &title);
    Q_INVOKABLE void loadProject(qint64 projectId);
    // The events shown, as a document of a project: its id, or 0
    Q_INVOKABLE qint64 toProject(qint64 projectId) const;
    // Saved as Markdown (links to articles become a list of the articles cited); an error, or empty
    Q_INVOKABLE QString exportTo(const QUrl &file) const;
    Q_INVOKABLE QString exportName() const;

Q_SIGNALS:
    void loaded();
    void loadingChanged();
    void filterChanged();

private:
    void load(const QString &title, const QList<ArticleRef> &articles);
    QList<Timeline::Event> shown() const;

    QString m_title;
    bool m_loading = false;
    quint64 m_generation = 0;
    QList<Timeline::Event> m_events;
    QString m_query;
    QString m_article;
};
