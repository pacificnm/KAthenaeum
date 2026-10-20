// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QNetworkRequest>

#include <functional>

class QNetworkReply;
class QUrl;

// Requests to the few hosts the app fetches from (the Kiwix catalog, Protomaps, Geofabrik). Qt follows redirects by
// itself; these follow one only to a host the caller allows, over https, and abort the request otherwise, before
// anything is read from the other host.
namespace Network
{
// The request, set to ask before following a redirect
QNetworkRequest guarded(QNetworkRequest request);
// Answers the reply's redirects: followed when `allowed` accepts the new address, aborted when not
void allowRedirects(QNetworkReply *reply, const std::function<bool(const QUrl &)> &allowed);
}
