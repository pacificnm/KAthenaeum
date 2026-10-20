// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/maptiles.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;
class QJSEngine;

// What the Map page needs besides Marble itself (which draws the map, caches what it shows and reports clicks): the
// article a clicked place links to in the installed libraries, where "Show on Map" opens, where the map was left, and
// keeping an area on this computer for use offline.
class MapManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(double cacheBytes READ cacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY saveChanged)
    Q_PROPERTY(int saveDone READ saveDone NOTIFY saveChanged)
    Q_PROPERTY(int saveTotal READ saveTotal NOTIFY saveChanged)
    Q_PROPERTY(int maxSaveTiles READ maxSaveTiles CONSTANT)

public:
    static MapManager *createInstance(QObject *parent);
    static MapManager *instance();
    static MapManager *create(QQmlEngine *, QJSEngine *);

    // Removes what the previous map engine left behind (its region files and OpenStreetMap extracts)
    void initialize();

    double cacheBytes() const
    {
        return m_cacheBytes;
    }
    bool saving() const
    {
        return !m_queue.isEmpty() || m_running > 0;
    }
    int saveDone() const
    {
        return m_saveDone;
    }
    int saveTotal() const
    {
        return m_saveTotal;
    }
    int maxSaveTiles() const
    {
        return int(MapTiles::MaxSaveTiles);
    }

    // The installed article a map feature links to (`wikipedia`: Marble's link or OpenStreetMap's tag), else one with the
    // feature's name: {libraryId, path, title, library, exact}, or empty
    Q_INVOKABLE QVariantMap article(const QString &wikipedia, const QString &name) const;
    // Where "Show on Map" opens for an article with coordinates: {lat, lon, zoom, name}
    Q_INVOKABLE QVariantMap locate(const QString &title, double lat, double lon) const;

    // Where the map was left ({lat, lon, zoom}, Marble's zoom), and keeping it
    Q_INVOKABLE QVariantMap startView() const;
    Q_INVOKABLE void saveView(double lat, double lon, int zoom);

    // Keeping the area shown for use offline, up to street level: how many tiles that is, and fetching those not kept yet
    Q_INVOKABLE int tileCount(double west, double south, double east, double north) const;
    Q_INVOKABLE void saveArea(double west, double south, double east, double north);
    Q_INVOKABLE void cancelSave();
    // Everything Marble has kept (shared with the Marble application), and forgetting it
    Q_INVOKABLE void measureCache();
    Q_INVOKABLE void clearCache();

Q_SIGNALS:
    void cacheChanged();
    void saveChanged();
    void message(const QString &text, bool error);

private:
    explicit MapManager(QObject *parent);
    void fetchNext();
    void finishSave(const QString &error);

    QNetworkAccessManager *m_network;
    QList<MapTiles::Tile> m_queue;
    QList<QPointer<QNetworkReply>> m_replies;
    int m_running = 0;
    int m_saveDone = 0;
    int m_saveTotal = 0;
    int m_failed = 0;
    double m_cacheBytes = 0;
    static inline MapManager *s_instance = nullptr;
};
