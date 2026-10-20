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

    // the sidebar section a top-level page belongs to (pages pushed on top keep their section)
    property string section: "libraries"
    function sectionOf(component) {
        return component === librariesPage ? "libraries" : component === searchPage ? "search" : component === mapPage ? "map"
             : component === askPage ? "ask" : component === chatsPage ? "chats" : component === projectsPage ? "projects"
             : component === libraryManagerPage ? "manager" : component === settingsPage ? "settings" : component === aboutPage ? "about" : section;
    }
    function componentOf(name) {
        return { libraries: librariesPage, search: searchPage, map: mapPage, ask: askPage, chats: chatsPage, projects: projectsPage,
                 manager: libraryManagerPage, settings: settingsPage, about: aboutPage }[name];
    }

    // One place that switches the main page, so the drawer and other pages share it
    function show(component, properties) {
        section = sectionOf(component);
        pageStack.clear();
        pageStack.push(component, properties ?? {});
    }
    function showLibraryManager() {
        show(libraryManagerPage);
    }
    function openProject(projectId) {
        section = "projects";
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

    // Each page's title in the header, with a back button when there is somewhere to go back to (a breadcrumb trail
    // of every page, with back and forward arrows, looked dated: the user, 2026-10-19)
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.ShowBackButton

    globalDrawer: Sidebar {
        id: sidebar
        current: root.section
        activeDownloads: LibraryManager.activeDownloads
        modal: !root.wideScreen
        drawerOpen: root.wideScreen
        onNavigate: name => root.show(root.componentOf(name))
        onSearch: text => text !== "" ? root.show(searchPage, { initialQuery: text }) : root.show(searchPage)
    }
    onWideScreenChanged: sidebar.drawerOpen = wideScreen

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: sidebar.focusSearch()
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
