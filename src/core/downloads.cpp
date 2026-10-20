// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "downloads.h"

#include "catalog.h"
#include "database.h"
#include "library.h"
#include "zimarchive.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QCoreApplication>
#include <QtConcurrentRun>

#include <csignal>
#include <sys/prctl.h>

namespace
{
constexpr QLatin1StringView Columns("id, url, filename, folder, title, total_bytes, done_bytes, status, error");

DownloadRecord fromQuery(const QSqlQuery &q)
{
    DownloadRecord r;
    r.id = q.value(0).toLongLong();
    r.url = QUrl(q.value(1).toString());
    r.filename = q.value(2).toString();
    r.folder = q.value(3).toString();
    r.title = q.value(4).toString();
    r.totalBytes = q.value(5).toLongLong();
    r.doneBytes = q.value(6).toLongLong();
    r.status = q.value(7).toString();
    r.error = q.value(8).toString();
    return r;
}

void removePartial(const QString &path)
{
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".aria2"));
}
}

// ---- aria2c output ----

qint64 Aria2::parseSize(QStringView text)
{
    static const QRegularExpression re(QStringLiteral("^([0-9]+(?:\\.[0-9]+)?)(B|KiB|MiB|GiB|TiB)$"));
    const auto m = re.matchView(text);
    if (!m.hasMatch())
        return -1;
    static const QHash<QString, double> unit = {{QStringLiteral("B"), 1.0},
                                                {QStringLiteral("KiB"), 1024.0},
                                                {QStringLiteral("MiB"), 1024.0 * 1024},
                                                {QStringLiteral("GiB"), 1024.0 * 1024 * 1024},
                                                {QStringLiteral("TiB"), 1024.0 * 1024 * 1024 * 1024}};
    return qint64(m.captured(1).toDouble() * unit.value(m.captured(2)));
}

int Aria2::parseDuration(QStringView text)
{
    static const QRegularExpression re(QStringLiteral("^(?:([0-9]+)h)?(?:([0-9]+)m)?(?:([0-9]+)s)?$"));
    const auto m = re.matchView(text);
    if (text.isEmpty() || !m.hasMatch())
        return -1;
    return m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toInt();
}

std::optional<Aria2Progress> Aria2::parseLine(QStringView line)
{
    static const QRegularExpression re(QStringLiteral("^\\[#\\w+ (\\S+)/(\\S+)\\((\\d+)%\\)(.*)\\]$"));
    const auto m = re.matchView(line.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    Aria2Progress p;
    p.done = parseSize(m.capturedView(1));
    p.total = parseSize(m.capturedView(2));
    p.percent = m.capturedView(3).toInt();
    for (const auto qLatin1Char = m.capturedView(4).split(QLatin1Char(' '), Qt::SkipEmptyParts); const auto field : qLatin1Char) {
        if (field.startsWith(QLatin1String("DL:")))
            p.speed = std::max<qint64>(0, parseSize(field.mid(3)));
        else if (field.startsWith(QLatin1String("ETA:")))
            p.etaSeconds = parseDuration(field.mid(4));
    }
    if (p.done < 0 || p.total < 0)
        return std::nullopt;
    return p;
}

QStringList Aria2::arguments(const QString &folder, const QUrl &url)
{
    // -c resumes a partial file; the summary prints a progress line every second; the metalink stays in memory so no
    // .meta4 file is left beside the ZIM; aria2 checks the metalink's hashes itself, and we check the ZIM afterwards.
    // --no-conf: the user's own ~/.aria2/aria2.conf must not change how this download behaves (no certificate checks, a
    // command run on completion, another folder); certificates are checked whatever it says.
    return {QStringLiteral("--no-conf=true"), QStringLiteral("--check-certificate=true"), QStringLiteral("-x8"), QStringLiteral("-s8"), QStringLiteral("-c"),
            QStringLiteral("--file-allocation=none"), QStringLiteral("--auto-file-renaming=false"),
            QStringLiteral("--follow-metalink=mem"), QStringLiteral("--summary-interval=1"),
            QStringLiteral("--show-console-readout=false"), QStringLiteral("--console-log-level=warn"),
            QStringLiteral("--dir"), folder, url.toString(QUrl::FullyEncoded)};
}

QString Aria2::explainExitCode(int code)
{
    switch (code) {
    case 3: return QStringLiteral("the file was not found on the server");
    case 6: return QStringLiteral("a network problem stopped the download");
    case 9: return QStringLiteral("there is not enough free disk space");
    case 13: return QStringLiteral("a file with that name already exists");
    case 15:
    case 16: return QStringLiteral("the file could not be written");
    case 19: return QStringLiteral("the server name could not be resolved (are you online?)");
    case 24: return QStringLiteral("the server refused the request");
    case 32: return QStringLiteral("the downloaded data did not match its checksum");
    default: return QStringLiteral("aria2c stopped with error %1").arg(code);
    }
}

// ---- records ----

QString DownloadRecord::path() const
{
    return QDir(folder).filePath(filename);
}

bool DownloadRecord::isActive() const
{
    return status == QLatin1String("queued") || status == QLatin1String("downloading") || status == QLatin1String("verifying");
}

double DownloadRecord::progress() const
{
    if (status == QLatin1String("complete"))
        return 1.0;
    return totalBytes > 0 ? std::clamp(double(doneBytes) / double(totalBytes), 0.0, 1.0) : 0.0;
}

// ---- manager ----

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
{
    // ~QCoreApplication waits for the thread pool before it deletes its children (this manager among them), so the
    // verification must be told to stop when the app starts to quit, not when this object goes
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [stop = m_stopVerifying] {
            stop->store(true);
        });
}

