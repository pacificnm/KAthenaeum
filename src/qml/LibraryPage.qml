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
    // read again when the installed libraries change: a method call in a binding is never re-run, so a removed
    // library's home stayed as it was (the review, 2026-10-19)
    property var info: LibraryBrowser.libraryInfo(libraryId)
    property var overviews: LibraryBrowser.overviews(libraryId)
    readonly property bool available: info.available === true
    function reload() {
        info = LibraryBrowser.libraryInfo(libraryId);
        overviews = available ? LibraryBrowser.overviews(libraryId) : [];
    }
    Connections {
        target: LibraryManager.installed
        function onModelReset() { page.reload(); }
        function onRowsRemoved() { page.reload(); }
        function onDataChanged() { page.reload(); }
    }
    property var recent: []
    property var suggestions: []

    signal openArticle(var libraryId, string path)
    signal openTitles(var libraryId, string label, string key, bool isPrefix)
    signal openSearch(string query, var libraryId)

    title: info.title !== undefined ? Util.pageTitle(info.title) : i18n("Removed library")

    actions: [
        Kirigami.Action {
            text: i18n("A to Z")
            icon.name: "view-sort-ascending-name"
            enabled: page.available
            onTriggered: page.openTitles(page.libraryId, i18n("%1: A to Z", page.info.title), "A", false)
        },
        Kirigami.Action {
            text: i18n("Random Article")
            icon.name: "roll"
            enabled: page.available
            onTriggered: {
                const a = LibraryBrowser.randomArticle(page.libraryId);
                if (a.path)
                    page.openArticle(page.libraryId, a.path);
                else
                    applicationWindow().showPassiveNotification(i18n("This library has no articles to pick from."));
            }
        }
    ]

    // coming back from an article: show it under "Recently read"
    onIsCurrentPageChanged: if (isCurrentPage) recent = LibraryBrowser.recent(libraryId)
    Component.onCompleted: recent = LibraryBrowser.recent(libraryId)

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !page.available
            type: Kirigami.MessageType.Warning
            text: page.info.libraryId === undefined ? i18n("This library has been removed.")
                                                    : i18n("This library's file is missing. Add it again in the Library Manager.")
        }

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
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: page.info.title ?? ""
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: page.info.description ?? ""
                    wrapMode: Text.Wrap
                    visible: text !== ""
                }
                QQC2.Label {
                    textFormat: Text.PlainText
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
            enabled: page.available
            autoAccept: false  // Kirigami accepts by itself after a moment, which jumped to full-text search while typing
            placeholderText: i18n("Find an article…")
            onTextChanged: page.suggestions = LibraryBrowser.suggest(page.libraryId, text)
            onAccepted: page.openSearch(text, page.libraryId)
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            visible: search.text !== ""
            QQC2.ItemDelegate {
                Layout.fillWidth: true
                icon.name: "search"
                text: i18n("Search the text of this library for “%1”", search.text.trim())
                onClicked: page.openSearch(search.text, page.libraryId)
            }
            Repeater {
                model: page.suggestions
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    text: Util.plain(modelData.title)
                    icon.name: "document-preview"
                    onClicked: page.openArticle(page.libraryId, modelData.path)
                }
            }
            QQC2.Label {
                textFormat: Text.PlainText
                visible: page.suggestions.length === 0
                text: i18n("No article titles start like that.")
                opacity: 0.7
            }
        }

        Kirigami.Heading {
            textFormat: Text.PlainText
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
                    text: Util.plain(modelData.title)
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
                    textFormat: Text.PlainText
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
                            text: Util.plain(modelData.title)
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
