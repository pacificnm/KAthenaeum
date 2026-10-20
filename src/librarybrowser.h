// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// What the reading pages ask for: a library's home, A to Z, overview articles, suggestions, and an article's details.
class LibraryBrowser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool fullWidth READ fullWidth WRITE setFullWidth NOTIFY fullWidthChanged)

public:
    static LibraryBrowser *create(QQmlEngine *, QJSEngine *);

    Q_INVOKABLE QVariantMap libraryInfo(qint64 libraryId) const;
    // {entries: [{path, title}], next} ; letter "A".."Z" or "#", or a title prefix when `prefix` is set
    Q_INVOKABLE QVariantMap titles(qint64 libraryId, const QString &letterOrPrefix, bool prefix, int from, int count) const;
    Q_INVOKABLE QVariantMap randomArticle(qint64 libraryId) const;
    // [{label, prefix, entries: [{path, title}], more}] for Outline of, Index of, Glossary of, Timeline of, List of
    Q_INVOKABLE QVariantList overviews(qint64 libraryId) const;
    Q_INVOKABLE QVariantList suggest(qint64 libraryId, const QString &text) const;
    Q_INVOKABLE QVariantList recent(qint64 libraryId) const;
    Q_INVOKABLE QUrl articleUrl(qint64 libraryId, const QString &path) const;
    // {found, libraryId, path, title, libraryTitle, toc: [{id, text, level}]}; records the visit in "Recently read"
    Q_INVOKABLE QVariantMap articleInfo(const QUrl &url) const;
    Q_INVOKABLE bool isArticleUrl(const QUrl &url) const;

    // articles across the whole window, or in a reading column (open articles reload)
    bool fullWidth() const;
    void setFullWidth(bool full);

Q_SIGNALS:
    void fullWidthChanged();

private:
    explicit LibraryBrowser(QObject *parent = nullptr);
};
