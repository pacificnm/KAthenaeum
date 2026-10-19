// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

ColumnLayout {
    id: view

    signal browseCatalog()

    spacing: 0

    // Where libraries go, and how much room is left: the thing that runs out first on a laptop
    QQC2.ToolBar {
        Layout.fillWidth: true
        position: QQC2.ToolBar.Header
        contentItem: RowLayout {
            Kirigami.Icon {
                source: "folder"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: i18n("Downloads go to %1 · %2 free", LibraryManager.folder, KCoreAddons.Format.formatByteSize(LibraryManager.freeSpace, 1))
            }
            QQC2.Button {
                text: i18n("Change…")
                icon.name: "folder-open"
                onClicked: folderDialog.open()
            }
        }
    }

    QQC2.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: list
            model: LibraryManager.installed
            clip: true

            delegate: QQC2.ItemDelegate {
                id: row
                required property var model
                width: ListView.view.width
                hoverEnabled: true
                down: false
                onClicked: detailsDialog.show(model.libraryId)

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    LibraryIcon {
                        source: row.model.icon
                        Layout.alignment: Qt.AlignTop
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 4
                            text: row.model.title
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.model.description
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            visible: text !== ""
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            opacity: 0.7
                            font: Kirigami.Theme.smallFont
                            wrapMode: Text.Wrap
                            text: Util.metaLine(row.model.flavour, row.model.size, row.model.articleCount, row.model.date)
                        }
                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            type: Kirigami.MessageType.Error
                            visible: row.model.missing
                            text: i18n("The file is missing: %1", row.model.path)
                        }
                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            type: Kirigami.MessageType.Warning
                            visible: !row.model.missing && !row.model.isWikipedia
                            text: i18n("Not a Wikipedia-style library: you may be able to read it, but KAthenaeum cannot search it or answer questions from it.")
                        }
                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            type: Kirigami.MessageType.Warning
                            visible: !row.model.missing && row.model.isWikipedia && !row.model.hasFulltext
                            text: i18n("This library has no full-text index, so articles can only be found by title.")
                        }
                    }

                    QQC2.Button {
                        Layout.alignment: Qt.AlignTop
                        visible: row.model.updateVersion !== ""
                        text: i18n("Update to %1", row.model.updateVersion)
                        icon.name: "update-none"
                        QQC2.ToolTip.text: i18n("Download the newer version (%1)", KCoreAddons.Format.formatByteSize(row.model.updateSize, 1))
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: LibraryManager.updateLibrary(row.model.libraryId)
                    }
                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignTop
                        icon.name: "delete"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Remove…")
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: removeDialog.ask(row.model.libraryId, row.model.title, row.model.size, row.model.managed, row.model.missing)
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: list.count === 0
                icon.name: "bookmarks"
                text: i18n("No libraries installed")
                explanation: i18n("Download one from the catalog, or add a ZIM file you already have.")
                helpfulAction: Kirigami.Action {
                    text: i18n("Browse the Catalog")
                    icon.name: "globe"
                    onTriggered: view.browseCatalog()
                }
            }
        }
    }

    LibraryDetailsDialog {
        id: detailsDialog
    }

    Kirigami.PromptDialog {
        id: removeDialog

        property var libraryId
        property string libraryTitle
        property bool canDelete

        function ask(id, title, size, managed, missing) {
            libraryId = id;
            libraryTitle = title;
            canDelete = !missing;
            deleteFile.checked = managed && !missing;  // files the user added from elsewhere are kept by default
            deleteFile.text = i18n("Also delete the file from disk (%1)", KCoreAddons.Format.formatByteSize(size, 1));
            open();
        }

        title: i18n("Remove %1?", libraryTitle)
        subtitle: i18n("Chats and projects that used it keep their text, but its articles will no longer open.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: LibraryManager.removeLibrary(libraryId, deleteFile.checked)

        QQC2.CheckBox {
            id: deleteFile
            visible: removeDialog.canDelete
        }
    }

    FolderDialog {
        id: folderDialog
        title: i18n("Folder for Downloaded Libraries")
        currentFolder: "file://" + LibraryManager.folder
        onAccepted: LibraryManager.setFolder(selectedFolder)
    }
}
