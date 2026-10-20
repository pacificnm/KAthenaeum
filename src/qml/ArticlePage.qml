// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import KAthenaeum

// Shows an article from zim:// (sanitised by the app; JavaScript off). Links inside the library stay in this page,
// with Back and Forward; links to the web open in the web browser. "Ask" opens a chat about this article beside it
// (over it, in a narrow window).
Kirigami.Page {
    id: page

    required property url url
    // A section to show once the article has loaded and is in place. Loading "page#section" directly did not always
    // scroll there (a citation opened Ray (film) at the top instead of at Plot, seen 2026-10-19).
    property string fragment
    property var info: ({})
    property bool asking: false   // the chat panel is open
    property var openChatId: 0    // a saved chat about this article to show once it has loaded (from the Chats page)
    property alias conversation: conversation

    signal openArticle(var libraryId, string path, string fragment)
    signal openSettings()

    title: info.title !== undefined ? Util.pageTitle(info.found === false && page.conversation.articleTitle !== "" ? page.conversation.articleTitle : info.title) : i18n("Loading…")
    padding: 0

    function jumpTo(id) {
        view.url = LibraryBrowser.articleUrl(info.libraryId, info.path).toString() + "#" + encodeURIComponent(id);
    }

    // The chat follows the article shown: following a link to another article starts a chat about that one
    function syncChat() {
        if (!info.path || conversation.articlePath === info.path && conversation.articleLibraryId === info.libraryId)
            return;
        conversation.chatId = 0;
        conversation.articleLibraryId = info.libraryId;
        conversation.articlePath = info.path;
        conversation.articleTitle = info.title;
        if (page.openChatId) {
            conversation.chatId = page.openChatId;
            page.openChatId = 0;
            page.asking = true;
        }
    }
    onInfoChanged: syncChat()

    // a citation in the chat: a section of this article scrolls here; another article opens on its own page
    function showSource(libraryId, path, fragment) {
        if (libraryId !== info.libraryId || path !== info.path) {
            page.openArticle(libraryId, path, fragment);
        } else if (page.asking && page.narrow) {
            // the article is hidden under the panel: a jump made now lands anywhere (it scrolled to the end), so show the
            // article first and jump once it is laid out
            page.asking = false;
            page.fragment = fragment;
            fragmentTimer.restart();
        } else {
            jumpTo(fragment);
        }
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
            text: i18n("Ask")
            icon.name: "dialog-messages"
            checkable: true
            checked: page.asking
            enabled: page.info.found === true
            tooltip: i18n("Ask about this article")
            onTriggered: {
                page.asking = !page.asking;
                if (page.asking)
                    chat.focusInput();
            }
        },
        Kirigami.Action {
            text: i18n("Add to Project…")
            icon.name: "bookmark-new"
            enabled: page.info.found === true
            tooltip: i18n("Add this article to a research project")
            onTriggered: {
                articlePicker.already = projects.containing(page.info.libraryId, page.info.path);
                articlePicker.open();
            }
        },
        Kirigami.Action {
            text: i18n("Contents")
            icon.name: "view-list-text"
            enabled: (page.info.toc ?? []).length > 0
            onTriggered: tocMenu.popup()
        },
        Kirigami.Action {
            text: i18n("Use the Whole Window")
            icon.name: "zoom-fit-width"
            checkable: true
            checked: LibraryBrowser.fullWidth
            displayHint: Kirigami.DisplayHint.AlwaysHide
            tooltip: i18n("Articles across the whole window, or in a column of comfortable reading width")
            onTriggered: LibraryBrowser.fullWidth = checked
        }
    ]

    Connections {
        target: LibraryBrowser
        function onFullWidthChanged() {
            view.reload();
        }
    }

    readonly property bool narrow: width < Kirigami.Units.gridUnit * 40

    Conversation {
        id: conversation
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

    WebEngineView {
        id: view
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !(page.asking && page.narrow)
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
            // only a link the reader clicked opens in the browser, never a page navigating by itself
            if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation)
                openOutside(request.url);
        }
        onNewWindowRequested: function(request) {
            if (request.requestedUrl.toString().startsWith("zim:"))
                view.url = request.requestedUrl;
            else
                openOutside(request.requestedUrl);
        }

        // Selected text: Copy, or ask about it
        onContextMenuRequested: function(request) {
            if (request.selectedText === "")
                return;  // the web view's own menu
            request.accepted = true;
            selectionMenu.selection = request.selectedText.trim();
            selectionMenu.popup();
        }
    }

    Kirigami.Separator {
        Layout.fillHeight: true
        visible: page.asking && !page.narrow
    }

    ColumnLayout {
        Layout.fillHeight: true
        Layout.fillWidth: page.narrow
        Layout.preferredWidth: page.narrow ? -1 : Math.max(Kirigami.Units.gridUnit * 20, page.width * 0.38)
        visible: page.asking
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    level: 4
                    text: conversation.chatId > 0 ? conversation.title : i18n("Ask about this article")
                    elide: Text.ElideRight
                }
                QQC2.ToolButton {
                    icon.name: "view-history"
                    text: i18n("Earlier Chats")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: articleChats.count > 0
                    QQC2.ToolTip.text: articleChats.count > 0 ? i18np("%1 earlier chat about this article", "%1 earlier chats about this article", articleChats.count)
                                                                : i18n("No earlier chats about this article")
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        articleChats.refresh();
                        chatsMenu.popup();
                    }
                }
                QQC2.ToolButton {
                    icon.name: "list-add"
                    text: i18n("New Chat")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: conversation.chatId > 0 || conversation.busy
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        conversation.chatId = 0;
                        chat.focusInput();
                    }
                }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    text: i18n("Close")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: page.asking = false
                }
            }
        }

        ChatView {
            id: chat
            Layout.fillWidth: true
            Layout.fillHeight: true
            conversation: conversation
            onOpenArticle: (libraryId, path, fragment) => page.showSource(libraryId, path, fragment)
            onOpenSettings: page.openSettings()
        }
    }
    }

    ProjectList {
        id: projects
    }
    ProjectPicker {
        id: articlePicker
        title: i18n("Add Article to Project")
        onChosen: (projectId, projectTitle, note) => {
            if (projects.addArticle(projectId, page.info.libraryId, page.info.path, page.info.title, note))
                applicationWindow().showPassiveNotification(i18n("Added to “%1”", projectTitle));
        }
    }
    ProjectPicker {
        id: excerptPicker
        title: i18n("Save Passage to Project")
        onChosen: (projectId, projectTitle, note) => {
            if (projects.addExcerpt(projectId, excerptPicker.preview, page.info.libraryId, page.info.path, page.info.title, "", note)) {
                applicationWindow().showPassiveNotification(i18n("Saved to “%1”", projectTitle));
                view.reload();  // shows it marked (the picker has closed, so no selection is lost: Athenaeum's lesson)
            }
        }
    }

    ChatList {
        id: articleChats
        articleLibraryId: page.info.libraryId ?? 0
        articlePath: page.info.path ?? ""
    }
    Connections {
        target: conversation
        function onSaved() { articleChats.refresh(); }
    }

    QQC2.Menu {
        id: chatsMenu
        Instantiator {
            model: articleChats
            delegate: QQC2.MenuItem {
                required property var chatId
                required property string title
                text: Util.plain(title)
                checkable: true
                checked: chatId === conversation.chatId
                onTriggered: conversation.chatId = chatId
            }
            onObjectAdded: (index, object) => chatsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => chatsMenu.removeItem(object)
        }
    }

    QQC2.Menu {
        id: selectionMenu
        property string selection
        QQC2.MenuItem {
            text: i18n("Copy")
            icon.name: "edit-copy"
            onTriggered: view.triggerWebAction(WebEngineView.Copy)
        }
        QQC2.MenuItem {
            text: i18n("Save to Project…")
            icon.name: "bookmark-new"
            onTriggered: {
                excerptPicker.preview = selectionMenu.selection;
                excerptPicker.open();
            }
        }
        QQC2.MenuItem {
            text: i18n("Ask About This")
            icon.name: "dialog-messages"
            onTriggered: {
                page.asking = true;
                const quote = selectionMenu.selection.length > 300 ? selectionMenu.selection.slice(0, 300) + "…" : selectionMenu.selection;
                chat.setQuestion(i18n("What does the article mean by “%1”?", quote));
            }
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
                text: (modelData.level === 3 ? "    " : "") + Util.plain(modelData.text)
                onTriggered: page.jumpTo(modelData.id)
            }
            onObjectAdded: (index, object) => tocMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => tocMenu.removeItem(object)
        }
    }
}
