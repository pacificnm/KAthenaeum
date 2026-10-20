// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

// One downloadable library in the Kiwix OPDS v2 catalog.
struct CatalogEntry {
    QString id;          // urn:uuid:...
    QString name;        // "wikipedia_en_chemistry" (repeats across flavours)
    QString title;
    QString summary;
    QString language;    // "eng", sometimes a list: "eng,jpn"
    QString category;    // "wikipedia", "ted", ... often empty
    QString flavour;     // "mini", "nopic", "maxi", often empty
    QString author;
    QString publisher;
    QStringList tags;    // "_ftindex:yes", "_pictures:no", ...
    QDate date;
    qint64 articleCount = 0;
    qint64 mediaCount = 0;
    qint64 size = 0;     // bytes, rounded UP to the next KiB by the catalog
    QUrl url;            // the .meta4 metalink aria2c downloads
    QUrl illustration;   // 48x48 PNG on the catalog server
    QString filename;    // "wikipedia_en_chemistry_mini_2026-10.zim"
    QString base;        // "wikipedia_en_chemistry_mini"
    QString version;     // "2026-10"

    // Without the ZIM's own full-text index KAthenaeum can only find articles by title
    bool hasFulltextIndex() const { return !tags.contains(QLatin1String("_ftindex:no")); }
};

namespace Catalog
{
// Parses an OPDS v2 acquisition feed. Relative links are resolved against `feedUrl`.
QList<CatalogEntry> parse(const QByteArray &xml, const QUrl &feedUrl, QString *error = nullptr);

// "wikipedia_en_all_nopic_2026-06.zim" -> {"wikipedia_en_all_nopic", "2026-06"}
std::optional<std::pair<QString, QString>> splitFilename(const QString &filename);

// https on kiwix.org or one of its subdomains, with no user info: the only place the app fetches anything from
bool isKiwixUrl(const QUrl &url);

// A plain ZIM file name, nothing that could escape the library folder.
bool isValidFilename(const QString &filename);

// May KAthenaeum download `url` as `filename`? Empty if so, otherwise why not. Only https from kiwix.org (parsed, not a
// substring test), and the URL must be for that very file, so the record and the file on disk cannot disagree.
QString checkDownloadUrl(const QUrl &url, const QString &filename);

QUrl defaultFeedUrl();
}

// The catalog, fetched from Kiwix and kept in a cache file so it also works offline.
class CatalogFeed : public QObject
{
    Q_OBJECT

public:
    CatalogFeed(const QString &cacheFile, const QUrl &feedUrl, QObject *parent = nullptr);

    const QList<CatalogEntry> &entries() const { return m_entries; }
    QDateTime fetchedAt() const { return m_fetchedAt; }
    bool isLoading() const { return m_reply != nullptr; }
    QString error() const { return m_error; }  // last fetch problem; entries may still come from the cache

    // Uses the cache when it is younger than an hour, otherwise fetches. A failed fetch keeps the cached copy.
    void load();
    // Always fetches.
    void refresh();
    // The cached copy, if there is one, and no fetch (at startup: update badges without touching the network)
    void useCache();

    static constexpr int MaxAgeSeconds = 3600;

Q_SIGNALS:
    void changed();

private:
    bool loadCache();
    void fetch();
    void finished();

    QString m_cacheFile;
    QUrl m_feedUrl;
    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QList<CatalogEntry> m_entries;
    QDateTime m_fetchedAt;
    QString m_error;
};
