// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// Search every installed library (or one): titles as you type, then articles found through the ZIMs' own word indexes
Kirigami.ScrollablePage {
    id: page

    property string initialQuery
    property var libraryId: -1
    property var suggestions: []
    property bool submitted: false

    signal openArticle(var libraryId, string path)

    title: i18n("Search")

    function submit() {
        submitted = true;
        Search.search(field.text, scope.currentValue);
    }

    Component.onCompleted: {
        field.text = initialQuery;
        if (initialQuery !== "")
            submit();
        field.forceActiveFocus();
    }

    header: QQC2.ToolBar {
        contentItem: RowLayout {
            Kirigami.SearchField {
                id: field
                Layout.fillWidth: true
                placeholderText: i18n("Search your libraries…")
                autoAccept: false
                onTextChanged: {
                    page.submitted = false;
                    page.suggestions = Search.suggest(text, scope.currentValue);
                }
                onAccepted: page.submit()
            }
            QQC2.ComboBox {
                id: scope
                textRole: "title"
                valueRole: "libraryId"
                model: [{ libraryId: -1, title: i18n("All libraries") }].concat(LibraryManager.libraryList())
                currentIndex: Math.max(0, model.findIndex(m => m.libraryId === page.libraryId))
                onActivated: if (page.submitted) page.submit()
            }
        }
    }

    ListView {
        id: list
        model: page.submitted ? Search.results : page.suggestions

        header: QQC2.ItemDelegate {
            width: ListView.view.width
            visible: !page.submitted && field.text.trim() !== ""
            height: visible ? implicitHeight : 0
            icon.name: "search"
            text: i18n("Search the text of articles for “%1”", field.text.trim())
            onClicked: page.submit()
        }

        delegate: QQC2.ItemDelegate {
            id: row
            required property var modelData
            width: ListView.view.width
            highlighted: false
            onClicked: page.openArticle(modelData.libraryId, modelData.path)

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading {
                        level: 4
                        text: row.modelData.title
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: row.modelData.libraryTitle
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        visible: scope.currentValue === -1
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: page.submitted && (row.modelData.section ?? "") !== ""
                    text: row.modelData.section ?? ""
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: page.submitted
                    text: row.modelData.snippet ?? ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
        }

        footer: QQC2.Label {
            width: ListView.view.width
            visible: page.submitted && !Search.running && list.count > 0
            padding: Kirigami.Units.largeSpacing
            opacity: 0.6
            font: Kirigami.Theme.smallFont
            text: i18np("%1 article · %2 ms", "%1 articles · %2 ms", list.count, Search.elapsedMs)
        }

        QQC2.BusyIndicator {
            anchors.centerIn: parent
            running: Search.running
            visible: running
        }
        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: !Search.running && list.count === 0
            icon.name: "search"
            text: page.submitted ? i18n("Nothing found") : (field.text === "" ? i18n("Search your libraries") : i18n("No article titles start like that"))
            explanation: page.submitted ? i18n("Try other words. Search finds articles by their words, so a question worded differently from the article can miss.")
                       : field.text === "" ? i18n("Type a title or some words, then press Enter to search the text of every article.") : ""
        }
    }
}
