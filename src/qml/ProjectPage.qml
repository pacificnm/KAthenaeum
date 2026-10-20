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

    signal openArticle(var libraryId, string path, string fragment)
    signal openChat(var chatId, var articleLibraryId, string articlePath)
    signal openDocument(var documentId)
    signal openCourse(var courseId)
    signal closed()

    // the classroom course this project is kept for, if any
    readonly property var course: project.projectId > 0 && project.exists ? Classroom.courseForProject(project.projectId) : ({})

    title: Util.pageTitle(project.title)
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
            text: i18n("Bibliography")
            icon.name: "format-text-blockquote"
            enabled: project.articles.length > 0
            tooltip: i18n("The project's articles cited in MLA, APA or Chicago style, to copy into your work")
            onTriggered: {
                bibliographyDialog.citations = project.bibliography();
                bibliographyDialog.open();
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

    CitationDialog {
        id: bibliographyDialog
        title: i18n("Bibliography")
        explanation: i18np("%1 article, in alphabetical order. A document's Export lists only the articles it links to.",
                           "%1 articles, in alphabetical order. A document's Export lists only the articles it links to.", project.articles.length)
    }

    // the app's one drafting job (Main.qml opens what it writes); this page shows it when it is for this project
    readonly property bool drafting: Drafting.running && Drafting.projectId === project.projectId

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            Layout.bottomMargin: 0
            visible: project.description !== ""
            text: project.description
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            visible: page.course.id !== undefined
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                source: "applications-education-symbolic"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                opacity: 0.7
            }
            QQC2.Label {
                textFormat: Text.PlainText
                text: i18n("Kept for the course “%1”", page.course.title ?? "")
                opacity: 0.7
            }
            AppButton {
                kind: "flat"
                text: i18n("Open Course")
                onClicked: page.openCourse(page.course.id)
            }
            Item { Layout.fillWidth: true }
        }

        SegmentedTabs {
            id: tabs
            Layout.fillWidth: true
            tabs: [
                { text: i18np("Sources (%1)", "Sources (%1)", project.articles.length + project.excerpts.length + project.chats.length), icon: "bookmarks" },
                { text: i18n("Chat"), icon: "dialog-messages" },
                { text: i18np("Documents (%1)", "Documents (%1)", project.documents.length), icon: "document-multiple" }
            ]
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
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            level: 3
                            text: i18np("%1 Article", "%1 Articles", project.articles.length)
                        }
                        AppButton {
                            kind: "primary"
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
                            detail: (modelData.available ? modelData.libraryTitle : i18n("%1 (not installed)", modelData.libraryTitle || i18n("library removed")))
                                    + (modelData.lesson !== "" ? " · " + modelData.lesson : "")
                            note: modelData.note
                            iconName: "text-html"
                            onOpen: if (modelData.available) page.openArticle(modelData.libraryId, modelData.path, "")
                            onEditNote: noteDialog.edit(modelData.itemId, modelData.note)
                            onRemove: project.removeItem(modelData.itemId)
                        }
                    }

                    // saved excerpts
                    Kirigami.Heading {
                        textFormat: Text.PlainText
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
                            detail: (modelData.title === "" ? i18n("Saved answer")
                                : modelData.sources > 1 ? i18np("%2 and %1 other", "%2 and %1 others", modelData.sources - 1, modelData.title)
                                : modelData.section !== "" ? i18n("%1 – %2", modelData.title, modelData.section) : modelData.title)
                                + (modelData.lesson !== "" ? " · " + modelData.lesson : "")
                            note: modelData.note
                            iconName: "format-text-blockquote"
                            onOpen: if (modelData.available) page.openArticle(modelData.libraryId, modelData.path, "")
                            onEditNote: noteDialog.edit(modelData.itemId, modelData.note)
                            onRemove: project.removeItem(modelData.itemId)
                        }
                    }

                    // chats
                    Kirigami.Heading {
                        textFormat: Text.PlainText
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
                            detail: modelData.itemId > 0 ? i18np("Added from Chats · %1 question", "Added from Chats · %1 questions", modelData.questions)
                                                         : i18np("Asked in this project · %1 question", "Asked in this project · %1 questions", modelData.questions)
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
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            level: 3
                            text: i18n("Documents")
                        }
                        AppButton {
                            kind: "secondary"
                            text: i18n("Draft with AI")
                            icon.name: "tools-wizard"
                            enabled: !Drafting.running
                            onClicked: draftMenu.popup()
                        }
                        AppButton {
                            kind: "primary"
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
                        visible: page.drafting
                        RowLayout {
                            QQC2.BusyIndicator {
                                running: page.drafting
                                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                            }
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: Drafting.total > 1 ? i18n("%1 (%2 of %3)", Drafting.label, Drafting.step, Drafting.total) : Drafting.label
                                elide: Text.ElideRight
                            }
                            AppButton {
                                kind: "danger"
                                text: i18n("Stop")
                                icon.name: "process-stop"
                                onClicked: Drafting.stop()
                            }
                        }
                        QQC2.ProgressBar {
                            Layout.fillWidth: true
                            visible: Drafting.total > 1
                            from: 0
                            to: Drafting.total
                            value: Drafting.step
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: Drafting.streamed !== ""
                            text: Drafting.streamed
                            textFormat: Text.MarkdownText
                            wrapMode: Text.Wrap
                            color: Kirigami.Theme.disabledTextColor
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
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
                        visible: project.documents.length === 0 && !page.drafting
                        icon.name: "document-new"
                        text: i18n("No documents yet")
                        explanation: i18n("Write notes, an outline or a report in Markdown, citing the project's articles and excerpts. Documents open in a window of their own, with Kate's editor and a live preview.")
                    }
                    Repeater {
                        model: project.documents
                        delegate: SourceRow {
                            Layout.fillWidth: true
                            heading: modelData.title
                            detail: i18np("%1 character · %2", "%1 characters · %2", modelData.characters,
                                          Qt.formatDateTime(modelData.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat)))
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
            text: i18n("Write From Your Instructions…")
            icon.name: "document-edit"
            onTriggered: reportDialog.open()
        }
        QQC2.MenuItem {
            text: i18n("Summary of the Project")
            icon.name: "view-list-text"
            enabled: project.articles.length + project.excerpts.length > 0
            onTriggered: Drafting.summarizeProject(project.projectId)
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
                text: Util.plain(modelData.title)
                icon.name: "dialog-messages"
                onTriggered: Drafting.summarizeChat(project.projectId, modelData.chatId)
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
                text: Util.plain(modelData.title)
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

    // A report written to the user's instructions: planned into sections, each written from its own sources
    Kirigami.Dialog {
        id: reportDialog
        title: i18n("Write From Your Instructions")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 30
        standardButtons: Kirigami.Dialog.NoButton
        readonly property bool emptyProject: project.articles.length + project.excerpts.length === 0
        onOpened: {
            if (reportTitle.text === "")
                reportTitle.text = project.title;
            if (emptyProject)
                wholeLibrary.checked = true;
            instructions.forceActiveFocus();
        }
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Write")
                icon.name: "tools-wizard"
                enabled: reportTitle.text.trim() !== "" && instructions.text.trim() !== "" && !Drafting.running
                onTriggered: {
                    Drafting.writeReport(project.projectId, reportTitle.text, instructions.text, fullReport.checked, wholeLibrary.checked);
                    reportDialog.close();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: reportDialog.close() }
        ]
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label { textFormat: Text.PlainText; text: i18n("Title") }
            QQC2.TextField {
                id: reportTitle
                Layout.fillWidth: true
            }
            QQC2.Label { textFormat: Text.PlainText; text: i18n("What should it cover, and how?") ; Layout.topMargin: Kirigami.Units.smallSpacing }
            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 9
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff  // the text wraps
                QQC2.TextArea {
                    id: instructions
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    placeholderText: i18n("For example: A full report on how Crater Lake formed. Start with Mount Mazama and the eruption, then the caldera and how the lake filled, then its water clarity and research since. Formal tone, with a short conclusion.")
                }
            }
            QQC2.RadioButton {
                id: shortReport
                text: i18n("Short: about 3 sections")
                checked: true
            }
            QQC2.RadioButton {
                id: fullReport
                text: i18n("Full report: up to 6 longer sections (a few minutes)")
            }
            QQC2.CheckBox {
                id: wholeLibrary
                text: reportDialog.emptyProject ? i18n("Search the whole library (the project has no sources yet)")
                                                : i18n("Also search the whole library, beyond the project's sources")
                enabled: !reportDialog.emptyProject
            }
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                text: i18n("A small local model plans the sections from your instructions, then writes each one from the sources that fit it. KAthenaeum adds the source numbers and a reference list. The result becomes a document you can edit.")
            }
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
                    text: Util.plain(modelData.title)
                    icon.name: added ? "checkmark" : "list-add"
                    enabled: !added
                    contentItem: ColumnLayout {
                        spacing: 0
                        QQC2.Label { textFormat: Text.PlainText; text: modelData.title; Layout.fillWidth: true; elide: Text.ElideRight }
                        QQC2.Label {
                            textFormat: Text.PlainText
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
