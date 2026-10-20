// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.h"

#include "network.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>

namespace
{
constexpr qint64 MaxFeedBytes = 32 << 20;  // the English catalog is about 1.5 MB
constexpr QLatin1StringView Acquisition("http://opds-spec.org/acquisition/open-access");
constexpr QLatin1StringView Thumbnail("http://opds-spec.org/image/thumbnail");

// QXmlStreamReader never fetches external entities and limits entity expansion, so a hostile feed cannot blow up memory.
CatalogEntry readEntry(QXmlStreamReader &xml, const QUrl &feedUrl)
{
    CatalogEntry e;
    while (xml.readNextStartElement()) {
        const auto tag = xml.name();
        if (tag == QLatin1String("link")) {
            const auto rel = xml.attributes().value(QLatin1String("rel"));
            const QUrl href = feedUrl.resolved(QUrl(xml.attributes().value(QLatin1String("href")).toString()));
            if (rel == Acquisition) {
                e.url = href;
                e.size = xml.attributes().value(QLatin1String("length")).toLongLong();
            } else if (rel == Thumbnail && Catalog::isIllustrationUrl(href)) {
                e.illustration = href;  // shown by QML, which may fetch from kiwix.org only (see main.cpp)
            }
            xml.skipCurrentElement();
        } else if (tag == QLatin1String("author") || tag == QLatin1String("publisher")) {
            QString name;
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("name"))
                    name = xml.readElementText().trimmed();
                else
                    xml.skipCurrentElement();
            }
            (tag == QLatin1String("author") ? e.author : e.publisher) = name;
        } else {
            const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if (tag == QLatin1String("id"))
                e.id = text;
            else if (tag == QLatin1String("title"))
                e.title = text;
            else if (tag == QLatin1String("summary"))
                e.summary = text;
            else if (tag == QLatin1String("language"))
                e.language = text;
            else if (tag == QLatin1String("name"))
                e.name = text;
            else if (tag == QLatin1String("flavour"))
                e.flavour = text;
            else if (tag == QLatin1String("category"))
                e.category = text;
            else if (tag == QLatin1String("tags"))
                e.tags = text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            else if (tag == QLatin1String("updated"))
                e.date = QDate::fromString(text.left(10), Qt::ISODate);
            else if (tag == QLatin1String("articleCount"))
                e.articleCount = text.toLongLong();
            else if (tag == QLatin1String("mediaCount"))
                e.mediaCount = text.toLongLong();
        }
    }
    e.filename = e.url.fileName();
    if (e.filename.endsWith(QLatin1String(".meta4")))
        e.filename.chop(6);
    if (const auto parts = Catalog::splitFilename(e.filename)) {
        e.base = parts->first;
        e.version = parts->second;
    }
    return e;
}
}

QUrl Catalog::defaultFeedUrl()
{
    // The whole English catalog is one ~1.5 MB response, so it is fetched once and filtered locally
    return QUrl(QStringLiteral("https://opds.library.kiwix.org/catalog/v2/entries?lang=eng&count=5000"));
}

QList<CatalogEntry> Catalog::parse(const QByteArray &data, const QUrl &feedUrl, QString *error)
{
    QList<CatalogEntry> entries;
    QXmlStreamReader xml(data);
    if (xml.readNextStartElement() && xml.name() == QLatin1String("feed")) {
        while (xml.readNextStartElement()) {
            if (xml.name() != QLatin1String("entry")) {
                xml.skipCurrentElement();
                continue;
            }
            CatalogEntry e = readEntry(xml, feedUrl);
            if (e.url.isValid() && isValidFilename(e.filename))  // an entry we could not download is no use
                entries.append(std::move(e));
        }
    } else if (!xml.hasError()) {
        xml.raiseError(QStringLiteral("not an OPDS feed"));
    }
    if (xml.hasError()) {
        if (error)
            *error = xml.errorString();
        return {};
    }
    return entries;
}

std::optional<std::pair<QString, QString>> Catalog::splitFilename(const QString &filename)
{
    static const QRegularExpression re(QStringLiteral("^(.+)_(\\d{4}-\\d{2})\\.zim$"));
    const auto m = re.match(filename);
    if (!m.hasMatch())
        return std::nullopt;
    return std::pair{m.captured(1), m.captured(2)};
}

