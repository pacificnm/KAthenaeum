// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import org.kde.kirigami as Kirigami
import KAthenaeum

// A timeline: the dated sentences of an article, or of a project's articles, in date order on a line, under the period
// they belong to. Each event is the article's own sentence, with a link back to it.
Kirigami.ScrollablePage {
    id: page

    // one article, or a project
    property var libraryId: 0
    property string path
    property string articleTitle
    property var projectId: 0

    signal openArticle(var libraryId, string path, string fragment)

    title: i18n("Timeline: %1", Util.pageTitle(timeline.title))

    TimelineModel {
        id: timeline
    }
    Component.onCompleted: page.projectId > 0 ? timeline.loadProject(page.projectId) : timeline.loadArticle(page.libraryId, page.path, page.articleTitle)

    actions: [
        Kirigami.Action {
            text: page.projectId > 0 ? i18n("Add to This Project") : i18n("Add to Project…")
            icon.name: "bookmark-new"
            enabled: timeline.events.length > 0
            tooltip: i18n("The events shown, as a document of a research project")
            onTriggered: {
                if (page.projectId > 0) {
                    if (timeline.toProject(page.projectId) > 0)
                        applicationWindow().showPassiveNotification(i18n("The timeline is a document of the project now"));
                } else {
                    picker.open();
                }
            }
        },
        Kirigami.Action {
            text: i18n("Export…")
            icon.name: "document-export"
            enabled: timeline.events.length > 0
            tooltip: i18n("Save the events shown as a Markdown file")
            onTriggered: exportDialog.open()
        }
    ]

    header: QQC2.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Kirigami.SearchField {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                placeholderText: i18n("Find events…")
                onTextChanged: timeline.query = text
            }
            QQC2.ComboBox {
                visible: timeline.articles.length > 1
                textRole: "label"
                valueRole: "key"
                model: [{ key: "", label: i18n("All articles (%1)", timeline.total) }].concat(timeline.articles.map(a => ({ key: a.key, label: Util.plain(a.title) + " (" + a.count + ")" })))
                onActivated: timeline.article = currentValue
            }
            Item { Layout.fillWidth: true }
            QQC2.Label {
                textFormat: Text.PlainText
                visible: !timeline.loading
                text: timeline.events.length === timeline.total ? i18np("%1 event", "%1 events", timeline.total)
                                                                : i18n("%1 of %2 events", timeline.events.length, timeline.total)
                opacity: 0.7
            }
        }
    }

    ListView {
        id: list
        model: timeline.events
        topMargin: Kirigami.Units.largeSpacing
        bottomMargin: Kirigami.Units.gridUnit
        readonly property int dateWidth: Kirigami.Units.gridUnit * 9
        readonly property int axisWidth: Kirigami.Units.gridUnit * 2

        // the period, on the line
        section.property: "period"
        section.criteria: ViewSection.FullString
        section.delegate: Item {
            required property string section
            width: ListView.view.width
            height: periodLabel.implicitHeight + Kirigami.Units.largeSpacing * 3
            Rectangle {  // the line runs through
                x: list.dateWidth + list.axisWidth / 2 - 1
                width: 2
                height: parent.height
                color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
            }
            Rectangle {
                id: pill
                x: list.dateWidth + list.axisWidth / 2 - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: periodLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                height: periodLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: height / 2
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.25)
                QQC2.Label {
                    id: periodLabel
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: parent.parent.section
                    font.weight: Font.DemiBold
                }
            }
        }

        delegate: Item {
            id: row
            required property var modelData
            width: ListView.view.width
            height: card.implicitHeight + Kirigami.Units.largeSpacing

            // the date, before the line
            QQC2.Label {
                width: list.dateWidth - Kirigami.Units.largeSpacing
                y: Kirigami.Units.largeSpacing
                horizontalAlignment: Text.AlignRight
                textFormat: Text.PlainText
                text: row.modelData.label
                wrapMode: Text.Wrap
                font.weight: Font.DemiBold
                font.italic: row.modelData.approximate
            }
            // the line, and a dot on it
            Rectangle {
                x: list.dateWidth + list.axisWidth / 2 - 1
                width: 2
                height: parent.height
                color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
            }
            Rectangle {
                x: list.dateWidth + list.axisWidth / 2 - width / 2
                y: Kirigami.Units.largeSpacing + 3
                width: Kirigami.Units.gridUnit * 0.6
                height: width
                radius: width / 2
                color: row.modelData.approximate ? Kirigami.Theme.backgroundColor : Kirigami.Theme.highlightColor
                border.width: 2
                border.color: Kirigami.Theme.highlightColor
            }
            // the event: the article's own sentence, and where it is from
            QQC2.ItemDelegate {
                id: card
                x: list.dateWidth + list.axisWidth
                width: Math.min(parent.width - x - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 45)
                onClicked: page.openArticle(row.modelData.libraryId, row.modelData.path, row.modelData.section.replace(/ /g, "_"))
                background: Rectangle {
                    radius: Kirigami.Units.cornerRadius * 1.5
                    color: card.hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.08) : "transparent"
                }
                contentItem: ColumnLayout {
                    spacing: 2
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: row.modelData.text
                        wrapMode: Text.Wrap
                        font.family: "Noto Serif"
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: row.modelData.section !== "" ? row.modelData.articleTitle + " · " + row.modelData.section : row.modelData.articleTitle
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont
                        opacity: 0.6
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: !timeline.loading && list.count === 0
            icon.name: "view-calendar-timeline"
            text: timeline.total === 0 ? i18n("No dates found") : i18n("No events match")
            explanation: timeline.total === 0 ? (page.projectId > 0 ? i18n("The project's articles give no dates. Add articles about events, people or places.")
                                                                  : i18n("This article gives no dates in its text."))
                                              : ""
        }
        QQC2.BusyIndicator {
            anchors.centerIn: parent
            running: timeline.loading
            visible: running
        }
    }

    ProjectPicker {
        id: picker
        title: i18n("Add the Timeline to a Project")
        withNote: false
        onChosen: (projectId, projectTitle) => {
            if (timeline.toProject(projectId) > 0)
                applicationWindow().showPassiveNotification(i18n("The timeline is a document of “%1” now", Util.plain(projectTitle)));
        }
    }

    FileDialog {
        id: exportDialog
        title: i18n("Export Timeline")
        fileMode: FileDialog.SaveFile
        nameFilters: [i18n("Markdown (*.md)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        selectedFile: currentFolder + "/" + timeline.exportName()
        onAccepted: {
            const error = timeline.exportTo(selectedFile);
            applicationWindow().showPassiveNotification(error !== "" ? Util.plain(error) : i18n("Exported to %1", Util.plain(Util.localPath(selectedFile))),
                                                        error !== "" ? "long" : "short");
        }
    }
}