DownloadManager::~DownloadManager()
{
    m_stopVerifying->store(true);  // the pool's thread returns within one read; the row stays "verifying" for next time
    // SIGTERM, not a kill, so aria2c writes its control file and Resume loses nothing
    for (QProcess *p : std::as_const(m_processes)) {
        p->disconnect(this);
        p->terminate();
        p->waitForFinished(3000);
    }
}

QString DownloadManager::aria2Program()
{
    return QStandardPaths::findExecutable(QStringLiteral("aria2c"));
}

void DownloadManager::reconcile()
{
    // a file that was being verified is complete: verify it again rather than ask for a Resume
    QList<qint64> verifying;
    QSqlQuery v(QStringLiteral("SELECT id FROM downloads WHERE status = 'verifying'"));
    while (v.next())
        verifying << v.value(0).toLongLong();
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE downloads SET status = 'failed', error = ? WHERE status IN ('queued', 'downloading')"));
    q.addBindValue(QStringLiteral("Interrupted when KAthenaeum closed. Press Resume to continue."));
    q.exec();
    for (qint64 id : std::as_const(verifying))
        startVerifying(id);
}

QList<DownloadRecord> DownloadManager::list() const
{
    QList<DownloadRecord> out;
    QSqlQuery q(QStringLiteral("SELECT %1 FROM downloads ORDER BY id DESC LIMIT 50").arg(Columns));
    while (q.next()) {
        DownloadRecord r = fromQuery(q);
        if (const auto live = m_live.constFind(r.id); live != m_live.cend() && r.status == QLatin1String("downloading")) {
            r.speed = live->speed;
            r.etaSeconds = live->etaSeconds;
        }
        out.append(r);
    }
    return out;
}

std::optional<DownloadRecord> DownloadManager::byId(qint64 id) const
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT %1 FROM downloads WHERE id = ?").arg(Columns));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    DownloadRecord r = fromQuery(q);
    if (const auto live = m_live.constFind(id); live != m_live.cend()) {
        r.speed = live->speed;
        r.etaSeconds = live->etaSeconds;
    }
    return r;
}

int DownloadManager::activeCount() const
{
    QSqlQuery q(QStringLiteral("SELECT count(*) FROM downloads WHERE status IN ('queued', 'downloading', 'verifying')"));
    return q.next() ? q.value(0).toInt() : 0;
}

