// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import KAthenaeum

Kirigami.Page {
    id: page

    title: i18n("Library Manager")
    padding: 0

    actions: [
        Kirigami.Action {
            visible: tabs.currentIndex === 0
            text: LibraryManager.checkingUpdates ? i18n("Checking…") : i18n("Check for Updates")
            icon.name: "update-none"
            enabled: !LibraryManager.checkingUpdates && LibraryManager.installed.count > 0
            tooltip: i18n("Look in the Kiwix catalog for newer versions of the installed libraries")
            onTriggered: LibraryManager.checkForUpdates()
        },
        Kirigami.Action {
            visible: tabs.currentIndex === 0
            text: i18n("Add File…")
            icon.name: "document-open"
            onTriggered: addFileDialog.open()
        },
        Kirigami.Action {
            visible: tabs.currentIndex === 0
            displayHint: Kirigami.DisplayHint.AlwaysHide
            text: i18n("Rescan Folder")
            icon.name: "view-refresh"
            onTriggered: LibraryManager.rescan()
        },
        Kirigami.Action {
            visible: tabs.currentIndex === 1
            text: i18n("Refresh Catalog")
            icon.name: "view-refresh"
            enabled: !LibraryManager.catalogLoading
            onTriggered: LibraryManager.refreshCatalog()
        }
    ]

    // The catalog comes from kiwix.org: fetch it only once someone opens the manager, not at every start
    Component.onCompleted: LibraryManager.loadCatalog()

    header: QQC2.ToolBar {
        padding: 0
        contentItem: SegmentedTabs {
            id: tabs
            tabs: [
                { text: LibraryManager.updateCount > 0 ? i18np("Installed · %1 update", "Installed · %1 updates", LibraryManager.updateCount)
                      : LibraryManager.installed.count > 0 ? i18n("Installed (%1)", LibraryManager.installed.count) : i18n("Installed"), icon: "bookmarks" },
                { text: i18n("Catalog"), icon: "globe" },
                { text: LibraryManager.activeDownloads > 0 ? i18n("Downloads (%1)", LibraryManager.activeDownloads) : i18n("Downloads"), icon: "folder-download" }
            ]
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex

        InstalledView {
            onBrowseCatalog: tabs.currentIndex = 1
        }
        CatalogView {
            onShowDownloads: tabs.currentIndex = 2
        }
        DownloadsView {}
    }

    FileDialog {
        id: addFileDialog
        title: i18n("Add a ZIM File")
        nameFilters: [i18n("ZIM files (*.zim)")]
        onAccepted: LibraryManager.addFile(selectedFile)
    }
}
