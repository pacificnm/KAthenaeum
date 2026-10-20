// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "connectivity.h"

#include "core/settings.h"

#include <QNetworkInformation>
#include <QQmlEngine>

namespace
{
Connectivity *theInstance = nullptr; // a child of the application, made once in main()
}

Connectivity *Connectivity::createInstance(QObject *parent)
{
    if (!theInstance)
        theInstance = new Connectivity(parent);
    return theInstance;
}

Connectivity *Connectivity::instance()
{
    return theInstance;
}

Connectivity *Connectivity::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(theInstance, QQmlEngine::CppOwnership);
    return theInstance;
}

Connectivity::Connectivity(QObject *parent)
    : QObject(parent)
    , m_allowed(AppSettings::onlineAllowed())
{
    if (QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability))
        connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this, &Connectivity::changed);
}

bool Connectivity::allowed() const
{
    return m_allowed;
}

void Connectivity::setAllowed(bool on)
{
    if (on == m_allowed)
        return;
    m_allowed = on;
    AppSettings::setOnlineAllowed(on);
    Q_EMIT changed();
}

bool Connectivity::reachable() const
{
    // without a backend to ask (or before it knows), assume a connection: the requests themselves then say if not
    const QNetworkInformation *info = QNetworkInformation::instance();
    if (!info)
        return true;
    const auto r = info->reachability();
    return r == QNetworkInformation::Reachability::Online || r == QNetworkInformation::Reachability::Unknown;
}
