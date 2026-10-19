// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// Ask a question: a local model answers from passages of the installed libraries, and the app links each sentence to
// the source it matches (see CLAUDE.md, "Ask")
Kirigami.ScrollablePage {
    id: page

    property alias chatId: conversation.chatId
    property alias conversation: conversation
    property bool follow: true  // keep the newest text in view while an answer is written

    signal openArticle(var libraryId, string path, string fragment)
    signal openSettings()

    title: conversation.title !== "" ? conversation.title : i18n("Ask")

    actions: [
        Kirigami.Action {
            text: i18n("New Chat")
            icon.name: "list-add"
            enabled: conversation.chatId > 0 || list.count > 0
            onTriggered: {
                conversation.chatId = 0;
                input.forceActiveFocus();
            }
        }
    ]

    function send() {
        if (input.text.trim() === "" || conversation.busy)
            return;
        page.follow = true;
        conversation.ask(input.text);
        input.clear();
    }

    function openCitation(row, link) {
        if (link.startsWith("cite:")) {
            const s = conversation.source(row, parseInt(link.slice(5)));
            if (!s.libraryId)
                return;
            if (!s.available) {
                applicationWindow().showPassiveNotification(i18n("“%1” is no longer installed.", s.libraryTitle || i18n("That library")));
                return;
            }
            page.openArticle(s.libraryId, s.path, s.section ? s.section.replace(/ /g, "_") : "");
        } else if (link.startsWith("check:")) {
            applicationWindow().showPassiveNotification(conversation.flagReason(row, parseInt(link.slice(6))), "long");
        }
    }

    function linkTip(row, link) {
        if (link.startsWith("cite:")) {
            const s = conversation.source(row, parseInt(link.slice(5)));
            return s.title ? (s.section ? i18n("%1 – %2", s.title, s.section) : s.title) : "";
        }
        if (link.startsWith("check:"))
            return conversation.flagReason(row, parseInt(link.slice(6)));
        return "";
    }

    Component.onCompleted: input.forceActiveFocus()

    Conversation {
        id: conversation
    }

    header: ColumnLayout {
        spacing: 0
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Models.checked && !Models.reachable
            type: Kirigami.MessageType.Error
            text: i18n("Ollama is not running at %1, so questions cannot be answered. Reading and searching libraries work without it.", Models.url)
            actions: [
                Kirigami.Action { text: i18n("Check Again"); icon.name: "view-refresh"; onTriggered: Models.refresh() },
                Kirigami.Action { text: i18n("Settings"); icon.name: "settings-configure"; onTriggered: page.openSettings() }
            ]
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Models.reachable && !Models.answerModelInstalled
            type: Kirigami.MessageType.Error
            text: i18n("The answer model “%1” is not installed. Install it in a terminal with: <b>ollama pull %1</b>", Models.answerModel)
            actions: [ Kirigami.Action { text: i18n("Check Again"); icon.name: "view-refresh"; onTriggered: Models.refresh() } ]
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Models.reachable && Models.answerModelInstalled && !Models.embeddingModelInstalled
            type: Kirigami.MessageType.Warning
            text: i18n("The embedding model “%1” is not installed, so sources are ranked by words only and sentences are not linked to them. Install it with: <b>ollama pull %1</b>", Models.embeddingModel)
            actions: [ Kirigami.Action { text: i18n("Check Again"); icon.name: "view-refresh"; onTriggered: Models.refresh() } ]
        }
    }

    ListView {
        id: list
        model: conversation
        spacing: Kirigami.Units.largeSpacing
        topMargin: Kirigami.Units.largeSpacing
        bottomMargin: Kirigami.Units.largeSpacing
        leftMargin: Kirigami.Units.gridUnit
        rightMargin: Kirigami.Units.gridUnit
        clip: true

        onCountChanged: if (page.follow) Qt.callLater(positionViewAtEnd)
        onContentHeightChanged: if (page.follow && conversation.busy) Qt.callLater(positionViewAtEnd)
        onMovementEnded: page.follow = atYEnd

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "dialog-messages"
            text: i18n("Ask a question")
            explanation: i18n("A local model answers from your libraries. Each sentence links to the article it comes from; sentences no source backs are marked for checking.")
        }

        delegate: ColumnLayout {
            id: message

            required property int index
            required property string role
            required property string text
            required property string status
            required property string phase
            required property string error
            required property string note
            required property var sources
            required property string stats

            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            spacing: Kirigami.Units.smallSpacing

            // ---- a question ----
            Rectangle {
                visible: message.role === "user"
                Layout.alignment: Qt.AlignRight
                Layout.maximumWidth: message.width * 0.8
                implicitWidth: question.implicitWidth + Kirigami.Units.largeSpacing * 2
                implicitHeight: question.implicitHeight + Kirigami.Units.largeSpacing
                radius: Kirigami.Units.cornerRadius
                color: Kirigami.Theme.alternateBackgroundColor
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)

                QQC2.Label {
                    id: question
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.Layout.maximumWidth - Kirigami.Units.largeSpacing * 2)
                    text: message.text
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
            }

            // ---- an answer ----
            RowLayout {
                visible: message.role === "assistant" && message.status === "working" && message.phase !== ""
                spacing: Kirigami.Units.smallSpacing
                QQC2.BusyIndicator {
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    running: visible
                }
                QQC2.Label {
                    text: message.phase
                    color: Kirigami.Theme.disabledTextColor
                }
            }

            TextEdit {
                id: answer
                visible: message.role === "assistant" && message.text !== ""
                Layout.fillWidth: true
                text: message.text
                textFormat: TextEdit.MarkdownText
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                color: Kirigami.Theme.textColor
                selectedTextColor: Kirigami.Theme.highlightedTextColor
                selectionColor: Kirigami.Theme.highlightColor
                font: Kirigami.Theme.defaultFont
                onLinkActivated: link => page.openCitation(message.index, link)

                HoverHandler {
                    cursorShape: answer.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
                QQC2.ToolTip.visible: answer.hoveredLink !== "" && QQC2.ToolTip.text !== ""
                QQC2.ToolTip.text: page.linkTip(message.index, answer.hoveredLink)
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            QQC2.Label {
                visible: message.role === "assistant" && message.status === "stopped"
                text: message.text === "" ? i18n("Stopped before the answer began.") : i18n("Stopped.")
                color: Kirigami.Theme.disabledTextColor
                font.italic: true
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: message.error !== ""
                type: Kirigami.MessageType.Error
                text: message.error
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: message.note !== ""
                type: Kirigami.MessageType.Warning
                text: message.note
            }

            // the sources the model was shown
            QQC2.Label {
                visible: message.role === "assistant" && message.sources.length > 0
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: i18n("Sources")
                font.bold: true
            }
            Repeater {
                model: message.role === "assistant" ? message.sources : []
                delegate: QQC2.ItemDelegate {
                    id: sourceRow
                    required property var modelData
                    Layout.fillWidth: true
                    topPadding: Kirigami.Units.smallSpacing
                    bottomPadding: Kirigami.Units.smallSpacing
                    onClicked: page.openCitation(message.index, "cite:" + modelData.n)
                    QQC2.ToolTip.visible: hovered && modelData.snippet !== ""
                    QQC2.ToolTip.text: modelData.snippet + "…"
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        QQC2.Label {
                            text: "[" + sourceRow.modelData.n + "]"
                            color: Kirigami.Theme.linkColor
                            Layout.alignment: Qt.AlignTop
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: sourceRow.modelData.section ? i18n("%1 – %2", sourceRow.modelData.title, sourceRow.modelData.section) : sourceRow.modelData.title
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: sourceRow.modelData.libraryTitle ?? ""
                                color: Kirigami.Theme.disabledTextColor
                                font: Kirigami.Theme.smallFont
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            QQC2.Label {
                visible: message.stats !== ""
                Layout.fillWidth: true
                text: message.stats
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                wrapMode: Text.Wrap
            }
        }
    }

    footer: QQC2.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: list.count === 0 ? i18n("Ask a question about your libraries…") : i18n("Ask a follow-up question…")
                onAccepted: page.send()
            }
            QQC2.Button {
                visible: !conversation.busy
                text: i18n("Ask")
                icon.name: "document-send"
                enabled: input.text.trim() !== ""
                onClicked: page.send()
            }
            QQC2.Button {
                visible: conversation.busy
                text: i18n("Stop")
                icon.name: "process-stop"
                onClicked: conversation.stop()
            }
        }
    }
}
