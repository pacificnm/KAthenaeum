// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import KAthenaeum

// Shows an article from zim:// (sanitised by the app; JavaScript off). Links inside the library stay in this page,
// with Back and Forward; links to the web open in the web browser. "Ask" opens a chat about this article beside it
// (over it, in a narrow window).
Kirigami.Page {
    id: page

    required property url url
    // A section to show once the article has loaded and is in place. Loading "page#section" directly does not always
    // scroll there (a citation can open Ray (film) at the top instead of at Plot).
    property string fragment
    property var info: ({})
    property bool asking: false   // the chat panel is open
    property bool notesOpen: false  // the highlights panel is open (one panel at a time)
    // a lesson's reading: the lesson (the teacher's introduction and questions) beside it, open from the start
    property var lessonId: 0
    property bool lessonOpen: lessonId > 0
    property var lesson: ({})
    readonly property bool panelOpen: asking || notesOpen || lessonOpen
    property var highlights: []   // this article's, for the Notes panel
    property var location: ({})   // where the map shows the article's subject (MapManager.locate), or nothing
    property var openChatId: 0    // a saved chat about this article to show once it has loaded (from the Chats page)
    property alias conversation: conversation

    signal openArticle(var libraryId, string path, string fragment)
    signal openSettings()
    signal showOnMap(var location, var libraryId, string path, string title)
    signal openLibraryManager()
    signal openProject(var projectId)
    signal openTimeline(var libraryId, string path, string title)

    title: info.title !== undefined ? Util.pageTitle(info.found === false && page.conversation.articleTitle !== "" ? page.conversation.articleTitle : info.title) : i18n("Loading…")
    padding: 0

    function jumpTo(id) {
        view.url = LibraryBrowser.articleUrl(info.libraryId, info.path).toString() + "#" + encodeURIComponent(id);
    }

    // The chat follows the article shown: following a link to another article starts a chat about that one
    function syncChat() {
        if (!info.path || conversation.articlePath === info.path && conversation.articleLibraryId === info.libraryId)
            return;
        conversation.chatId = 0;
        conversation.articleLibraryId = info.libraryId;
        conversation.articlePath = info.path;
        conversation.articleTitle = info.title;
        if (page.openChatId) {
            conversation.chatId = page.openChatId;
            page.openChatId = 0;
            page.asking = true;
        }
    }
    onInfoChanged: {
        syncChat();
        refreshNotes();
        locate();
    }
    onAskingChanged: if (asking) { notesOpen = false; lessonOpen = false; }
    onNotesOpenChanged: if (notesOpen) { asking = false; lessonOpen = false; }
    onLessonOpenChanged: if (lessonOpen) { asking = false; notesOpen = false; }
    function refreshLesson() {
        page.lesson = page.lessonId > 0 ? Classroom.lesson(page.lessonId) : {};
    }
    // the questions to read with first, where they are easy to refer back to, then the rest of the introduction
    readonly property var lessonParts: {
        const intro = page.lesson.intro ?? "";
        const m = intro.match(/(^|\n)##\s*As you read[^\n]*\n([\s\S]*?)(?=\n##\s|$)/i);
        if (!m)
            return { questions: "", rest: intro };
        return { questions: m[2].trim(), rest: (intro.slice(0, m.index) + intro.slice(m.index + m[0].length)).trim() };
    }
    onLessonIdChanged: refreshLesson()
    Component.onCompleted: refreshLesson()
    Connections {
        target: Classroom
        enabled: page.lessonId > 0
        function onCoursesChanged() { page.refreshLesson(); }
        function onIntroReady(lessonId) { if (lessonId === page.lessonId) page.refreshLesson(); }
    }

    function refreshNotes() {
        page.highlights = page.info.found ? Notes.forArticle(page.info.libraryId, page.info.path) : [];
    }
    function locate() {
        page.location = page.info.found && page.info.hasCoordinates ? MapManager.locate(page.info.title, page.info.lat, page.info.lon) : {};
    }
    // a highlight in the page: shown (the panel closed first on a narrow window, as for a citation)
    function showHighlight(id) {
        if (page.narrow && page.panelOpen) {
            page.notesOpen = false;
            page.fragment = "hl-" + id;
            fragmentTimer.restart();
        } else {
            jumpTo("hl-" + id);
        }
    }
    // marked in the page once it is saved (Notes.changed reloads the article)
    function highlight(text, color, note) {
        if (Notes.add(page.info.libraryId, page.info.path, page.info.title, text, color, note ?? "") <= 0)
            return;
        // reading for a lesson whose course is kept in a project: the highlight goes there too
        if (page.lessonId > 0 && Classroom.keepHighlight(page.lessonId, page.info.libraryId, page.info.path, page.info.title, text, note ?? ""))
            applicationWindow().showPassiveNotification(i18n("Highlighted, and saved to “%1”", Util.plain(page.lesson.projectTitle)), "short");
        else if (!page.notesOpen)
            applicationWindow().showPassiveNotification(i18n("Highlighted. Your notes are under Notes."), "short",
                                                        i18n("Show"), () => { if (page) page.notesOpen = true; });  // the page may have closed
    }

    // a citation in the chat: a section of this article scrolls here; another article opens on its own page
    function showSource(libraryId, path, fragment) {
        if (libraryId !== info.libraryId || path !== info.path) {
            page.openArticle(libraryId, path, fragment);
        } else if (page.panelOpen && page.narrow) {
            // the article is hidden under the panel: a jump made now lands anywhere (even the end), so show the
            // article first and jump once it is laid out
            page.asking = false;
            page.notesOpen = false;
            page.lessonOpen = false;
            page.fragment = fragment;
            fragmentTimer.restart();
        } else {
            jumpTo(fragment);
        }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            enabled: view.canGoBack
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: view.goBack()
        },
        Kirigami.Action {
            text: i18n("Forward")
            icon.name: "go-next"
            enabled: view.canGoForward
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: view.goForward()
        },
        Kirigami.Action {
            text: i18n("Ask")
            icon.name: "dialog-messages"
            checkable: true
            checked: page.asking
            enabled: page.info.found === true
            tooltip: i18n("Ask about this article")
            onTriggered: {
                page.asking = !page.asking;
                if (page.asking)
                    chat.focusInput();
            }
        },
        Kirigami.Action {
            text: i18n("Lesson")
            icon.name: "applications-education-symbolic"
            visible: page.lessonId > 0
            checkable: true
            checked: page.lessonOpen
            tooltip: i18n("The lesson this reading is for: the teacher's introduction and the questions to read with")
            onTriggered: page.lessonOpen = !page.lessonOpen
        },
        Kirigami.Action {
            text: page.highlights.length > 0 ? i18n("Notes (%1)", page.highlights.length) : i18n("Notes")
            icon.name: "draw-highlight"
            checkable: true
            checked: page.notesOpen
            enabled: page.info.found === true
            tooltip: i18n("Your highlights and notes in this article. Select text and right-click to highlight it.")
            onTriggered: page.notesOpen = !page.notesOpen
        },
        Kirigami.Action {
            text: i18n("Cite")
            icon.name: "format-text-blockquote"
            enabled: page.info.found === true
            tooltip: i18n("Cite this article in MLA, APA or Chicago style")
            onTriggered: {
                citeDialog.citations = LibraryBrowser.cite(page.info.libraryId, page.info.path, page.info.title);
                citeDialog.open();
            }
        },
        Kirigami.Action {
            text: i18n("Show on Map")
            icon.name: "mark-location"
            visible: page.location.lat !== undefined
            tooltip: i18n("Show where this is on your downloaded maps")
            onTriggered: page.showOnMap(Object.assign({}, page.location, { libraryTitle: page.info.libraryTitle }), page.info.libraryId, page.info.path, page.info.title)
        },
        Kirigami.Action {
            text: i18n("Add to Project…")
            icon.name: "bookmark-new"
            enabled: page.info.found === true
            tooltip: i18n("Add this article to a research project")
            onTriggered: {
                articlePicker.already = projects.containing(page.info.libraryId, page.info.path);
                articlePicker.open();
            }
        },
        Kirigami.Action {
            text: i18n("Contents")
            icon.name: "view-list-text"
            enabled: (page.info.toc ?? []).length > 0
            onTriggered: tocMenu.popup()
        },
        Kirigami.Action {
            text: i18n("Timeline")
            icon.name: "view-calendar-timeline"
            enabled: page.info.found === true
            displayHint: Kirigami.DisplayHint.AlwaysHide
            tooltip: i18n("The article's dated events, in order")
            onTriggered: page.openTimeline(page.info.libraryId, page.info.path, page.info.title)
        },
        Kirigami.Action {
            text: i18n("Use the Whole Window")
            icon.name: "zoom-fit-width"
            checkable: true
            checked: LibraryBrowser.fullWidth
            displayHint: Kirigami.DisplayHint.AlwaysHide
            tooltip: i18n("Articles across the whole window, or in a column of comfortable reading width")
            onTriggered: LibraryBrowser.fullWidth = checked
        }
    ]

    Connections {
        target: LibraryBrowser
        function onFullWidthChanged() {
            view.reload();
        }
    }
    // a highlight changed here or on the Notes page: the panel and the marks follow
    Connections {
        target: Notes
        function onChanged() {
            const before = JSON.stringify(page.highlights.map(h => [h.id, h.color, h.note]));
            page.refreshNotes();
            if (JSON.stringify(page.highlights.map(h => [h.id, h.color, h.note])) !== before)
                view.reload();
        }
    }

    readonly property bool narrow: width < Kirigami.Units.gridUnit * 40

    Conversation {
        id: conversation
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

    WebEngineView {
        id: view
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !(page.panelOpen && page.narrow)
        url: page.url

        settings.javascriptEnabled: false
        settings.javascriptCanOpenWindows: false
        settings.pluginsEnabled: false
        settings.localStorageEnabled: false
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: false
        settings.navigateOnDropEnabled: false  // a file dropped on the article must not replace it

        onLoadingChanged: loadingInfo => {
            if (loadingInfo.status === WebEngineView.LoadSucceededStatus && page.fragment !== "")
                fragmentTimer.start();
        }

        onUrlChanged: {
            if (LibraryBrowser.isArticleUrl(url))
                page.info = LibraryBrowser.articleInfo(url);
        }

        // Only zim:// is shown here; a link to the web goes to the web browser, anything else nowhere
        function openOutside(target) {
            const s = target.toString();
            if (/^(https?|mailto):/i.test(s)) {
                Qt.openUrlExternally(target);
                applicationWindow().showPassiveNotification(i18n("Opened in your web browser"));
            }
        }
        onNavigationRequested: function(request) {
            if (request.url.toString().startsWith("zim:"))
                return;
            request.reject();
            // only a link the reader clicked opens in the browser, never a page navigating by itself
            if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation)
                openOutside(request.url);
        }
        onNewWindowRequested: function(request) {
            if (request.requestedUrl.toString().startsWith("zim:"))
                view.url = request.requestedUrl;
            else
                openOutside(request.requestedUrl);
        }

        // Selected text: Copy, or ask about it
        // Always the app's own menu: Chromium's (Back and Forward that never worked here, Save Page, View Page Source)
        // would come up wherever nothing is selected
        onContextMenuRequested: function(request) {
            request.accepted = true;
            if (request.selectedText.trim() !== "") {
                selectionMenu.selection = request.selectedText.trim();
                selectionMenu.at = request.position;
                selectionMenu.popup();
            } else {
                pageMenu.link = request.linkUrl.toString();
                pageMenu.popup();
            }
        }
    }

    Kirigami.Separator {
        Layout.fillHeight: true
        visible: page.panelOpen && !page.narrow
    }

    // the lesson panel: what the teacher asked to look for, beside the reading
    ColumnLayout {
        Layout.fillHeight: true
        Layout.fillWidth: page.narrow
        Layout.preferredWidth: page.narrow ? -1 : Math.max(Kirigami.Units.gridUnit * 20, page.width * 0.32)
        visible: page.lessonOpen
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: i18n("Lesson %1 of %2 · %3", page.lesson.number ?? 0, page.lesson.count ?? 0, page.lesson.courseTitle ?? "")
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                    }
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 4
                        textFormat: Text.PlainText
                        text: page.lesson.title ?? ""
                        elide: Text.ElideRight
                    }
                }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    text: i18n("Close")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: page.lessonOpen = false
                }
            }
        }
        QQC2.ScrollView {
            id: lessonScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
            ColumnLayout {
                width: lessonScroll.availableWidth
                spacing: Kirigami.Units.largeSpacing
                Item { implicitHeight: Kirigami.Units.smallSpacing }
                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: text !== ""
                    textFormat: Text.PlainText
                    text: page.lesson.objective ?? ""
                    wrapMode: Text.Wrap
                    font.italic: true
                    opacity: 0.75
                }
                // the questions to answer while reading
                Kirigami.ShadowedRectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: page.lessonParts.questions !== ""
                    implicitHeight: questionsLayout.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.cornerRadius * 1.5
                    color: Qt.alpha(Kirigami.Theme.highlightColor, 0.1)
                    ColumnLayout {
                        id: questionsLayout
                        anchors { fill: parent; margins: Kirigami.Units.largeSpacing }
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Label {
                            textFormat: Text.PlainText
                            text: i18n("As you read")
                            font.weight: Font.DemiBold
                        }
                        TextEdit {
                            Layout.fillWidth: true
                            text: Util.markdown(page.lessonParts.questions)
                            textFormat: TextEdit.MarkdownText
                            wrapMode: TextEdit.Wrap
                            readOnly: true
                            selectByMouse: true
                            color: Kirigami.Theme.textColor
                            selectedTextColor: Kirigami.Theme.highlightedTextColor
                            selectionColor: Kirigami.Theme.highlightColor
                            font: Kirigami.Theme.defaultFont
                        }
                    }
                }
                // the rest of the teacher's introduction: the main idea and key words (Markdown from the model, tidied)
                TextEdit {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: text !== ""
                    text: Util.markdown(page.lessonParts.rest)
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    color: Kirigami.Theme.textColor
                    selectedTextColor: Kirigami.Theme.highlightedTextColor
                    selectionColor: Kirigami.Theme.highlightColor
                    font: Kirigami.Theme.defaultFont
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: (page.lesson.intro ?? "") === ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    opacity: 0.7
                    text: i18n("The teacher has not written this lesson's introduction yet. Open the lesson to have it written.")
                }
                // the lesson's readings: this one marked, the others a click away
                QQC2.Label {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    visible: (page.lesson.readings ?? []).length > 1
                    textFormat: Text.PlainText
                    text: i18n("Readings")
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: (page.lesson.readings ?? []).length > 1 ? page.lesson.readings : []
                    delegate: QQC2.ItemDelegate {
                        id: lessonReading
                        required property var modelData
                        readonly property bool current: modelData.libraryId === page.info.libraryId && modelData.path === page.info.path
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        enabled: modelData.available && !current
                        icon.name: current ? "go-next" : "document-open"
                        text: modelData.n + ". " + Util.plain(modelData.title)
                        font.weight: current ? Font.DemiBold : Font.Normal
                        onClicked: view.url = LibraryBrowser.articleUrl(modelData.libraryId, modelData.path)
                    }
                }
                AppButton {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    visible: (page.lesson.projectId ?? 0) > 0
                    kind: "flat"
                    icon.name: "folder-bookmark"
                    text: i18n("Open “%1”", Util.plain(page.lesson.projectTitle ?? ""))
                    QQC2.ToolTip.text: i18n("The course's project: its readings, your highlights and the lesson documents")
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: page.openProject(page.lesson.projectId)
                }
                AppButton {
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    kind: page.lesson.completed ? "secondary" : "primary"
                    icon.name: page.lesson.completed ? "edit-undo" : "checkmark"
                    text: page.lesson.completed ? i18n("Mark as Not Done") : i18n("I've Finished This Lesson")
                    onClicked: Classroom.setCompleted(page.lessonId, !page.lesson.completed)
                }
            }
        }
    }

    // the highlights panel
    ColumnLayout {
        Layout.fillHeight: true
        Layout.fillWidth: page.narrow
        Layout.preferredWidth: page.narrow ? -1 : Math.max(Kirigami.Units.gridUnit * 18, page.width * 0.3)
        visible: page.notesOpen
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    textFormat: Text.PlainText
                    text: i18n("Notes in this article")
                    elide: Text.ElideRight
                }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    text: i18n("Close")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: page.notesOpen = false
                }
            }
        }
        QQC2.ScrollView {
            id: notesScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
            ColumnLayout {
                width: notesScroll.availableWidth
                spacing: Kirigami.Units.largeSpacing
                Item { implicitHeight: Kirigami.Units.smallSpacing }
                Repeater {
                    model: page.highlights
                    delegate: HighlightCard {
                        required property var modelData
                        highlight: modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        onGoTo: page.showHighlight(modelData.id)
                        onAddToProject: {
                            highlightPicker.highlightId = modelData.id;
                            highlightPicker.preview = modelData.text;
                            highlightPicker.open();
                        }
                    }
                }
            }
        }
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Kirigami.Units.gridUnit
            visible: page.highlights.length === 0
            icon.name: "draw-highlight"
            text: i18n("No highlights yet")
            explanation: i18n("Select a passage, right-click it and choose a colour under Highlight. Add a note to say why it matters.")
        }
    }

    ColumnLayout {
        Layout.fillHeight: true
        Layout.fillWidth: page.narrow
        Layout.preferredWidth: page.narrow ? -1 : Math.max(Kirigami.Units.gridUnit * 20, page.width * 0.38)
        visible: page.asking
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    level: 4
                    text: conversation.chatId > 0 ? conversation.title : i18n("Ask about this article")
                    elide: Text.ElideRight
                }
                QQC2.ToolButton {
                    icon.name: "view-history"
                    text: i18n("Earlier Chats")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: articleChats.count > 0
                    QQC2.ToolTip.text: articleChats.count > 0 ? i18np("%1 earlier chat about this article", "%1 earlier chats about this article", articleChats.count)
                                                                : i18n("No earlier chats about this article")
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        articleChats.refresh();
                        chatsMenu.popup();
                    }
                }
                QQC2.ToolButton {
                    icon.name: "list-add"
                    text: i18n("New Chat")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: conversation.chatId > 0 || conversation.busy
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        conversation.chatId = 0;
                        chat.focusInput();
                    }
                }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    text: i18n("Close")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: page.asking = false
                }
            }
        }

        ChatView {
            id: chat
            Layout.fillWidth: true
            Layout.fillHeight: true
            conversation: conversation
            onOpenArticle: (libraryId, path, fragment) => page.showSource(libraryId, path, fragment)
            onOpenSettings: page.openSettings()
        }
    }
    }

    ProjectList {
        id: projects
    }
    ProjectPicker {
        id: articlePicker
        title: i18n("Add Article to Project")
        onChosen: (projectId, projectTitle, note) => {
            if (projects.addArticle(projectId, page.info.libraryId, page.info.path, page.info.title, note))
                applicationWindow().showPassiveNotification(i18n("Added to “%1”", Util.plain(projectTitle)));
        }
    }
    ProjectPicker {
        id: excerptPicker
        title: i18n("Save Passage to Project")
        onChosen: (projectId, projectTitle, note) => {
            if (projects.addExcerpt(projectId, excerptPicker.preview, page.info.libraryId, page.info.path, page.info.title, "", note)) {
                applicationWindow().showPassiveNotification(i18n("Saved to “%1”", Util.plain(projectTitle)));
                view.reload();  // shows it marked (the picker has closed, so no selection is lost: Athenaeum's lesson)
            }
        }
    }

    // the passage highlighted with a note at once
    Kirigami.Dialog {
        id: noteDialog
        property string passage
        property string color: "yellow"
        title: i18n("Highlight with Note")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 26
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onOpened: {
            noteArea.text = "";
            noteArea.forceActiveFocus();
        }
        onAccepted: page.highlight(passage, color, noteArea.text)
        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: "“" + (noteDialog.passage.length > 280 ? noteDialog.passage.slice(0, 280) + "…" : noteDialog.passage) + "”"
                wrapMode: Text.Wrap
                font.family: "Noto Serif"
                opacity: 0.85
            }
            QQC2.TextArea {
                id: noteArea
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                placeholderText: i18n("Why this matters, a question it raises…")
            }
            RowLayout {
                spacing: Kirigami.Units.smallSpacing
                QQC2.Label { textFormat: Text.PlainText; text: i18n("Colour:") }
                Repeater {
                    model: Notes.colors
                    delegate: QQC2.AbstractButton {
                        required property string modelData
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: implicitWidth
                        Accessible.name: modelData
                        onClicked: noteDialog.color = modelData
                        contentItem: Item {}
                        background: Rectangle {
                            radius: width / 2
                            color: Notes.swatch(parent.modelData)
                            border.width: noteDialog.color === parent.modelData ? 2 : 0
                            border.color: Kirigami.Theme.textColor
                        }
                    }
                }
            }
        }
    }

    ProjectPicker {
        id: highlightPicker
        property var highlightId: 0
        title: i18n("Save Highlight to Project")
        withNote: false
        onChosen: (projectId, projectTitle) => {
            if (Notes.addToProject(projectId, highlightId))
                applicationWindow().showPassiveNotification(i18n("Saved to “%1”", Util.plain(projectTitle)));
        }
    }

    CitationDialog {
        id: citeDialog
        title: i18n("Cite “%1”", Util.plain(page.info.title ?? ""))
        explanation: i18n("Check your teacher's instructions: some want the date you read it, some the address on the web. The article was read in an offline copy, which is dated.")
    }

    DefinitionPopup {
        id: definition
        onOpenEntry: (libraryId, path) => page.openArticle(libraryId, path, "")
        onGetDictionary: page.openLibraryManager()
    }

    ChatList {
        id: articleChats
        articleLibraryId: page.info.libraryId ?? 0
        articlePath: page.info.path ?? ""
    }
    Connections {
        target: conversation
        function onSaved() { articleChats.refresh(); }
    }

    QQC2.Menu {
        id: chatsMenu
        Instantiator {
            model: articleChats
            delegate: QQC2.MenuItem {
                required property var chatId
                required property string title
                text: Util.plain(title)
                checkable: true
                checked: chatId === conversation.chatId
                onTriggered: conversation.chatId = chatId
            }
            onObjectAdded: (index, object) => chatsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => chatsMenu.removeItem(object)
        }
    }

    // a right-click with nothing selected: a link, or the page
    QQC2.Menu {
        id: pageMenu
        property string link
        QQC2.MenuItem {
            text: i18n("Open Link")
            icon.name: "quickopen"
            visible: pageMenu.link !== ""
            height: visible ? implicitHeight : 0
            onTriggered: pageMenu.link.startsWith("zim:") ? view.url = pageMenu.link : view.openOutside(pageMenu.link)
        }
        QQC2.MenuSeparator {
            visible: pageMenu.link !== ""
            height: visible ? implicitHeight : 0
        }
        QQC2.MenuItem {
            text: i18n("Back")
            icon.name: "go-previous"
            enabled: view.canGoBack
            onTriggered: view.goBack()
        }
        QQC2.MenuItem {
            text: i18n("Forward")
            icon.name: "go-next"
            enabled: view.canGoForward
            onTriggered: view.goForward()
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Ask About This Article")
            icon.name: "dialog-messages"
            enabled: page.info.found === true
            onTriggered: {
                page.asking = true;
                chat.focusInput();
            }
        }
        QQC2.MenuItem {
            text: page.highlights.length > 0 ? i18n("Notes (%1)", page.highlights.length) : i18n("Notes")
            icon.name: "draw-highlight"
            enabled: page.info.found === true
            onTriggered: page.notesOpen = true
        }
        QQC2.MenuItem {
            text: i18n("Cite This Article…")
            icon.name: "format-text-blockquote"
            enabled: page.info.found === true
            onTriggered: {
                citeDialog.citations = LibraryBrowser.cite(page.info.libraryId, page.info.path, page.info.title);
                citeDialog.open();
            }
        }
        QQC2.MenuItem {
            text: i18n("Show on Map")
            icon.name: "mark-location"
            visible: page.location.lat !== undefined
            height: visible ? implicitHeight : 0
            onTriggered: page.showOnMap(Object.assign({}, page.location, { libraryTitle: page.info.libraryTitle }), page.info.libraryId, page.info.path, page.info.title)
        }
        QQC2.MenuItem {
            text: i18n("Contents")
            icon.name: "view-list-text"
            enabled: (page.info.toc ?? []).length > 0
            onTriggered: tocMenu.popup()
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Select All")
            icon.name: "edit-select-all"
            onTriggered: view.triggerWebAction(WebEngineView.SelectAll)
        }
    }

    QQC2.Menu {
        id: selectionMenu
        property string selection
        property point at
        // a word or two: offered to the dictionary
        readonly property bool word: selection !== "" && selection.length <= 40 && selection.split(/\s+/).length <= 3
        QQC2.MenuItem {
            text: i18n("Copy")
            icon.name: "edit-copy"
            onTriggered: view.triggerWebAction(WebEngineView.Copy)
        }
        QQC2.MenuItem {
            text: selectionMenu.word ? i18n("Define “%1”", Util.plain(selectionMenu.selection)) : ""
            icon.name: "accessories-dictionary-symbolic"
            visible: selectionMenu.word
            height: visible ? implicitHeight : 0
            onTriggered: definition.show(selectionMenu.selection, selectionMenu.at)
        }
        QQC2.MenuSeparator {}
        QQC2.Menu {
            title: i18n("Highlight")
            icon.name: "draw-highlight"
            Repeater {
                model: Notes.colors.map(c => ({ color: c, text: Notes.colorName(c) }))
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.text
                    icon.name: "draw-highlight"
                    icon.color: Qt.darker(Notes.swatch(modelData.color), 1.15)
                    onTriggered: page.highlight(selectionMenu.selection, modelData.color)
                }
            }
        }
        QQC2.MenuItem {
            text: i18n("Highlight with Note…")
            icon.name: "document-edit"
            onTriggered: {
                noteDialog.passage = selectionMenu.selection;
                noteDialog.open();
            }
        }
        QQC2.MenuItem {
            text: i18n("Save to Project…")
            icon.name: "bookmark-new"
            onTriggered: {
                excerptPicker.preview = selectionMenu.selection;
                excerptPicker.open();
            }
        }
        QQC2.MenuItem {
            text: i18n("Ask About This")
            icon.name: "dialog-messages"
            onTriggered: {
                page.asking = true;
                const quote = selectionMenu.selection.length > 300 ? selectionMenu.selection.slice(0, 300) + "…" : selectionMenu.selection;
                chat.setQuestion(i18n("What does the article mean by “%1”?", quote));
            }
        }
    }

    // Jumping as soon as the page has loaded did not scroll either: the page is still sliding in and the view has not
    // been laid out. A moment later it works.
    Timer {
        id: fragmentTimer
        interval: Kirigami.Units.longDuration * 2
        onTriggered: {
            const id = page.fragment;
            page.fragment = "";
            page.jumpTo(id);
        }
    }

    // a different colour scheme: show the article in the new colours
    readonly property color themeBackground: Kirigami.Theme.backgroundColor
    onThemeBackgroundChanged: view.reload()

    QQC2.Menu {
        id: tocMenu
        Instantiator {
            model: page.info.toc ?? []
            delegate: QQC2.MenuItem {
                required property var modelData
                text: (modelData.level === 3 ? "    " : "") + Util.plain(modelData.text)
                onTriggered: page.jumpTo(modelData.id)
            }
            onObjectAdded: (index, object) => tocMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => tocMenu.removeItem(object)
        }
    }
}
