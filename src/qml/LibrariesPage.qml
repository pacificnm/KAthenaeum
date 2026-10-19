// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

Kirigami.ScrollablePage {
    id: page

    signal openLibraryManager()

    title: i18n("Libraries")

    ListView {
        id: list
        model: LibraryManager.installed

        delegate: QQC2.ItemDelegate {
            id: row
            required property var model
            width: ListView.view.width
            down: false
            highlighted: false
            // opening a library (home page, A to Z) is the next feature

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                LibraryIcon {
                    source: row.model.icon
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
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                        text: Util.metaLine(row.model.flavour, row.model.size, row.model.articleCount, row.model.date)
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
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
