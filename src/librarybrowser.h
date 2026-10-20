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
    Q_PROPERTY(QString citationStyle READ citationStyle WRITE setCitationStyle NOTIFY citationStyleChanged)
    Q_PROPERTY(QVariantList citationStyles READ citationStyles CONSTANT)

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
    // {found, libraryId, path, title, libraryTitle, toc: [{id, text, level}], hasCoordinates, lat, lon}; records the visit
    // in "Recently read"
    Q_INVOKABLE QVariantMap articleInfo(const QUrl &url) const;
    Q_INVOKABLE bool isArticleUrl(const QUrl &url) const;

    // The article cited in every style: [{style, label, text, html}], the chosen style first
    Q_INVOKABLE QVariantList cite(qint64 libraryId, const QString &path, const QString &title) const;
    // Plain text and HTML (italics kept when pasted into a word processor) on the clipboard
    Q_INVOKABLE void copyCitation(const QString &text, const QString &html) const;

    // A word from an installed Wiktionary: {found, word, pronunciation, parts: [{name, senses: [{definition, example}]}],
    // libraryId, path, library} | {found: false, word, hasDictionary}
    Q_INVOKABLE QVariantMap define(const QString &word) const;
    Q_INVOKABLE bool hasDictionary() const;

    // articles across the whole window, or in a reading column (open articles reload)
    bool fullWidth() const;
    void setFullWidth(bool full);

    QString citationStyle() const;
    void setCitationStyle(const QString &style);
    QVariantList citationStyles() const;  // [{style, label}]

Q_SIGNALS:
    void fullWidthChanged();
    void citationStyleChanged();

private:
    explicit LibraryBrowser(QObject *parent = nullptr);
};
