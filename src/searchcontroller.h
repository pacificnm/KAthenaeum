// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Searching the installed libraries from QML: title suggestions while typing (quick, on the UI thread), and full
// searches through the ZIMs' own indexes on a worker thread (a query runs up to a dozen Xapian searches).
class SearchController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Search)
    QML_SINGLETON
    Q_PROPERTY(QString query READ query NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int elapsedMs READ elapsedMs NOTIFY resultsChanged)

public:
    static SearchController *create(QQmlEngine *, QJSEngine *);

    QString query() const { return m_query; }
    QVariantList results() const { return m_results; }
    bool running() const { return m_running; }
    int elapsedMs() const { return m_elapsedMs; }

    // libraryId -1 = every installed library. Results: [{libraryId, libraryTitle, path, title, section, snippet, titleMatch}]
    Q_INVOKABLE void search(const QString &query, qint64 libraryId = -1);
    // Article titles starting like `text`, across libraries: [{libraryId, libraryTitle, path, title}]
    Q_INVOKABLE QVariantList suggest(const QString &text, qint64 libraryId = -1) const;

Q_SIGNALS:
    void resultsChanged();
    void runningChanged();

private:
    explicit SearchController(QObject *parent = nullptr);

    QString m_query;
    QVariantList m_results;
    bool m_running = false;
    int m_elapsedMs = 0;
    quint64 m_generation = 0;  // a newer search makes an older one's results stale
};
