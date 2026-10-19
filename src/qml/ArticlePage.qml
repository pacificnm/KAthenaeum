// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtWebEngine
import org.kde.kirigami as Kirigami
import KAthenaeum

// Shows an article from zim:// (sanitised by the app; JavaScript off). Links inside the library stay in this page,
// with Back and Forward; links to the web open in the web browser.
Kirigami.Page {
    id: page

    required property url url
    // A section to show once the article has loaded and is in place. Loading "page#section" directly did not always
    // scroll there (a citation opened Ray (film) at the top instead of at Plot, seen 2026-10-19).
    property string fragment
    property var info: ({})

    title: info.title ?? i18n("Loading…")
    padding: 0

    function jumpTo(id) {
        view.url = LibraryBrowser.articleUrl(info.libraryId, info.path).toString() + "#" + encodeURIComponent(id);
    }

    actions: [
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            enabled: view.canGoBack
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: view.goBack()
        },
        Kirigami.Action {
            text: i18n("Forward")
            icon.name: "go-next"
            enabled: view.canGoForward
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: view.goForward()
        },
        Kirigami.Action {
            text: i18n("Contents")
            icon.name: "view-list-text"
            enabled: (page.info.toc ?? []).length > 0
            onTriggered: tocMenu.popup()
        }
    ]

    WebEngineView {
        id: view
        anchors.fill: parent
        url: page.url

        settings.javascriptEnabled: false
        settings.javascriptCanOpenWindows: false
        settings.pluginsEnabled: false
        settings.localStorageEnabled: false
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: false

        onLoadingChanged: loadingInfo => {
            if (loadingInfo.status === WebEngineView.LoadSucceededStatus && page.fragment !== "")
                fragmentTimer.start();
        }

        onUrlChanged: {
            if (LibraryBrowser.isArticleUrl(url))
                page.info = LibraryBrowser.articleInfo(url);
        }

        // Only zim:// is shown here; a link to the web goes to the web browser, anything else nowhere
        function openOutside(target) {
            const s = target.toString();
            if (/^(https?|mailto):/i.test(s)) {
                Qt.openUrlExternally(target);
                applicationWindow().showPassiveNotification(i18n("Opened in your web browser"));
            }
        }
        onNavigationRequested: function(request) {
            if (request.url.toString().startsWith("zim:"))
                return;
            request.reject();
            openOutside(request.url);
        }
        onNewWindowRequested: function(request) {
            if (request.requestedUrl.toString().startsWith("zim:"))
                view.url = request.requestedUrl;
            else
                openOutside(request.requestedUrl);
        }
    }

    // Jumping as soon as the page has loaded did not scroll either: the page is still sliding in and the view has not
    // been laid out. A moment later it works.
    Timer {
        id: fragmentTimer
        interval: Kirigami.Units.longDuration * 2
        onTriggered: {
            const id = page.fragment;
            page.fragment = "";
            page.jumpTo(id);
        }
    }

    // a different colour scheme: show the article in the new colours
    readonly property color themeBackground: Kirigami.Theme.backgroundColor
    onThemeBackgroundChanged: view.reload()

    QQC2.Menu {
        id: tocMenu
        Instantiator {
            model: page.info.toc ?? []
            delegate: QQC2.MenuItem {
                required property var modelData
                text: (modelData.level === 3 ? "    " : "") + modelData.text
                onTriggered: page.jumpTo(modelData.id)
            }
            onObjectAdded: (index, object) => tocMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => tocMenu.removeItem(object)
        }
    }
}
