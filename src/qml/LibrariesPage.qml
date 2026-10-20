// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

Kirigami.ScrollablePage {
    id: page

    signal openLibraryManager()
    signal openLibrary(var libraryId)

    title: i18n("Libraries")

    // the libraries as cards, and one more to get others
    GridView {
        id: grid

        readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 16)))
        cellWidth: Math.floor(width / columns)
        cellHeight: Kirigami.Units.gridUnit * 7
        topMargin: Kirigami.Units.largeSpacing
        model: LibraryManager.installed

        delegate: Item {
            id: cell
            required property var model
            width: grid.cellWidth
            height: grid.cellHeight

            CardButton {
                anchors { fill: parent; margins: Kirigami.Units.largeSpacing / 2 + Kirigami.Units.smallSpacing }
                onClicked: page.openLibrary(cell.model.libraryId)
                Accessible.name: cell.model.title

                RowLayout {
                    anchors { fill: parent; margins: Kirigami.Units.largeSpacing * 1.5 }
                    spacing: Kirigami.Units.largeSpacing * 1.5
                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.iconSizes.huge + Kirigami.Units.largeSpacing * 2
                        Layout.preferredHeight: Layout.preferredWidth
                        Layout.alignment: Qt.AlignTop
                        radius: Kirigami.Units.cornerRadius * 2
                        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.06)
                        LibraryIcon {
                            anchors.centerIn: parent
                            width: Kirigami.Units.iconSizes.large
                            height: Kirigami.Units.iconSizes.large
                            source: cell.model.icon
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: Kirigami.Units.smallSpacing
                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading {
                                Layout.fillWidth: true
                                level: 3
                                textFormat: Text.PlainText
                                text: cell.model.title
                                elide: Text.ElideRight
                            }
                            // a newer version in the catalog: to the Library Manager, where it can be downloaded
                            AppButton {
                                visible: cell.model.updateVersion !== ""
                                kind: "secondary"
                                icon.name: "update-medium"
                                text: i18n("Update to %1", cell.model.updateVersion)
                                onClicked: page.openLibraryManager()
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: cell.model.description
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            visible: text !== ""
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            opacity: 0.7
                            font: Kirigami.Theme.smallFont
                            wrapMode: Text.Wrap
                            text: Util.metaLine(cell.model.flavour, cell.model.size, cell.model.articleCount, cell.model.date)
                        }
                    }
                }
            }
        }

        footer: Item {
            width: grid.width
            height: grid.count > 0 ? Kirigami.Units.gridUnit * 4 : 0
            visible: grid.count > 0
            AppButton {
                kind: "flat"
                anchors.centerIn: parent
                text: i18n("Get More Libraries…")
                icon.name: "folder-download"
                onClicked: page.openLibraryManager()
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: grid.count === 0
            icon.name: "bookmarks"
            text: i18n("No libraries yet")
            explanation: i18n("Download a Wikipedia library from the Library Manager to start reading and asking questions.")
            helpfulAction: Kirigami.Action {
                text: i18n("Open the Library Manager")
                icon.name: "folder-download"
                onTriggered: page.openLibraryManager()
            }
        }
    }
}
