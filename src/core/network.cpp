// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.h"

#include <QNetworkReply>
#include <QUrl>

QNetworkRequest Network::guarded(QNetworkRequest request)
{
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    return request;
}

void Network::allowRedirects(QNetworkReply *reply, const std::function<bool(const QUrl &)> &allowed)
{
    QObject::connect(reply, &QNetworkReply::redirected, reply, [reply, allowed](const QUrl &url) {
        if (url.scheme() == QLatin1String("https") && allowed(url))
            Q_EMIT reply->redirectAllowed();
        else
            reply->abort();
    });
}
