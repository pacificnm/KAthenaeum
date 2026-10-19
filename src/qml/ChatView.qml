// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A chat: what is missing to answer (Ollama, a model), the questions and cited answers, and the question box. The Ask
// page and the article panel both show one (see CLAUDE.md, "Ask").
ColumnLayout {
    id: view

    required property Conversation conversation
    property bool follow: true  // keep the newest text in view while an answer is written

    signal openArticle(var libraryId, string path, string fragment)
    signal openSettings()

    spacing: 0

    // puts a question in the box, for the user to send or change ("Ask about this" on selected text)
    function setQuestion(text) {
        input.text = text;
        input.forceActiveFocus();
    }
    function focusInput() {
        input.forceActiveFocus();
    }

    function send() {
        if (input.text.trim() === "" || view.conversation.busy)
            return;
        view.follow = true;
        view.conversation.ask(input.text);
        input.clear();
    }

    function openCitation(row, link) {
        if (link.startsWith("cite:")) {
            const s = view.conversation.source(row, parseInt(link.slice(5)));
            if (!s.libraryId)
                return;
            if (!s.available) {
                applicationWindow().showPassiveNotification(i18n("“%1” is no longer installed.", s.libraryTitle || i18n("That library")));
                return;
            }
            view.openArticle(s.libraryId, s.path, s.section ? s.section.replace(/ /g, "_") : "");
        } else if (link.startsWith("check:")) {
            applicationWindow().showPassiveNotification(view.conversation.flagReason(row, parseInt(link.slice(6))), "long");
        }
    }

    function linkTip(row, link) {
        if (link.startsWith("cite:")) {
            const s = view.conversation.source(row, parseInt(link.slice(5)));
            return s.title ? (s.section ? i18n("%1 – %2", s.title, s.section) : s.title) : "";
        }
        if (link.startsWith("check:"))
            return view.conversation.flagReason(row, parseInt(link.slice(6)));
        return "";
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: false  // a nested layout fills by default: empty, it took half the page
        spacing: 0
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Models.checked && !Models.reachable
            type: Kirigami.MessageType.Error
            text: i18n("Ollama is not running at %1, so questions cannot be answered. Reading and searching libraries work without it.", Models.url)
            actions: [
                Kirigami.Action { text: i18n("Check Again"); icon.name: "view-refresh"; onTriggered: Models.refresh() },
                Kirigami.Action { text: i18n("Settings"); icon.name: "settings-configure"; onTriggered: view.openSettings() }
            ]
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Models.reachable && !Models.answerModelInstalled
            type: Kirigami.MessageType.Error
            text: i18n("The answer model “%1” is not installed. Install it in a terminal with: <b>ollama pull %1</b>, or choose another in Settings.", Models.answerModel)
            actions: [
                Kirigami.Action { text: i18n("Check Again"); icon.name: "view-refresh"; onTriggered: Models.refresh() },
                Kirigami.Action { text: i18n("Settings"); icon.name: "settings-configure"; onTriggered: view.openSettings() }
            ]
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: LibraryManager.installed.count === 0
            type: Kirigami.MessageType.Information
            text: i18n("No libraries are installed yet, so there is nothing to answer from.")
            actions: [ Kirigami.Action { text: i18n("Library Manager"); icon.name: "folder-download"; onTriggered: applicationWindow().showLibraryManager() } ]
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


    QQC2.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff  // answers wrap; the margins made it think they did not fit
        // the content colours a ScrollablePage gives, as in the rest of the app
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        background: Rectangle { color: Kirigami.Theme.backgroundColor }

    ListView {
        id: list
        model: view.conversation
        spacing: Kirigami.Units.largeSpacing
        topMargin: Kirigami.Units.largeSpacing
        bottomMargin: Kirigami.Units.largeSpacing
        leftMargin: Kirigami.Units.gridUnit
        rightMargin: Kirigami.Units.gridUnit
        clip: true

        onCountChanged: if (view.follow) Qt.callLater(positionViewAtEnd)
        onContentHeightChanged: if (view.follow && view.conversation.busy) Qt.callLater(positionViewAtEnd)
        onMovementEnded: view.follow = atYEnd

        Kirigami.PlaceholderMessage {
            parent: list  // not the list's content, which is taller than the view
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "dialog-messages"
            text: view.conversation.articlePath !== "" ? i18n("Ask about “%1”", Util.plain(view.conversation.articleTitle))
                : view.conversation.projectId > 0 ? i18n("Ask about this project") : i18n("Ask a question")
            explanation: view.conversation.articlePath !== ""
                ? i18n("Answers come from this article alone. Each sentence links to the section it comes from.")
                : view.conversation.projectId > 0
                ? i18n("Answers come from the project's articles and saved excerpts, written with its goal in mind. Switch on “Also search the whole library” to look further.")
                : i18n("A local model answers from your libraries. Each sentence links to the article it comes from; sentences no source backs are marked for checking.")
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
            required property string content

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
                    textFormat: Text.PlainText
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
                onLinkActivated: link => view.openCitation(message.index, link)

                HoverHandler {
                    cursorShape: answer.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
                QQC2.ToolTip.visible: answer.hoveredLink !== "" && QQC2.ToolTip.text !== ""
                QQC2.ToolTip.text: Util.plain(view.linkTip(message.index, answer.hoveredLink))
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            QQC2.Label {
                textFormat: Text.PlainText
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
                visible: message.note !== "" && message.error === ""  // the error already says what went wrong
                type: Kirigami.MessageType.Warning
                text: message.note
            }

            // the sources the model was shown
            QQC2.Label {
                textFormat: Text.PlainText
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
                    onClicked: view.openCitation(message.index, "cite:" + modelData.n)
                    QQC2.ToolTip.visible: hovered && modelData.snippet !== ""
                    QQC2.ToolTip.text: Util.plain(modelData.snippet + "…")
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        QQC2.Label {
                            textFormat: Text.PlainText
                            text: "[" + sourceRow.modelData.n + "]"
                            color: Kirigami.Theme.linkColor
                            Layout.alignment: Qt.AlignTop
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: view.conversation.articlePath !== "" ? (sourceRow.modelData.section || i18n("Introduction"))
                                    : sourceRow.modelData.section ? i18n("%1 – %2", sourceRow.modelData.title, sourceRow.modelData.section) : sourceRow.modelData.title
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                visible: text !== "" && view.conversation.articlePath === ""
                                text: sourceRow.modelData.kind === "library" ? i18n("%1 · outside the project", sourceRow.modelData.libraryTitle ?? "")
                                                                             : sourceRow.modelData.libraryTitle ?? ""
                                color: Kirigami.Theme.disabledTextColor
                                font: Kirigami.Theme.smallFont
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            QQC2.ToolButton {
                visible: message.role === "assistant" && message.status === "done" && message.content !== "" && message.sources.length > 0
                text: i18n("Save to Project…")
                icon.name: "bookmark-new"
                font: Kirigami.Theme.smallFont
                onClicked: {
                    answerPicker.sources = message.sources;
                    answerPicker.preview = message.content;
                    answerPicker.open();
                }
            }

            QQC2.Label {
                textFormat: Text.PlainText
                visible: message.stats !== ""
                Layout.fillWidth: true
                text: message.stats
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                wrapMode: Text.Wrap
            }
        }
    }
    }

    QQC2.ToolBar {
        Layout.fillWidth: true
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.TextField {
                id: input
                // a pasted page as a question pushed the sources out of the model's context (num_ctx 6144; the review)
                maximumLength: 2000
                Layout.fillWidth: true
                placeholderText: list.count > 0 ? i18n("Ask a follow-up question…")
                    : view.conversation.articlePath !== "" ? i18n("Ask about this article…")
                    : view.conversation.projectId > 0 ? i18n("Ask about this project…") : i18n("Ask a question about your libraries…")
                onAccepted: view.send()
            }
            QQC2.Button {
                visible: !view.conversation.busy
                text: i18n("Ask")
                icon.name: "document-send"
                enabled: input.text.trim() !== ""
                onClicked: view.send()
            }
            QQC2.Button {
                visible: view.conversation.busy
                text: i18n("Stop")
                icon.name: "process-stop"
                onClicked: view.conversation.stop()
            }
        }
    }

    ProjectList {
        id: projects
    }
    ProjectPicker {
        id: answerPicker
        property var sources: []
        title: i18n("Save Answer to Project")
        onChosen: (projectId, projectTitle, note) => {
            if (projects.addAnswer(projectId, answerPicker.preview, answerPicker.sources, note))
                applicationWindow().showPassiveNotification(i18n("Saved to “%1”", projectTitle));
        }
    }
}
