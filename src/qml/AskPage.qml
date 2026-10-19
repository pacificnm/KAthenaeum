// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// Ask a question: a local model answers from passages of the installed libraries, and the app links each sentence to
// the source it matches (see CLAUDE.md, "Ask")
Kirigami.Page {
    id: page

    property alias chatId: conversation.chatId
    property alias conversation: conversation

    signal openArticle(var libraryId, string path, string fragment)
    signal openSettings()

    title: conversation.title !== "" ? Util.pageTitle(conversation.title) : i18n("Ask")
    padding: 0

    actions: [
        Kirigami.Action {
            text: i18n("New Chat")
            icon.name: "list-add"
            enabled: conversation.chatId > 0 || conversation.busy
            onTriggered: {
                conversation.chatId = 0;
                chat.focusInput();
            }
        }
    ]

    // for the UI checks: what a click on a citation link does
    function openCitation(row, link) {
        chat.openCitation(row, link);
    }

    Component.onCompleted: chat.focusInput()

    Conversation {
        id: conversation
    }

    ChatView {
        id: chat
        anchors.fill: parent
        conversation: conversation
        onOpenArticle: (libraryId, path, fragment) => page.openArticle(libraryId, path, fragment)
        onOpenSettings: page.openSettings()
    }
}