QString DownloadManager::start(const QUrl &url, const QString &filename, const QString &title, qint64 size, const QString &folder)
{
    if (const QString problem = Catalog::checkDownloadUrl(url, filename); !problem.isEmpty())
        return problem;
    if (aria2Program().isEmpty())
        return QStringLiteral("aria2c is not installed (sudo apt install aria2)");
    if (!QDir().mkpath(folder))
        return QStringLiteral("cannot create the folder %1").arg(folder);

    const QString path = QDir(folder).filePath(filename);
    for (const auto rows = Library::all(); const LibraryRecord &r : rows)
        if (r.filename() == filename && r.fileExists())
            return QStringLiteral("%1 is already installed").arg(filename);

    // Room for what is still to come; a partial file from an earlier attempt already holds the rest
    const qint64 have = QFileInfo::exists(path + QStringLiteral(".aria2")) ? QFileInfo(path).size() : 0;
    const qint64 free = QStorageInfo(folder).bytesAvailable();
    if (size > 0 && free >= 0 && free < size - have + (64 << 20))
        return QStringLiteral("not enough free space in %1: this needs %2 MB, %3 MB are free")
            .arg(folder, QString::number((size - have) >> 20), QString::number(free >> 20));

    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO downloads (url, filename, folder, title, total_bytes, done_bytes) VALUES (?, ?, ?, ?, ?, ?) RETURNING id"));
    q.addBindValue(url.toString(QUrl::FullyEncoded));
    q.addBindValue(filename);
    q.addBindValue(QDir::cleanPath(folder));
    q.addBindValue(Database::text(title));
    q.addBindValue(size);
    q.addBindValue(have);
    if (!q.exec() || !q.next()) {
        if (q.lastError().nativeErrorCode() == QLatin1String("2067"))  // SQLITE_CONSTRAINT_UNIQUE: the index on active downloads
            return QStringLiteral("%1 is already downloading").arg(filename);
        return QStringLiteral("could not record the download: %1").arg(q.lastError().text());
    }
    const qint64 id = q.value(0).toLongLong();
    Q_EMIT listChanged();
    return launch(id);
}

QString DownloadManager::resume(qint64 id)
{
    const auto r = byId(id);
    if (!r)
        return QStringLiteral("no such download");
    if (r->isActive())
        return {};
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE downloads SET status = 'queued', error = '', finished_at = NULL WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec())
        return QStringLiteral("%1 is already downloading").arg(r->filename);
    Q_EMIT changed(id);
    return launch(id);
}

QString DownloadManager::launch(qint64 id)
{
    const auto r = byId(id);
    if (!r)
        return QStringLiteral("The download no longer exists");
    auto *proc = new QProcess(this);
    proc->setProgram(aria2Program());
    proc->setArguments(Aria2::arguments(r->folder, r->url));
    proc->setProcessChannelMode(QProcess::MergedChannels);
    // In the child, before exec: the kernel sends SIGTERM if KAthenaeum dies, so no orphan keeps writing a file that the
    // database no longer tracks. (Athenaeum learned this the hard way.)
    proc->setChildProcessModifier([] {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id] {
        readOutput(id);
    });
    connect(proc, &QProcess::finished, this, [this, id](int exitCode, QProcess::ExitStatus status) {
        processFinished(id, status == QProcess::NormalExit ? exitCode : -1);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            processFinished(id, -2);
    });
    m_processes.insert(id, proc);
    setStatus(id, QStringLiteral("downloading"));
    proc->start();
    return {};
}

void DownloadManager::readOutput(qint64 id)
{
    QProcess *proc = m_processes.value(id);
    if (!proc)
        return;
    QString &tail = m_outputTail[id];
    std::optional<Aria2Progress> latest;
    while (proc->canReadLine()) {
        const QString line = QString::fromUtf8(proc->readLine()).trimmed();
        if (auto p = Aria2::parseLine(line))
            latest = p;
        else if (!line.isEmpty() && !line.startsWith(QLatin1Char('*')) && !line.startsWith(QLatin1Char('=')) && !line.startsWith(QLatin1Char('-'))
                 && !line.startsWith(QLatin1String("FILE:")))
            tail = QString(tail + line + QLatin1Char('\n')).right(2000);  // QStringBuilder: materialise before right()
    }
    if (!latest)
        return;
    m_live.insert(id, *latest);
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE downloads SET done_bytes = ?, total_bytes = CASE WHEN ? > 0 THEN ? ELSE total_bytes END WHERE id = ?"));
    q.addBindValue(latest->done);
    // aria2's total is rounded (22MiB); the catalog's exact figure is better, so only fill it when we had none
    const auto r = byId(id);
    const qint64 total = r && r->totalBytes > 0 ? r->totalBytes : latest->total;
    q.addBindValue(total);
    q.addBindValue(total);
    q.addBindValue(id);
    q.exec();
    Q_EMIT changed(id);
}

