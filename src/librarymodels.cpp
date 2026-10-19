// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarymodels.h"

#include "core/zimarchive.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>

// ---- installed ----

int InstalledModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_records.size());
}

QHash<int, QByteArray> InstalledModel::roleNames() const
{
    return {{IdRole, "libraryId"}, {TitleRole, "title"}, {DescriptionRole, "description"}, {PathRole, "path"},
            {FilenameRole, "filename"}, {FlavourRole, "flavour"}, {LanguageRole, "language"}, {DateRole, "date"},
            {SizeRole, "size"}, {ArticleCountRole, "articleCount"}, {IconRole, "icon"}, {HasFulltextRole, "hasFulltext"},
            {IsWikipediaRole, "isWikipedia"}, {MissingRole, "missing"}, {ManagedRole, "managed"},
            {UpdateVersionRole, "updateVersion"}, {UpdateSizeRole, "updateSize"}};
}

QVariant InstalledModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const LibraryRecord &r = m_records.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case DescriptionRole: return r.description;
    case PathRole: return r.path;
    case FilenameRole: return r.filename();
    case FlavourRole: return r.flavour;
    case LanguageRole: return r.language;
    case DateRole: return r.date;
    case SizeRole: return double(r.sizeBytes);  // QML numbers are doubles; qint64 would arrive as a string-ish variant
    case ArticleCountRole: return double(r.articleCount);
    case IconRole: return m_icons.value(r.id);
    case HasFulltextRole: return r.hasFulltextIndex;
    case IsWikipediaRole: return ZimArchive::isMwoffliner(r.scraper);
    case MissingRole: return !r.fileExists();
    case ManagedRole: return QFileInfo(r.path).absolutePath() == QDir(m_folder).absolutePath();
    case UpdateVersionRole: return m_updates.contains(r.id) ? m_updates.value(r.id).version : QString();
    case UpdateSizeRole: return m_updates.contains(r.id) ? double(m_updates.value(r.id).size) : 0.0;
    }
    return {};
}

void InstalledModel::reload(const QString &folder, const QHash<qint64, CatalogEntry> &updates)
{
    const int before = int(m_records.size());
    beginResetModel();
    m_folder = folder;
    m_records = Library::all();
    m_updates = updates;
    m_icons.clear();
    for (const LibraryRecord &r : std::as_const(m_records)) {
        const QByteArray png = Library::illustration(r.id);
        if (!png.isEmpty())
            m_icons.insert(r.id, QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
    }
    endResetModel();
    if (before != m_records.size())
        Q_EMIT countChanged();
}

// ---- catalog ----

int CatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> CatalogModel::roleNames() const
{
    return {{TitleRole, "title"}, {SummaryRole, "summary"}, {NameRole, "name"}, {CategoryRole, "category"},
            {FlavourRole, "flavour"}, {LanguageRole, "language"}, {DateRole, "date"}, {SizeRole, "size"},
            {ArticleCountRole, "articleCount"}, {IllustrationRole, "illustration"}, {HasFulltextRole, "hasFulltext"},
            {FilenameRole, "filename"}, {StateRole, "state"}, {ProgressRole, "progress"}};
}

QVariant CatalogModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const CatalogEntry &e = m_all.at(m_rows.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return e.title;
    case SummaryRole: return e.summary;
    case NameRole: return e.name;
    case CategoryRole: return e.category;
    case FlavourRole: return e.flavour;
    case LanguageRole: return e.language;
    case DateRole: return e.date.toString(Qt::ISODate);
    case SizeRole: return double(e.size);
    case ArticleCountRole: return double(e.articleCount);
    case IllustrationRole: return e.illustration;
    case HasFulltextRole: return e.hasFulltextIndex();
    case FilenameRole: return e.filename;
    case StateRole: return stateOf(e);
    case ProgressRole: return m_downloading.value(e.filename, 0.0);
    }
    return {};
}

QString CatalogModel::stateOf(const CatalogEntry &e) const
{
    if (m_installed.contains(e.filename))
        return QStringLiteral("installed");
    if (m_downloading.contains(e.filename))
        return QStringLiteral("downloading");
    if (const auto v = m_installedVersions.constFind(e.base); v != m_installedVersions.cend() && *v < e.version)
        return QStringLiteral("update");  // an older version of this very library is installed
    return QStringLiteral("available");
}

void CatalogModel::setEntries(const QList<CatalogEntry> &entries)
{
    m_all = entries;
    apply();
}

void CatalogModel::setLocalState(const QSet<QString> &installed, const QHash<QString, double> &downloading)
{
    m_installed = installed;
    m_downloading = downloading;
    m_installedVersions.clear();
    for (const QString &f : installed)
        if (const auto parts = Catalog::splitFilename(f)) {
            QString &v = m_installedVersions[parts->first];
            v = std::max(v, parts->second);
        }
    // Only two roles change, and views repaint just the rows on screen, so touching every row is cheap
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {StateRole, ProgressRole});
}

