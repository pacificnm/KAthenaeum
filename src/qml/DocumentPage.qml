// SPDX-License-Identifier: GPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A project's Markdown document: write, cite the project's sources, preview, export. Saved as you type.
Kirigami.Page {
    id: page

    property alias documentId: doc.documentId
    property alias document: doc
    property string mode: width > Kirigami.Units.gridUnit * 50 ? "split" : "edit"  // edit | split | preview

    signal openUrl(url url)

    title: doc.title
    padding: 0

    actions: [
        Kirigami.Action {
            text: i18n("Cite")
            icon.name: "insert-link"
            tooltip: i18n("Insert a link to one of the project's articles, or quote a saved excerpt")
            enabled: project.articles.length + project.excerpts.length > 0 && page.mode !== "preview"
            onTriggered: citeMenu.popup()
        },
        Kirigami.Action {
            text: i18n("Edit")
            icon.name: "document-edit"
            checkable: true
            checked: page.mode === "edit"
            displayHint: Kirigami.DisplayHint.KeepVisible
            onTriggered: page.mode = "edit"
        },
        Kirigami.Action {
            text: i18n("Split")
            icon.name: "view-split-left-right"
            checkable: true
            checked: page.mode === "split"
            onTriggered: page.mode = "split"
        },
        Kirigami.Action {
            text: i18n("Preview")
            icon.name: "document-preview"
            checkable: true
            checked: page.mode === "preview"
            onTriggered: page.mode = "preview"
        },
        Kirigami.Action {
            text: i18n("Export…")
            icon.name: "document-export"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: exportDialog.open()
        }
    ]

    function insert(text) {
        const at = editor.cursorPosition;
        // a quotation is a block of its own
        const block = text.startsWith(">") ? (at > 0 && doc.content[at - 1] !== "\n" ? "\n\n" : "") + text + "\n\n" : text;
        editor.insert(at, block);
        editor.forceActiveFocus();
    }

    ProjectDocument {
        id: doc
    }
    Project {
        id: project
        projectId: doc.projectId
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                QQC2.TextField {
                    id: titleField
                    Layout.fillWidth: true
                    text: doc.title
                    placeholderText: i18n("Title")
                    font.bold: true
                    onTextEdited: doc.title = text
                }
                QQC2.Label {
                    text: doc.error !== "" ? doc.error : doc.saved ? i18n("Saved") : i18n("Saving…")
                    color: doc.error !== "" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                visible: page.mode !== "preview"
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                QQC2.TextArea {
                    id: editor
                    text: doc.content
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    placeholderText: i18n("Write in Markdown: # Heading, **bold**, - lists, > quotations. Cite inserts links to the project's sources.")
                    onTextChanged: if (text !== doc.content) doc.content = text
                    Component.onCompleted: forceActiveFocus()
                }
            }
            Kirigami.Separator {
                Layout.fillHeight: true
                visible: page.mode === "split"
            }
            QQC2.ScrollView {
                id: previewView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                visible: page.mode !== "edit"
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
                background: Rectangle { color: Kirigami.Theme.backgroundColor }
                TextEdit {
                    id: preview
                    width: previewView.availableWidth
                    padding: Kirigami.Units.largeSpacing * 2
                    text: "# " + doc.title + "\n\n" + doc.content
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    color: Kirigami.Theme.textColor
                    selectedTextColor: Kirigami.Theme.highlightedTextColor
                    selectionColor: Kirigami.Theme.highlightColor
                    font: Kirigami.Theme.defaultFont
                    onLinkActivated: link => {
                        if (link.startsWith("zim:"))
                            page.openUrl(link);
                        else if (/^(https?|mailto):/i.test(link))
                            Qt.openUrlExternally(link);
                    }
                    HoverHandler {
                        cursorShape: preview.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                    }
                }
            }
        }
    }

    QQC2.Menu {
        id: citeMenu
        Instantiator {
            model: project.articles
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.note !== "" ? i18n("%1 (has a note)", modelData.title) : modelData.title
                icon.name: "text-html"
                onTriggered: page.insert(project.articleLink(modelData.itemId))
            }
            onObjectAdded: (index, object) => citeMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => citeMenu.removeItem(object)
        }
        Instantiator {
            model: project.excerpts
            delegate: QQC2.MenuItem {
                required property var modelData
                text: "“" + (modelData.text.length > 60 ? modelData.text.slice(0, 60) + "…" : modelData.text) + "”"
                icon.name: "format-text-blockquote"
                onTriggered: page.insert(project.quote(modelData.itemId))
            }
            onObjectAdded: (index, object) => citeMenu.addItem(object)
            onObjectRemoved: (index, object) => citeMenu.removeItem(object)
        }
    }

    FileDialog {
        id: exportDialog
        title: i18n("Export Document")
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("Markdown (*.md)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + doc.exportName()
        onAccepted: {
            doc.save();
            const error = doc.exportTo(selectedFile);
            applicationWindow().showPassiveNotification(error !== "" ? error : i18n("Exported to %1", selectedFile.toString().replace("file://", "")),
                                                        error !== "" ? "long" : "short");
        }
    }
}
