// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Highlights and their notes, for the article page and the Notes page (core: Highlights). Every change clears the
// rendered-article cache, so an article shows its highlights as they now are.
class Notes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QStringList colors READ colors CONSTANT)

public:
    static Notes *create(QQmlEngine *, QJSEngine *);

    int count() const;
    QStringList colors() const;

    // The highlight's id, or 0 (with a message: the passage is too short to find again in the page)
    Q_INVOKABLE qint64 add(qint64 libraryId, const QString &path, const QString &title, const QString &text, const QString &color = {},
                           const QString &note = {});
    Q_INVOKABLE bool tooShort(const QString &text) const;
    Q_INVOKABLE bool setNote(qint64 id, const QString &note);
    Q_INVOKABLE bool setColor(qint64 id, const QString &color);
    Q_INVOKABLE bool remove(qint64 id);
    // [{id, libraryId, path, title, text, color, note, created, articleKey}]
    Q_INVOKABLE QVariantList forArticle(qint64 libraryId, const QString &path) const;
    Q_INVOKABLE QVariantList all(const QString &color = {}, const QString &text = {}) const;

    // The highlights shown (by id) as Markdown, grouped by article
    Q_INVOKABLE QString markdown(const QVariantList &ids) const;
    // Saved to a Markdown file (links to articles become a list of the articles cited); an error, or empty
    Q_INVOKABLE QString exportTo(const QUrl &file, const QVariantList &ids) const;
    // As a new document of a project: its id, or 0
    Q_INVOKABLE qint64 toProject(qint64 projectId, const QString &title, const QVariantList &ids) const;
    // Kept as a saved excerpt of a project, with its note
    Q_INVOKABLE bool addToProject(qint64 projectId, qint64 id) const;

Q_SIGNALS:
    void changed();
    void message(const QString &text);

private:
    explicit Notes(QObject *parent = nullptr);
    void touched();
};
