// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// Saved conversations, most recently used first
Kirigami.ScrollablePage {
    id: page

    signal openChat(var chatId, var articleLibraryId, string articlePath, var projectId)

    title: i18n("Chats")

    onVisibleChanged: if (visible) chats.refresh()

    ChatList {
        id: chats
    }

    ListView {
        id: list
        model: chats

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "view-history"
            text: i18n("No chats yet")
            explanation: i18n("Questions you ask are saved here with their answers and sources.")
        }

        delegate: QQC2.ItemDelegate {
            id: row
            required property var chatId
            required property string title
            required property var updated
            required property int questions
            required property var articleLibraryId
            required property string articlePath
            required property string articleTitle
            required property var projectId
            required property string projectTitle

            width: ListView.view.width
            highlighted: false
            onClicked: page.openChat(chatId, articleLibraryId, articlePath, projectId)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: row.projectId > 0 ? "folder-bookmark" : row.articleLibraryId > 0 ? "document-preview" : "dialog-messages"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: row.title
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        // independent facts, joined with a separator translators can change
                        text: [row.projectId > 0 ? i18n("In “%1”", row.projectTitle) : "",
                               row.articleLibraryId > 0 ? i18n("About “%1”", row.articleTitle) : "",
                               i18np("%1 question", "%1 questions", row.questions),
                               Qt.formatDateTime(row.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat))].filter(s => s !== "").join(i18nc("between facts in a list row", " · "))
                        color: Kirigami.Theme.disabledTextColor
                        font: Kirigami.Theme.smallFont
                        elide: Text.ElideRight
                    }
                }
                QQC2.ToolButton {
                    visible: row.projectId <= 0
                    icon.name: "bookmark-new"
                    text: i18n("Add to Project…")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        picker.chatId = row.chatId;
                        picker.open();
                    }
                }
                QQC2.ToolButton {
                    icon.name: "edit-rename"
                    text: i18n("Rename")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        renameDialog.chatId = row.chatId;
                        renameField.text = row.title;
                        renameDialog.open();
                    }
                }
                QQC2.ToolButton {
                    icon.name: "edit-delete"
                    text: i18n("Delete")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        deleteDialog.chatId = row.chatId;
                        deleteDialog.chatTitle = row.title;
                        deleteDialog.open();
                    }
                }
            }
        }
    }

    ProjectList {
        id: projects
    }
    ProjectPicker {
        id: picker
        property var chatId
        withNote: false
        onChosen: (projectId, projectTitle) => {
            if (projects.attachChat(projectId, picker.chatId))
                applicationWindow().showPassiveNotification(i18n("Added to “%1”", Util.plain(projectTitle)));
        }
    }

    Kirigami.PromptDialog {
        id: deleteDialog
        property var chatId
        property string chatTitle
        title: i18n("Delete Chat")
        subtitle: i18n("Delete “%1” and its answers? This cannot be undone.", Util.plain(chatTitle))
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    chats.remove(deleteDialog.chatId);
                    deleteDialog.close();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: deleteDialog.close()
            }
        ]
    }

    Kirigami.PromptDialog {
        id: renameDialog
        property var chatId
        title: i18n("Rename Chat")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: chats.rename(chatId, renameField.text)
        QQC2.TextField {
            id: renameField
            onAccepted: renameDialog.accept()
        }
    }
}
