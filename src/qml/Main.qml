// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import KAthenaeum

Kirigami.ApplicationWindow {
    id: root

    title: i18n("KAthenaeum")
    minimumWidth: Kirigami.Units.gridUnit * 22
    minimumHeight: Kirigami.Units.gridUnit * 20
    width: Kirigami.Units.gridUnit * 60
    height: Kirigami.Units.gridUnit * 40

    // One place that switches the main page, so the drawer and other pages share it
    function show(component, properties) {
        pageStack.clear();
        pageStack.push(component, properties ?? {});
    }
    function showLibraryManager() {
        show(libraryManagerPage);
    }
    function openProject(projectId) {
        pageStack.push(projectPage, { projectId: projectId });
    }
    // a document opens in its own window: Kate's editor and a live preview
    function openDocument(documentId) {
        DocumentWindows.open(documentId);
    }
    // a chat about one article opens with its article, the panel beside it; a project's chat in its project
    // `onTop`: opened from inside a page (a project's sources), so Back returns there; from the Chats list it replaces
    // the stack. (Opening an attached chat from a project threw the project away; the review.)
    function openChat(chatId, articleLibraryId, articlePath, projectId, onTop) {
        if (projectId > 0) {
            show(projectsPage);
            pageStack.push(projectPage, { projectId: projectId });
            pageStack.currentItem.showChat(chatId);
        } else if (articleLibraryId > 0) {
            const props = { url: LibraryBrowser.articleUrl(articleLibraryId, articlePath), openChatId: chatId };
            onTop ? pageStack.push(articlePage, props) : show(articlePage, props);
        } else {
            onTop ? pageStack.push(askPage, { chatId: chatId }) : show(askPage, { chatId: chatId });
        }
    }
    // Reading goes deeper one page at a time: Libraries > Chemistry > A to Z > Water
    function openLibrary(libraryId) {
        pageStack.push(libraryPage, { libraryId: libraryId });
    }
    function openTitles(libraryId, label, key, isPrefix) {
        pageStack.push(titlesPage, { libraryId: libraryId, label: label, key: key, isPrefix: isPrefix });
    }
    function openSearch(query, libraryId) {
        pageStack.push(searchPage, { initialQuery: query, libraryId: libraryId });
    }
    function openArticle(libraryId, path, fragment) {
        pageStack.push(articlePage, { url: LibraryBrowser.articleUrl(libraryId, path), fragment: fragment ?? "" });
    }

    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.Breadcrumb

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: false
        modal: !root.wideScreen
        collapsible: true
        collapsed: !root.wideScreen
        showHeaderWhenCollapsed: true
        actions: [
            Kirigami.Action { text: i18n("Search"); icon.name: "search"; shortcut: StandardKey.Find; onTriggered: root.show(searchPage) },
            Kirigami.Action { text: i18n("Libraries"); icon.name: "bookmarks"; onTriggered: root.show(librariesPage) },
            Kirigami.Action { text: i18n("Ask"); icon.name: "dialog-messages"; onTriggered: root.show(askPage) },
            Kirigami.Action { text: i18n("Chats"); icon.name: "view-history"; onTriggered: root.show(chatsPage) },
            Kirigami.Action { text: i18n("Projects"); icon.name: "folder-bookmark"; onTriggered: root.show(projectsPage) },
            Kirigami.Action { text: i18n("Map"); icon.name: "map-flat"; onTriggered: root.show(mapPage) },
            Kirigami.Action { separator: true },
            Kirigami.Action { text: i18n("Library Manager"); icon.name: "folder-download"; onTriggered: root.show(libraryManagerPage) },
            Kirigami.Action { text: i18n("Settings"); icon.name: "settings-configure"; onTriggered: root.show(settingsPage) },
            Kirigami.Action { text: i18n("About KAthenaeum"); icon.name: "help-about"; onTriggered: root.show(aboutPage) }
        ]
    }

    pageStack.initialPage: librariesPage

    // an AI draft is written as a project document, wherever the reader has gone meanwhile
    // a link in a document's preview: show the article here, and bring this window forward
    Connections {
        target: DocumentWindows
        function onOpenArticle(url) {
            root.pageStack.push(articlePage, { url: url });
            root.raise();
            root.requestActivate();
        }
    }

    Connections {
        target: Drafting
        function onDocumentReady(documentId, projectId, title) {
            const page = root.pageStack.currentItem;
            if (page && page.project !== undefined && page.project.projectId === projectId)
                root.openDocument(documentId);
            else
                root.showPassiveNotification(i18n("“%1” is ready", Util.plain(title)), "long", i18n("Open"), () => root.openDocument(documentId));
        }
        function onFailed(message) {
            root.showPassiveNotification(message, "long");
        }
    }

    Connections {
        target: MapManager
        function onMessage(text, isError) {
            root.showPassiveNotification(text, isError ? "long" : "short");
        }
    }

    Connections {
        target: LibraryManager
        function onMessage(text, isError) {
            root.showPassiveNotification(text, isError ? "long" : "short");
        }
    }

    Component {
        id: librariesPage
        LibrariesPage {
            onOpenLibraryManager: root.show(libraryManagerPage)
            onOpenLibrary: libraryId => root.openLibrary(libraryId)
        }
    }
    Component {
        id: libraryPage
        LibraryPage {
            onOpenArticle: (libraryId, path) => root.openArticle(libraryId, path)
            onOpenTitles: (libraryId, label, key, isPrefix) => root.openTitles(libraryId, label, key, isPrefix)
            onOpenSearch: (query, libraryId) => root.openSearch(query, libraryId)
        }
    }
    Component {
        id: searchPage
        SearchPage {
            onOpenArticle: (libraryId, path) => root.openArticle(libraryId, path)
        }
    }
    Component {
        id: titlesPage
        TitlesPage {
            onOpenArticle: (libraryId, path) => root.openArticle(libraryId, path)
        }
    }
    Component {
        id: articlePage
        ArticlePage {
            onOpenArticle: (libraryId, path, fragment) => root.openArticle(libraryId, path, fragment)
            onOpenSettings: root.show(settingsPage)
        }
    }
    Component {
        id: askPage
        AskPage {
            onOpenArticle: (libraryId, path, fragment) => root.openArticle(libraryId, path, fragment)
            onOpenSettings: root.show(settingsPage)
        }
    }
    Component {
        id: chatsPage
        ChatsPage {
            onOpenChat: (chatId, articleLibraryId, articlePath, projectId) => root.openChat(chatId, articleLibraryId, articlePath, projectId)
        }
    }
    Component {
        id: projectsPage
        ProjectsPage {
            onOpenProject: projectId => root.openProject(projectId)
        }
    }
    Component {
        id: projectPage
        ProjectPage {
            onOpenArticle: (libraryId, path, fragment) => root.openArticle(libraryId, path, fragment)
            onOpenChat: (chatId, articleLibraryId, articlePath) => root.openChat(chatId, articleLibraryId, articlePath, 0, true)
            onOpenDocument: documentId => root.openDocument(documentId)
            onClosed: root.show(projectsPage)
        }
    }
    Component {
        id: mapPage
        MapPage {
            onOpenArticle: (libraryId, path) => root.openArticle(libraryId, path)
            onOpenSearch: query => root.openSearch(query, -1)
            onOpenMapSettings: root.show(settingsPage)
        }
    }
    Component { id: libraryManagerPage; LibraryManagerPage {} }
    Component { id: settingsPage; SettingsPage {} }
    Component { id: aboutPage; FormCard.AboutPage {} }
}
