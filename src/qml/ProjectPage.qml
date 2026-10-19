// SPDX-License-Identifier: GPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// One research project: its sources (articles, saved passages, chats), a chat that answers from them, and documents
Kirigami.Page {
    id: page

    property alias projectId: project.projectId
    property alias project: project
    property alias conversation: conversation
    property alias tab: tabs.currentIndex
    property alias drafting: drafting

    signal openArticle(var libraryId, string path, string fragment)
    signal openChat(var chatId, var articleLibraryId, string articlePath)
    signal openDocument(var documentId)
    signal closed()

    title: project.title
    padding: 0

    actions: [
        Kirigami.Action {
            text: i18n("Edit")
            icon.name: "document-edit"
            onTriggered: {
                editDialog.name = project.title;
                editDialog.goal = project.description;
                editDialog.open();
            }
        },
        Kirigami.Action {
            text: i18n("Export…")
            icon.name: "document-export"
            tooltip: i18n("Save the whole project as one Markdown file")
            onTriggered: exportDialog.open()
        },
        Kirigami.Action {
            text: i18n("Delete")
            icon.name: "edit-delete"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: deleteDialog.open()
        }
    ]

    function showChat(chatId) {
        tabs.currentIndex = 1;
        conversation.chatId = chatId;
    }

    Project {
        id: project
    }

    Conversation {
        id: conversation
        projectId: project.projectId
    }

    ProjectList {
        id: projects
    }

    Drafting {
        id: drafting
        onDocumentReady: documentId => page.openDocument(documentId)
        onFailed: message => applicationWindow().showPassiveNotification(message, "long")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            Layout.bottomMargin: 0
            visible: project.description !== ""
            text: project.description
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
        }

        QQC2.TabBar {
            id: tabs
            Layout.fillWidth: true
            QQC2.TabButton { text: i18np("Sources (%1)", "Sources (%1)", project.articles.length + project.excerpts.length) }
            QQC2.TabButton { text: i18n("Chat") }
            QQC2.TabButton { text: i18np("Documents (%1)", "Documents (%1)", project.documents.length) }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- sources ----
            QQC2.ScrollView {
                id: sourcesView
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
                background: Rectangle { color: Kirigami.Theme.backgroundColor }

                ColumnLayout {
                    width: sourcesView.availableWidth
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.PlaceholderMessage {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.gridUnit * 2
                        visible: project.articles.length === 0 && project.excerpts.length === 0 && project.chats.length === 0
                        icon.name: "bookmark-new"
                        text: i18n("No sources yet")
                        explanation: i18n("Open an article and choose “Add to Project”, or select a passage, right-click it and choose “Save to Project”. You can also add articles here.")
                        helpfulAction: Kirigami.Action {
                            text: i18n("Add Article…")
                            icon.name: "list-add"
                            onTriggered: addDialog.open()
                        }
                    }

                    // articles
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.largeSpacing
                        Layout.bottomMargin: 0
                        visible: project.articles.length > 0
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 3
                            text: i18np("%1 Article", "%1 Articles", project.articles.length)
                        }
                        QQC2.Button {
                            text: i18n("Add Article…")
                            icon.name: "list-add"
                            onClicked: addDialog.open()
                        }
                    }
                    Repeater {
                        model: project.articles
                        delegate: SourceRow {
                            Layout.fillWidth: true
                            heading: modelData.title
                            detail: modelData.available ? modelData.libraryTitle : i18n("%1 (not installed)", modelData.libraryTitle || i18n("library removed"))
                            note: modelData.note
                            iconName: "text-html"
                            onOpen: if (modelData.available) page.openArticle(modelData.libraryId, modelData.path, "")
                            onEditNote: noteDialog.edit(modelData.itemId, modelData.note)
                            onRemove: project.removeItem(modelData.itemId)
                        }
                    }

                    // saved excerpts
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.largeSpacing
                        Layout.bottomMargin: 0
                        visible: project.excerpts.length > 0
                        level: 3
                        text: i18np("%1 Saved Excerpt", "%1 Saved Excerpts", project.excerpts.length)
                    }
                    Repeater {
                        model: project.excerpts
                        delegate: SourceRow {
                            Layout.fillWidth: true
                            quote: modelData.text
                            detail: modelData.title === "" ? i18n("Saved answer")
                                : modelData.sources > 1 ? i18np("%2 and %1 other", "%2 and %1 others", modelData.sources - 1, modelData.title)
                                : modelData.section !== "" ? i18n("%1 – %2", modelData.title, modelData.section) : modelData.title
                            note: modelData.note
                            iconName: "format-text-blockquote"
                            onOpen: if (modelData.available) page.openArticle(modelData.libraryId, modelData.path, "")
                            onEditNote: noteDialog.edit(modelData.itemId, modelData.note)
                            onRemove: project.removeItem(modelData.itemId)
                        }
                    }

                    // chats
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.largeSpacing
                        Layout.bottomMargin: 0
                        visible: project.chats.length > 0
                        level: 3
                        text: i18np("%1 Chat", "%1 Chats", project.chats.length)
                    }
                    Repeater {
                        model: project.chats
                        delegate: SourceRow {
                            Layout.fillWidth: true
                            heading: modelData.title
                            detail: (modelData.itemId > 0 ? i18n("Added from Chats") : i18n("Asked in this project")) + " · "
                                    + i18np("%1 question", "%1 questions", modelData.questions)
                            iconName: "dialog-messages"
                            canNote: false
                            removable: modelData.itemId > 0
                            onOpen: modelData.itemId > 0 ? page.openChat(modelData.chatId, modelData.articleLibraryId, modelData.articlePath)
                                                         : page.showChat(modelData.chatId)
                            onRemove: project.removeItem(modelData.itemId)
                        }
                    }
                    Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
                }
            }

            // ---- chat ----
            ColumnLayout {
                spacing: 0
                QQC2.ToolBar {
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Switch {
                            text: i18n("Also search the whole library")
                            checked: conversation.alsoLibrary
                            onToggled: conversation.alsoLibrary = checked
                            QQC2.ToolTip.text: i18n("Articles from outside the project are shown to the model as a separate group, and answers say when they come from outside.")
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        }
                        Item { Layout.fillWidth: true }
                        QQC2.ToolButton {
                            icon.name: "view-history"
                            text: i18n("Earlier Chats")
                            display: QQC2.AbstractButton.IconOnly
                            enabled: project.chats.some(c => c.itemId === 0)
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            onClicked: chatsMenu.popup()
                        }
                        QQC2.ToolButton {
                            icon.name: "list-add"
                            text: i18n("New Chat")
                            display: QQC2.AbstractButton.IconOnly
                            enabled: conversation.chatId > 0 || conversation.busy
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            onClicked: conversation.chatId = 0
                        }
                    }
                }
                ChatView {
                    id: chat
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    conversation: conversation
                    onOpenArticle: (libraryId, path, fragment) => page.openArticle(libraryId, path, fragment)
                }
                Connections {
                    target: conversation
                    function onSaved() { project.refresh(); }
                }
            }

            // ---- documents ----
            QQC2.ScrollView {
                id: docsView
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
                background: Rectangle { color: Kirigami.Theme.backgroundColor }

                ColumnLayout {
                    width: docsView.availableWidth
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.largeSpacing
                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 3
                            text: i18n("Documents")
                        }
                        QQC2.Button {
                            text: i18n("Draft with AI")
                            icon.name: "tools-wizard"
                            enabled: !drafting.running && (project.articles.length + project.excerpts.length + project.chats.length > 0)
                            onClicked: draftMenu.popup()
                        }
                        QQC2.Button {
                            text: i18n("New Document")
                            icon.name: "document-new"
                            onClicked: {
                                const id = project.createDocument(i18n("Untitled"));
                                if (id > 0)
                                    page.openDocument(id);
                            }
                        }
                    }
                    // an AI draft in progress
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        visible: drafting.running
                        RowLayout {
                            QQC2.BusyIndicator {
                                running: drafting.running
                                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: drafting.total > 1 ? i18n("%1 (%2 of %3)", drafting.label, drafting.step, drafting.total) : drafting.label
                                elide: Text.ElideRight
                            }
                            QQC2.Button {
                                text: i18n("Stop")
                                icon.name: "process-stop"
                                onClicked: drafting.stop()
                            }
                        }
                        QQC2.ProgressBar {
                            Layout.fillWidth: true
                            visible: drafting.total > 1
                            from: 0
                            to: drafting.total
                            value: drafting.step
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: drafting.streamed !== ""
                            text: drafting.streamed
                            textFormat: Text.MarkdownText
                            wrapMode: Text.Wrap
                            color: Kirigami.Theme.disabledTextColor
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("A small local model writes the draft. It becomes a document you can edit; check it against the sources.")
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                            wrapMode: Text.Wrap
                        }
                    }

                    Kirigami.PlaceholderMessage {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.gridUnit * 2
                        visible: project.documents.length === 0 && !drafting.running
                        icon.name: "document-new"
                        text: i18n("No documents yet")
                        explanation: i18n("Write notes, an outline or a report in Markdown, citing the project's articles and excerpts.")
                    }
                    Repeater {
                        model: project.documents
                        delegate: SourceRow {
                            Layout.fillWidth: true
                            heading: modelData.title
                            detail: i18np("%1 character", "%1 characters", modelData.characters) + " · "
                                    + Qt.formatDateTime(modelData.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat))
                            iconName: "text-markdown"
                            canNote: false
                            removeText: i18n("Delete Document")
                            onOpen: page.openDocument(modelData.documentId)
                            onRemove: {
                                deleteDocDialog.documentId = modelData.documentId;
                                deleteDocDialog.subtitle = i18n("Delete “%1”? This cannot be undone.", modelData.title);
                                deleteDocDialog.open();
                            }
                        }
                    }
                }
            }
        }
    }

    QQC2.Menu {
        id: draftMenu
        QQC2.MenuItem {
            text: i18n("Summary of the Project")
            icon.name: "view-list-text"
            enabled: project.articles.length + project.excerpts.length > 0
            onTriggered: drafting.summarizeProject(project.projectId)
        }
        QQC2.MenuSeparator { visible: project.chats.length > 0 }
        QQC2.MenuItem {
            enabled: false
            visible: project.chats.length > 0
            text: i18n("Notes from a chat:")
        }
        Instantiator {
            model: project.chats
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.title
                icon.name: "dialog-messages"
                onTriggered: drafting.summarizeChat(project.projectId, modelData.chatId)
            }
            onObjectAdded: (index, object) => draftMenu.addItem(object)
            onObjectRemoved: (index, object) => draftMenu.removeItem(object)
        }
    }

    QQC2.Menu {
        id: chatsMenu
        Instantiator {
            model: project.chats.filter(c => c.itemId === 0)
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.title
                checkable: true
                checked: modelData.chatId === conversation.chatId
                onTriggered: conversation.chatId = modelData.chatId
            }
            onObjectAdded: (index, object) => chatsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => chatsMenu.removeItem(object)
        }
    }

    ProjectEditDialog {
        id: editDialog
        title: i18n("Edit Project")
        onDone: (name, goal) => project.update(name, goal)
    }

    Kirigami.PromptDialog {
        id: noteDialog
        property var itemId
        title: i18n("Note")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        function edit(itemId, note) {
            noteDialog.itemId = itemId;
            noteField.text = note;
            open();
            noteField.forceActiveFocus();
        }
        onAccepted: project.setNote(itemId, noteField.text)
        QQC2.TextArea {
            id: noteField
            implicitWidth: Kirigami.Units.gridUnit * 22
            implicitHeight: Kirigami.Units.gridUnit * 5
            wrapMode: TextEdit.Wrap
        }
    }

    Kirigami.PromptDialog {
        id: deleteDialog
        title: i18n("Delete Project")
        subtitle: i18n("Delete “%1” with its sources, notes and documents? Its chats are kept. This cannot be undone.", project.title)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    deleteDialog.close();
                    projects.remove(project.projectId);
                    page.closed();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: deleteDialog.close() }
        ]
    }

    Kirigami.PromptDialog {
        id: deleteDocDialog
        property var documentId
        title: i18n("Delete Document")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    project.removeDocument(deleteDocDialog.documentId);
                    deleteDocDialog.close();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: deleteDocDialog.close() }
        ]
    }

    FileDialog {
        id: exportDialog
        title: i18n("Export Project")
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("Markdown (*.md)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + project.exportName()
        onAccepted: {
            const error = project.exportTo(selectedFile);
            applicationWindow().showPassiveNotification(error !== "" ? error : i18n("Exported to %1", selectedFile.toString().replace("file://", "")),
                                                        error !== "" ? "long" : "short");
        }
    }

    // Add an article by title, from any library
    Kirigami.Dialog {
        id: addDialog
        title: i18n("Add Article")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 26
        preferredHeight: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Close
        property var found: []
        onOpened: {
            search.text = "";
            found = [];
            search.forceActiveFocus();
        }
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.SearchField {
                id: search
                Layout.fillWidth: true
                placeholderText: i18n("Article title…")
                onTextChanged: addDialog.found = Search.suggest(text, -1)
            }
            Repeater {
                model: addDialog.found
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    readonly property bool added: project.articles.some(a => a.libraryId === modelData.libraryId && a.path === modelData.path)
                    text: modelData.title
                    icon.name: added ? "checkmark" : "list-add"
                    enabled: !added
                    contentItem: ColumnLayout {
                        spacing: 0
                        QQC2.Label { text: modelData.title; Layout.fillWidth: true; elide: Text.ElideRight }
                        QQC2.Label {
                            text: added ? i18n("%1 · already in the project", modelData.libraryTitle) : modelData.libraryTitle
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }
                    }
                    onClicked: projects.addArticle(project.projectId, modelData.libraryId, modelData.path, modelData.title)
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