void DownloadManager::processFinished(qint64 id, int exitCode)
{
    QProcess *proc = m_processes.take(id);
    if (!proc)
        return;
    proc->deleteLater();
    m_live.remove(id);
    const QString tail = m_outputTail.take(id).trimmed();
    const auto r = byId(id);
    if (!r)
        return;

    if (m_cancelled.remove(id)) {
        setStatus(id, QStringLiteral("cancelled"));
        return;
    }
    if (exitCode != 0) {
        const QString why = exitCode == -2 ? QStringLiteral("aria2c could not be started")
                          : exitCode == -1 ? QStringLiteral("aria2c crashed")
                                           : Aria2::explainExitCode(exitCode);
        const QString message = tail.isEmpty() ? why : QStringLiteral("%1: %2").arg(why, tail.right(300));
        setStatus(id, QStringLiteral("failed"), message);
        Q_EMIT failed(id, message);
        return;
    }

    // Transferred, not yet proven good: aria2c exiting 0 is not proof enough
    startVerifying(id);
}

void DownloadManager::startVerifying(qint64 id)
{
    const auto r = byId(id);
    if (!r)
        return;
    setStatus(id, QStringLiteral("verifying"));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, id] {
        watcher->deleteLater();
        verified(id, watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([path = r->path(), size = r->totalBytes, stop = m_stopVerifying] {
        return ZimArchive::verify(path, size, stop.get());
    }));
}

void DownloadManager::verified(qint64 id, const QString &problem)
{
    const auto r = byId(id);
    if (!r || problem == ZimArchive::Stopped)
        return;
    if (!problem.isEmpty()) {
        removePartial(r->path());  // a corrupt file must not be registered, and resuming it would not fix it
        const QString message = QStringLiteral("The download finished but did not check out: %1. The file was removed; start it again.").arg(problem);
        setStatus(id, QStringLiteral("failed"), message);
        Q_EMIT failed(id, message);
        return;
    }
    QString error;
    const auto libraryId = Library::add(r->path(), &error);
    if (!libraryId) {
        const QString message = QStringLiteral("Downloaded and verified, but it could not be added: %1").arg(error);
        setStatus(id, QStringLiteral("failed"), message);
        Q_EMIT failed(id, message);
        return;
    }
    QSqlQuery q;
    q.prepare(QStringLiteral("UPDATE downloads SET done_bytes = total_bytes WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    setStatus(id, QStringLiteral("complete"));
    Q_EMIT completed(id, *libraryId);
}

bool DownloadManager::cancel(qint64 id)
{
    QProcess *proc = m_processes.value(id);
    if (!proc)
        return false;
    m_cancelled.insert(id);
    proc->terminate();  // aria2c saves its control file on SIGTERM, so a cancelled download can still be resumed
    return true;
}

bool DownloadManager::remove(qint64 id)
{
    const auto r = byId(id);
    if (!r || r->isActive())
        return false;
    bool installed = false;  // never delete a file a library uses, whatever this row says
    for (const auto rows = Library::all(); const LibraryRecord &lib : rows)
        installed = installed || QFileInfo(lib.path) == QFileInfo(r->path());
    if (r->status != QLatin1String("complete") && !installed)
        removePartial(r->path());
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM downloads WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    Q_EMIT listChanged();
    return true;
}

void DownloadManager::setStatus(qint64 id, const QString &status, const QString &error)
{
    QSqlQuery q;
    const bool done = status == QLatin1String("complete") || status == QLatin1String("failed") || status == QLatin1String("cancelled");
    q.prepare(QStringLiteral("UPDATE downloads SET status = ?, error = ?, finished_at = %1 WHERE id = ?")
                  .arg(done ? QStringLiteral("CURRENT_TIMESTAMP") : QStringLiteral("NULL")));
    q.addBindValue(status);
    q.addBindValue(Database::text(error));
    q.addBindValue(id);
    if (!q.exec())
        qWarning("download %lld: could not set status %s: %s", id, qPrintable(status), qPrintable(q.lastError().text()));
    Q_EMIT changed(id);
}
