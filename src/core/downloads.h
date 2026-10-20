// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <atomic>
#include <memory>
#include <optional>

class QProcess;

// What aria2c's progress summary says: "[#85a9a7 1.1MiB/22MiB(5%) CN:1 DL:592KiB ETA:36s]"
struct Aria2Progress {
    qint64 done = 0;
    qint64 total = 0;
    int percent = 0;
    qint64 speed = 0; // bytes per second
    int etaSeconds = -1; // -1 = unknown
};

namespace Aria2
{
std::optional<Aria2Progress> parseLine(QStringView line);
qint64 parseSize(QStringView text); // "1.1MiB" -> 1153433, "0B" -> 0, -1 if unreadable
int parseDuration(QStringView text); // "1h2m3s" -> 3723, -1 if unreadable
QStringList arguments(const QString &folder, const QUrl &url);
QString explainExitCode(int code); // aria2c's documented exit codes, in words
}

struct DownloadRecord {
    qint64 id = 0;
    QUrl url;
    QString filename;
    QString folder;
    QString title;
    qint64 totalBytes = 0;
    qint64 doneBytes = 0;
    QString status; // queued | downloading | verifying | complete | failed | cancelled
    QString error;
    // live only, not stored
    qint64 speed = 0;
    int etaSeconds = -1;

    QString path() const;
    bool isActive() const;
    double progress() const; // 0..1
};

// Runs aria2c, one process per download, and proves each finished file is a complete ZIM before it is registered.
class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    // Rows left active by a previous run cannot be tracked: their aria2c died with that run (PR_SET_PDEATHSIG).
    // Marks them failed; the partial file stays, so Resume continues where it stopped.
    void reconcile();

    // Starts downloading. Returns an empty string, or why it could not start.
    QString start(const QUrl &url, const QString &filename, const QString &title, qint64 size, const QString &folder);
    bool cancel(qint64 id);
    QString resume(qint64 id);
    // Forgets a finished, failed or cancelled download; a partial file is deleted with it.
    bool remove(qint64 id);

    QList<DownloadRecord> list() const; // newest first
    std::optional<DownloadRecord> byId(qint64 id) const;
    int activeCount() const;

    static QString aria2Program(); // empty if aria2c is not installed

Q_SIGNALS:
    void changed(qint64 id);
    void listChanged();
    void completed(qint64 id, qint64 libraryId);
    void failed(qint64 id, const QString &message);

private:
    QString launch(qint64 id);
    void readOutput(qint64 id);
    void processFinished(qint64 id, int exitCode);
    void verified(qint64 id, const QString &problem);
    void setStatus(qint64 id, const QString &status, const QString &error = {});

    QHash<qint64, QProcess *> m_processes;
    QHash<qint64, Aria2Progress> m_live;
    QHash<qint64, QString> m_outputTail;
    QSet<qint64> m_cancelled;
    // set when the manager goes (the app closes): a verification in progress stops instead of holding up the exit
    std::shared_ptr<std::atomic<bool>> m_stopVerifying = std::make_shared<std::atomic<bool>>(false);
    void startVerifying(qint64 id);
};