bool Catalog::isValidFilename(const QString &filename)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,200}\\.zim$"));
    return re.match(filename).hasMatch();
}

bool Catalog::isIllustrationUrl(const QUrl &url)
{
    // "https://opds.library.kiwix.org/catalog/v2/illustration/<uuid>/?size=48", and nothing more: any other address or
    // query on kiwix.org could carry text out of the computer (a question put in it by a model told to by an article)
    static const QRegularExpression path(QStringLiteral("^/catalog/v2/illustration/[0-9a-fA-F-]{36}/?$"));
    static const QRegularExpression query(QStringLiteral("^(size=\\d{1,4})?$"));
    return isKiwixUrl(url) && path.match(url.path()).hasMatch() && query.match(url.query()).hasMatch() && url.fragment().isEmpty();
}

bool Catalog::isKiwixUrl(const QUrl &url)
{
    const QString host = url.host().toLower();
    return url.isValid() && url.scheme() == QLatin1String("https") && url.userInfo().isEmpty()
        && (host == QLatin1String("kiwix.org") || host.endsWith(QLatin1String(".kiwix.org")));
}

QString Catalog::checkDownloadUrl(const QUrl &url, const QString &filename)
{
    if (!isKiwixUrl(url))
        return QStringLiteral("only https downloads from kiwix.org are allowed");
    if (!isValidFilename(filename))
        return QStringLiteral("not a valid ZIM file name: %1").arg(filename);
    QString last = url.fileName();
    if (last.endsWith(QLatin1String(".meta4")))
        last.chop(6);
    if (last != filename)
        return QStringLiteral("the link does not point at %1").arg(filename);
    return {};
}

CatalogFeed::CatalogFeed(const QString &cacheFile, const QUrl &feedUrl, QObject *parent)
    : QObject(parent)
    , m_cacheFile(cacheFile)
    , m_feedUrl(feedUrl)
    , m_network(new QNetworkAccessManager(this))
{
}

void CatalogFeed::load()
{
    const bool cached = m_entries.isEmpty() ? loadCache() : true;
    if (cached && m_fetchedAt.secsTo(QDateTime::currentDateTime()) < MaxAgeSeconds)
        return;
    fetch();
}

void CatalogFeed::refresh()
{
    fetch();
}

void CatalogFeed::useCache()
{
    if (m_entries.isEmpty() && loadCache())
        Q_EMIT changed();
}

bool CatalogFeed::loadCache()
{
    QFile file(m_cacheFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    auto entries = Catalog::parse(file.readAll(), m_feedUrl);
    if (entries.isEmpty())
        return false;
    m_entries = std::move(entries);
    m_fetchedAt = QFileInfo(file).lastModified();
    Q_EMIT changed();
    return true;
}

void CatalogFeed::fetch()
{
    if (m_reply)
        return;
    QNetworkRequest request = Network::guarded(QNetworkRequest(m_feedUrl));
    request.setTransferTimeout(60000);
    m_reply = m_network->get(request);
    Network::allowRedirects(m_reply, &Catalog::isKiwixUrl);
    // the English catalog is about 1.5 MB: a reply far bigger than that is not the catalog, and is not kept in memory
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received) {
        if (received > MaxFeedBytes)
            reply->abort();
    });
    connect(m_reply, &QNetworkReply::finished, this, &CatalogFeed::finished);
    Q_EMIT changed();
}

void CatalogFeed::finished()
{
    QNetworkReply *reply = std::exchange(m_reply, nullptr);
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_error = reply->errorString();
        Q_EMIT changed();
        return;
    }
    const QByteArray data = reply->readAll();
    QString parseError;
    auto entries = Catalog::parse(data, m_feedUrl, &parseError);
    if (entries.isEmpty()) {
        m_error = parseError.isEmpty() ? QStringLiteral("the catalog was empty") : parseError;
        Q_EMIT changed();
        return;
    }
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile cache(m_cacheFile);  // written whole or not at all, so a crash never leaves half a catalog
    if (cache.open(QIODevice::WriteOnly)) {
        cache.write(data);
        cache.commit();
    }
    m_entries = std::move(entries);
    m_fetchedAt = QDateTime::currentDateTime();
    m_error.clear();
    Q_EMIT changed();
}
