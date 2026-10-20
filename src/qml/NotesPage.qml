// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import org.kde.kirigami as Kirigami
import KAthenaeum

// Every highlight and its note, by article: found again, written up, exported or turned into a project document.
Kirigami.ScrollablePage {
    id: page

    signal openArticle(var libraryId, string path, string fragment)
    signal openProject(var projectId)

    title: i18n("Notes")

    property string color: ""  // "" for every colour
    property var highlights: []
    function refresh() {
        page.highlights = Notes.all(page.color, search.text);
    }
    Component.onCompleted: refresh()
    Connections {
        target: Notes
        function onChanged() { refreshTimer.restart(); }
    }
    // after a note is written, the list is read again once the field has been left (not under the reader's cursor)
    Timer {
        id: refreshTimer
        interval: 50
        onTriggered: page.refresh()
    }

    readonly property var shownIds: highlights.map(h => h.id)

    actions: [
        Kirigami.Action {
            text: i18n("New Project Document…")
            icon.name: "document-new"
            enabled: page.highlights.length > 0
            tooltip: i18n("The notes shown, as a document in a research project, to write from")
            onTriggered: projectPicker.open()
        },
        Kirigami.Action {
            text: i18n("Export…")
            icon.name: "document-export"
            enabled: page.highlights.length > 0
            tooltip: i18n("Save the notes shown as a Markdown file, with the articles they come from cited")
            onTriggered: exportDialog.open()
        }
    ]

    header: QQC2.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Kirigami.SearchField {
                id: search
                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                placeholderText: i18n("Search your notes…")
                onTextChanged: page.refresh()
            }
            SegmentedTabs {
                id: colorTabs
                Layout.fillWidth: true
                readonly property var values: ["", "yellow", "green", "blue", "pink"]
                tabs: [{ text: i18n("All") }, { text: i18n("Yellow") }, { text: i18n("Green") }, { text: i18n("Blue") }, { text: i18n("Pink") }]
                onCurrentIndexChanged: {
                    page.color = values[currentIndex];
                    page.refresh();
                }
            }
        }
    }

    ListView {
        id: list
        model: page.highlights
        spacing: Kirigami.Units.largeSpacing
        topMargin: Kirigami.Units.largeSpacing
        bottomMargin: Kirigami.Units.gridUnit
        reuseItems: false

        // each article once, above its highlights
        section.property: "articleKey"
        section.criteria: ViewSection.FullString
        section.delegate: QQC2.ItemDelegate {
            id: header
            required property string section
            readonly property var first: page.highlights.find(h => h.articleKey === section) ?? ({})
            width: ListView.view.width
            topPadding: Kirigami.Units.largeSpacing * 2
            leftPadding: Kirigami.Units.largeSpacing * 2
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    source: "text-html"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                Kirigami.Heading {
                    level: 4
                    textFormat: Text.PlainText
                    text: header.first.title || header.first.path || ""
                    color: header.hovered ? Kirigami.Theme.linkColor : Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            onClicked: page.openArticle(first.libraryId, first.path, "")
        }

        delegate: Item {
            id: row
            required property var modelData
            width: ListView.view.width
            implicitHeight: card.implicitHeight
            HighlightCard {
                id: card
                highlight: row.modelData
                anchors { left: parent.left; right: parent.right; leftMargin: Kirigami.Units.largeSpacing * 2; rightMargin: Kirigami.Units.largeSpacing * 2 }
                width: Math.min(parent.width - Kirigami.Units.largeSpacing * 4, Kirigami.Units.gridUnit * 50)
                onGoTo: page.openArticle(row.modelData.libraryId, row.modelData.path, "hl-" + row.modelData.id)
                onOpenArticle: page.openArticle(row.modelData.libraryId, row.modelData.path, "")
                onAddToProject: {
                    excerptPicker.highlightId = row.modelData.id;
                    excerptPicker.preview = row.modelData.text;
                    excerptPicker.open();
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "draw-highlight"
            text: search.text !== "" || page.color !== "" ? i18n("No notes match") : i18n("No notes yet")
            explanation: search.text !== "" || page.color !== "" ? ""
                       : i18n("While reading, select a passage, right-click it and choose Highlight. Your highlights and the notes you write on them are gathered here, by article.")
        }
    }

    ProjectPicker {
        id: excerptPicker
        property var highlightId: 0
        title: i18n("Save Highlight to Project")
        withNote: false
        onChosen: (projectId, projectTitle) => {
            if (Notes.addToProject(projectId, highlightId))
                applicationWindow().showPassiveNotification(i18n("Saved to “%1”", projectTitle));
        }
    }

    ProjectPicker {
        id: projectPicker
        title: i18n("Notes to a Project Document")
        withNote: false
        onChosen: (projectId, projectTitle) => {
            const documentId = Notes.toProject(projectId, i18n("Notes"), page.shownIds);
            if (documentId > 0) {
                DocumentWindows.open(documentId);
                applicationWindow().showPassiveNotification(i18n("A document of your notes is in “%1”", projectTitle), "long",
                                                            i18n("Open Project"), () => page.openProject(projectId));
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: i18n("Export Notes")
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("Markdown (*.md)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/notes.md"
        onAccepted: {
            const error = Notes.exportTo(selectedFile, page.shownIds);
            applicationWindow().showPassiveNotification(error !== "" ? error : i18n("Exported to %1", selectedFile.toString().replace("file://", "")),
                                                        error !== "" ? "long" : "short");
        }
    }
}
