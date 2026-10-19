// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// Research projects: articles, saved passages, chats and documents gathered around one question
Kirigami.ScrollablePage {
    id: page

    signal openProject(var projectId)

    title: i18n("Projects")

    actions: [
        Kirigami.Action {
            text: i18n("New Project")
            icon.name: "list-add"
            onTriggered: newDialog.open()
        }
    ]

    ProjectList {
        id: projects
    }

    ListView {
        id: list
        model: projects

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "project-development"
            text: i18n("No projects yet")
            explanation: i18n("A project gathers articles, passages you select in them, and chats around one question. Ask questions about just that material, and write up what you find.")
            helpfulAction: Kirigami.Action {
                text: i18n("New Project")
                icon.name: "list-add"
                onTriggered: newDialog.open()
            }
        }

        delegate: QQC2.ItemDelegate {
            id: row
            required property var projectId
            required property string title
            required property string description
            required property var updated
            required property int articles
            required property int excerpts
            required property int chats
            required property int documents

            width: ListView.view.width
            onClicked: page.openProject(projectId)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: "project-development"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: row.title
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: row.description !== ""
                        text: row.description
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: [i18np("%1 article", "%1 articles", row.articles), i18np("%1 excerpt", "%1 excerpts", row.excerpts),
                               i18np("%1 chat", "%1 chats", row.chats), i18np("%1 document", "%1 documents", row.documents),
                               Qt.formatDateTime(row.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat))].join(" · ")
                        color: Kirigami.Theme.disabledTextColor
                        font: Kirigami.Theme.smallFont
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    ProjectEditDialog {
        id: newDialog
        title: i18n("New Project")
        onDone: (name, goal) => {
            const id = projects.create(name, goal);
            if (id > 0)
                page.openProject(id);
        }
    }
}
