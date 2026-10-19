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
    function openChat(chatId) {
        show(askPage, { chatId: chatId });
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
            Kirigami.Action { text: i18n("Projects"); icon.name: "project-development"; onTriggered: root.show(projectsPage) },
            Kirigami.Action { separator: true },
            Kirigami.Action { text: i18n("Library Manager"); icon.name: "folder-download"; onTriggered: root.show(libraryManagerPage) },
            Kirigami.Action { text: i18n("Settings"); icon.name: "settings-configure"; onTriggered: root.show(settingsPage) },
            Kirigami.Action { text: i18n("About KAthenaeum"); icon.name: "help-about"; onTriggered: root.show(aboutPage) }
        ]
    }

    pageStack.initialPage: librariesPage

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
    Component { id: articlePage; ArticlePage {} }
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
            onOpenChat: chatId => root.openChat(chatId)
        }
    }
    Component { id: projectsPage; ProjectsPage {} }
    Component { id: libraryManagerPage; LibraryManagerPage {} }
    Component { id: settingsPage; SettingsPage {} }
    Component { id: aboutPage; FormCard.AboutPage {} }
}
