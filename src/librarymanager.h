// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "librarymodels.h"

class CatalogFeed;
class DownloadManager;
class QQmlEngine;
class QJSEngine;

// Everything the Library Manager page does: the catalog, downloads, and the installed libraries.
class LibraryManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(InstalledModel *installed READ installed CONSTANT)
    Q_PROPERTY(CatalogModel *catalog READ catalog CONSTANT)
    Q_PROPERTY(DownloadsModel *downloads READ downloads CONSTANT)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(double freeSpace READ freeSpace NOTIFY folderChanged)
    Q_PROPERTY(int activeDownloads READ activeDownloads NOTIFY downloadsChanged)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged)
    Q_PROPERTY(QDateTime catalogFetchedAt READ catalogFetchedAt NOTIFY catalogChanged)
    Q_PROPERTY(bool aria2Available READ aria2Available CONSTANT)

public:
    // Created once in main() as a child of the application, so it (and any running aria2c) goes before the app does
    static LibraryManager *createInstance(QObject *parent);
    static LibraryManager *instance();
    static LibraryManager *create(QQmlEngine *, QJSEngine *);

    // After the database is open: recover interrupted downloads and pick up files already in the folder.
    void initialize();

    InstalledModel *installed() { return &m_installed; }
    CatalogModel *catalog() { return &m_catalog; }
    DownloadsModel *downloads() { return &m_downloads; }
    QString folder() const;
    double freeSpace() const;
    int activeDownloads() const;
    bool catalogLoading() const;
    QString catalogError() const;
    QDateTime catalogFetchedAt() const;
    bool aria2Available() const;

    Q_INVOKABLE void loadCatalog();     // from the cache if fresh
    Q_INVOKABLE void refreshCatalog();  // from Kiwix
    Q_INVOKABLE void download(int catalogRow);
    Q_INVOKABLE void updateLibrary(qint64 libraryId);
    Q_INVOKABLE void cancelDownload(qint64 id);
    Q_INVOKABLE void resumeDownload(qint64 id);
    Q_INVOKABLE void removeDownload(qint64 id);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void addFile(const QUrl &file);
    Q_INVOKABLE void removeLibrary(qint64 id, bool deleteFile);
    Q_INVOKABLE bool setFolder(const QUrl &folder);
    Q_INVOKABLE QVariantMap details(qint64 id) const;
    Q_INVOKABLE QVariantList libraryList() const;  // [{libraryId, title}] of the installed libraries whose file is there

Q_SIGNALS:
    void folderChanged();
    void downloadsChanged();
    void catalogChanged();
    void message(const QString &text, bool isError);

private:
    explicit LibraryManager(QObject *parent = nullptr);
    void startDownload(const CatalogEntry &entry);
    void reloadInstalled();
    void reloadDownloads();
    void syncCatalogState();

    CatalogFeed *m_feed;
    DownloadManager *m_downloader;
    InstalledModel m_installed;
    CatalogModel m_catalog;
    DownloadsModel m_downloads;
    QDateTime m_lastFetch;
};
