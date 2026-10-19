// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A library's home: what it is, find an article, A to Z, random, recently read, and Wikipedia's own overview pages
Kirigami.ScrollablePage {
    id: page

    required property var libraryId
    readonly property var info: LibraryBrowser.libraryInfo(libraryId)
    readonly property var overviews: LibraryBrowser.overviews(libraryId)
    property var recent: []
    property var suggestions: []

    signal openArticle(var libraryId, string path)
    signal openTitles(var libraryId, string label, string key, bool isPrefix)

    title: info.title ?? ""

    actions: [
        Kirigami.Action {
            text: i18n("A to Z")
            icon.name: "view-sort-ascending-name"
            onTriggered: page.openTitles(page.libraryId, i18n("%1: A to Z", page.info.title), "A", false)
        },
        Kirigami.Action {
            text: i18n("Random Article")
            icon.name: "roll"
            onTriggered: {
                const a = LibraryBrowser.randomArticle(page.libraryId);
                if (a.path)
                    page.openArticle(page.libraryId, a.path);
            }
        }
    ]

    // coming back from an article: show it under "Recently read"
    onIsCurrentPageChanged: if (isCurrentPage) recent = LibraryBrowser.recent(libraryId)
    Component.onCompleted: recent = LibraryBrowser.recent(libraryId)

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            LibraryIcon {
                source: page.info.icon ?? ""
                Layout.alignment: Qt.AlignTop
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading {
                    Layout.fillWidth: true
                    text: page.info.title ?? ""
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: page.info.description ?? ""
                    wrapMode: Text.Wrap
                    visible: text !== ""
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                    text: Util.metaLine(page.info.flavour, page.info.size, page.info.articleCount, page.info.date)
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: page.info.missing === true
            text: i18n("The file of this library is missing. Find it again from the Library Manager.")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: page.info.missing === false && page.info.hasFulltext === false
            text: i18n("This library has no full-text index, so articles can only be found by title.")
        }

        Kirigami.SearchField {
            id: search
            Layout.fillWidth: true
            placeholderText: i18n("Find an article by title…")
            onTextChanged: page.suggestions = LibraryBrowser.suggest(page.libraryId, text)
            onAccepted: if (page.suggestions.length > 0) page.openArticle(page.libraryId, page.suggestions[0].path)
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            visible: search.text !== ""
            Repeater {
                model: page.suggestions
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.title
                    icon.name: "document-preview"
                    onClicked: page.openArticle(page.libraryId, modelData.path)
                }
            }
            QQC2.Label {
                visible: page.suggestions.length === 0
                text: i18n("No article titles start like that.")
                opacity: 0.7
            }
        }

        Kirigami.Heading {
            level: 3
            text: i18n("Recently read")
            visible: page.recent.length > 0
        }
        Flow {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: page.recent.length > 0
            Repeater {
                model: page.recent
                delegate: Kirigami.Chip {
                    required property var modelData
                    text: modelData.title
                    closable: false
                    checkable: false
                    onClicked: page.openArticle(page.libraryId, modelData.path)
                }
            }
        }

        Repeater {
            model: page.overviews
            delegate: ColumnLayout {
                id: overview
                required property var modelData
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    level: 3
                    text: overview.modelData.label
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Repeater {
                        model: overview.modelData.entries
                        delegate: Kirigami.Chip {
                            required property var modelData
                            text: modelData.title
                            closable: false
                            checkable: false
                            onClicked: page.openArticle(page.libraryId, modelData.path)
                        }
                    }
                    QQC2.Button {
                        visible: overview.modelData.more
                        text: i18n("All…")
                        flat: true
                        onClicked: page.openTitles(page.libraryId, overview.modelData.label, overview.modelData.prefix, true)
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit
            visible: page.overviews.length === 0 && page.recent.length === 0 && search.text === ""
            icon.name: "view-sort-ascending-name"
            text: i18n("Start with A to Z, a random article, or a title above")
        }
    }
}
