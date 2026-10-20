// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QtQml/qqmlregistration.h>

#include "core/catalog.h"
#include "core/downloads.h"
#include "core/library.h"

// Installed libraries, for the Library Manager and the Libraries page.
class InstalledModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by LibraryManager")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        DescriptionRole,
        PathRole,
        FilenameRole,
        FlavourRole,
        LanguageRole,
        DateRole,
        SizeRole,
        ArticleCountRole,
        IconRole,
        HasFulltextRole,
        IsWikipediaRole,
        MissingRole,
        ManagedRole,
        UpdateVersionRole,
        UpdateSizeRole,
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // `updates`: the newest catalog entry for each library that has a newer version, by library id
    void reload(const QString &folder, const QHash<qint64, CatalogEntry> &updates);
    const QList<LibraryRecord> &records() const
    {
        return m_records;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<LibraryRecord> m_records;
    QHash<qint64, QString> m_icons; // data: URLs of the 48x48 PNGs
    QHash<qint64, CatalogEntry> m_updates;
    QString m_folder;
};

// The Kiwix catalog, filtered and sorted for display.
class CatalogModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by LibraryManager")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filterChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY filterChanged)
    Q_PROPERTY(QString flavour READ flavour WRITE setFlavour NOTIFY filterChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY filterChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        SummaryRole,
        NameRole,
        CategoryRole,
        FlavourRole,
        LanguageRole,
        DateRole,
        SizeRole,
        ArticleCountRole,
        IllustrationRole,
        HasFulltextRole,
        FilenameRole,
        StateRole,
        ProgressRole,
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QList<CatalogEntry> &entries);
    // What is already here: installed file names, and the downloads in progress (file name -> 0..1)
    void setLocalState(const QSet<QString> &installed, const QHash<QString, double> &downloading);
    std::optional<CatalogEntry> entryAt(int row) const;
    const QList<CatalogEntry> &allEntries() const
    {
        return m_all;
    }

    int totalCount() const
    {
        return int(m_all.size());
    }
    QString query() const
    {
        return m_query;
    }
    void setQuery(const QString &q);
    QString category() const
    {
        return m_category;
    }
    void setCategory(const QString &c);
    QString flavour() const
    {
        return m_flavour;
    }
    void setFlavour(const QString &f);
    QString sortOrder() const
    {
        return m_sort;
    }
    void setSortOrder(const QString &s);
    QVariantList categories() const; // [{value, label, count}], most common first

Q_SIGNALS:
    void countChanged();
    void filterChanged();

private:
    void apply();
    QString stateOf(const CatalogEntry &e) const;

    QList<CatalogEntry> m_all;
    QList<int> m_rows;
    QSet<QString> m_installed;
    QHash<QString, QString> m_installedVersions; // base -> newest installed version
    QHash<QString, double> m_downloading;
    QString m_query;
    QString m_category = QStringLiteral("wikipedia"); // KAthenaeum answers from Wikipedia-style libraries
    QString m_flavour;
    QString m_sort = QStringLiteral("title");
};

// Recent downloads, newest first.
class DownloadsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by LibraryManager")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        FilenameRole,
        StatusRole,
        ErrorRole,
        ProgressRole,
        DoneRole,
        TotalRole,
        SpeedRole,
        EtaRole,
        ActiveRole,
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reload(const QList<DownloadRecord> &records);
    void update(const DownloadRecord &record); // one row changed: no reset, so the list does not jump
    const QList<DownloadRecord> &records() const
    {
        return m_records;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<DownloadRecord> m_records;
};
