// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWebEngineUrlSchemeHandler>

// Serves zim://lib<id>/<path> to the web view straight from the ZIM: articles go through the sanitiser, images are
// passed through, everything else (the ZIM's own scripts and stylesheets) is refused. No local server, nothing on
// the network.
class ZimSchemeHandler : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT

public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    void requestStarted(QWebEngineUrlRequestJob *job) override;

    // Before the application object exists
    static void registerScheme();
};