std::optional<CatalogEntry> CatalogModel::entryAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return std::nullopt;
    return m_all.at(m_rows.at(row));
}

void CatalogModel::setQuery(const QString &q)
{
    if (q == m_query)
        return;
    m_query = q;
    Q_EMIT filterChanged();
    apply();
}

void CatalogModel::setCategory(const QString &c)
{
    if (c == m_category)
        return;
    m_category = c;
    Q_EMIT filterChanged();
    apply();
}

void CatalogModel::setFlavour(const QString &f)
{
    if (f == m_flavour)
        return;
    m_flavour = f;
    Q_EMIT filterChanged();
    apply();
}

void CatalogModel::setSort(const QString &s)
{
    if (s == m_sort)
        return;
    m_sort = s;
    Q_EMIT filterChanged();
    apply();
}

QVariantList CatalogModel::categories() const
{
    QHash<QString, int> counts;
    for (const CatalogEntry &e : m_all)
        ++counts[e.category];
    QList<std::pair<QString, int>> sorted;
    for (auto [value, count] : counts.asKeyValueRange())
        sorted.append({value, count});
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    QVariantList out;
    for (const auto &[value, count] : sorted) {
        QString label = value.isEmpty() ? QStringLiteral("Uncategorised") : value;
        label[0] = label[0].toUpper();
        out.append(QVariantMap{{QStringLiteral("value"), value.isEmpty() ? QStringLiteral("-") : value},
                               {QStringLiteral("label"), label},
                               {QStringLiteral("count"), count}});
    }
    return out;
}

void CatalogModel::apply()
{
    const int before = int(m_rows.size());
    beginResetModel();
    m_rows.clear();
    const QStringList words = m_query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < m_all.size(); ++i) {
        const CatalogEntry &e = m_all.at(i);
        // "" = every kind, "-" = entries with no category
        if (!m_category.isEmpty() && e.category != (m_category == QLatin1String("-") ? QString() : m_category))
            continue;
        if (!m_flavour.isEmpty() && e.flavour != m_flavour)
            continue;
        if (!words.isEmpty()) {
            const QString hay = (e.title + QLatin1Char(' ') + e.summary + QLatin1Char(' ') + e.name + QLatin1Char(' ') + e.category).toLower();
            if (!std::all_of(words.cbegin(), words.cend(), [&](const QString &w) { return hay.contains(w); }))
                continue;
        }
        m_rows.append(i);
    }
    const auto &all = m_all;
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](int a, int b) {
        const CatalogEntry &x = all.at(a), &y = all.at(b);
        if (m_sort == QLatin1String("newest"))
            return x.date > y.date;
        if (m_sort == QLatin1String("largest"))
            return x.size > y.size;
        if (m_sort == QLatin1String("smallest"))
            return x.size < y.size;
        const int c = QString::localeAwareCompare(x.title, y.title);
        return c != 0 ? c < 0 : x.size < y.size;  // the same library in its flavours: smallest first
    });
    endResetModel();
    if (before != m_rows.size())
        Q_EMIT countChanged();
}

// ---- downloads ----

int DownloadsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_records.size());
}

QHash<int, QByteArray> DownloadsModel::roleNames() const
{
    return {{IdRole, "downloadId"}, {TitleRole, "title"}, {FilenameRole, "filename"}, {StatusRole, "status"},
            {ErrorRole, "error"}, {ProgressRole, "progress"}, {DoneRole, "done"}, {TotalRole, "total"},
            {SpeedRole, "speed"}, {EtaRole, "eta"}, {ActiveRole, "active"}};
}

QVariant DownloadsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const DownloadRecord &r = m_records.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case Qt::DisplayRole:
    case TitleRole: return r.title.isEmpty() ? r.filename : r.title;
    case FilenameRole: return r.filename;
    case StatusRole: return r.status;
    case ErrorRole: return r.error;
    case ProgressRole: return r.progress();
    case DoneRole: return double(r.doneBytes);
    case TotalRole: return double(r.totalBytes);
    case SpeedRole: return double(r.speed);
    case EtaRole: return r.etaSeconds;
    case ActiveRole: return r.isActive();
    }
    return {};
}

void DownloadsModel::reload(const QList<DownloadRecord> &records)
{
    const int before = int(m_records.size());
    beginResetModel();
    m_records = records;
    endResetModel();
    if (before != m_records.size())
        Q_EMIT countChanged();
}

void DownloadsModel::update(const DownloadRecord &record)
{
    for (int row = 0; row < m_records.size(); ++row) {
        if (m_records.at(row).id == record.id) {
            m_records[row] = record;
            Q_EMIT dataChanged(index(row), index(row));
            return;
        }
    }
}
