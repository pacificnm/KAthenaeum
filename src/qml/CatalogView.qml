// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

ColumnLayout {
    id: view

    signal showDownloads()

    readonly property var catalog: LibraryManager.catalog
    spacing: 0

    QQC2.ToolBar {
        Layout.fillWidth: true
        position: QQC2.ToolBar.Header
        contentItem: Flow {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                width: Kirigami.Units.gridUnit * 14
                placeholderText: i18n("Search the catalog…")
                // the filters live in the catalog model, which outlives this page: show what it filters on
                Component.onCompleted: text = view.catalog.query
                onTextChanged: view.catalog.query = text
            }
            QQC2.ComboBox {
                id: kind
                textRole: "label"
                valueRole: "value"
                // every kind first, then the catalog's own categories, most common first
                model: [{ value: "", label: i18n("All kinds"), count: view.catalog.totalCount }].concat(view.catalog.categories)
                displayText: currentIndex < 0 ? "" : Util.plain(model[currentIndex].label)
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    text: i18n("%1 (%2)", Util.plain(modelData.label), modelData.count)
                    highlighted: kind.highlightedIndex === index
                }
                // computed from the list itself: indexOfValue() is not re-run when the categories arrive
                currentIndex: Math.max(0, model.findIndex(m => m.value === view.catalog.category))
                onActivated: view.catalog.category = currentValue
            }
            QQC2.ComboBox {
                textRole: "text"
                valueRole: "value"
                model: [
                    { value: "", text: i18n("Any size") },
                    { value: "mini", text: Util.flavourLabel("mini") },
                    { value: "nopic", text: Util.flavourLabel("nopic") },
                    { value: "maxi", text: Util.flavourLabel("maxi") }
                ]
                currentIndex: Math.max(0, model.findIndex(m => m.value === view.catalog.flavour))
                onActivated: view.catalog.flavour = currentValue
            }
            QQC2.ComboBox {
                textRole: "text"
                valueRole: "value"
                model: [
                    { value: "title", text: i18n("By title") },
                    { value: "newest", text: i18n("Newest first") },
                    { value: "smallest", text: i18n("Smallest first") },
                    { value: "largest", text: i18n("Largest first") }
                ]
                currentIndex: Math.max(0, model.findIndex(m => m.value === view.catalog.sort))
                onActivated: view.catalog.sort = currentValue
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        type: Kirigami.MessageType.Error
        visible: !LibraryManager.aria2Available
        text: i18n("Downloading needs aria2, which is not installed. Install it with: sudo apt install aria2")
    }
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        type: Kirigami.MessageType.Warning
        visible: LibraryManager.catalogError !== ""
        text: view.catalog.totalCount > 0
              ? i18n("Could not reach the Kiwix catalog (%1). Showing the copy from %2.", Util.plain(LibraryManager.catalogError),
                     LibraryManager.catalogFetchedAt.toLocaleString(Qt.locale(), Locale.ShortFormat))
              : i18n("Could not reach the Kiwix catalog: %1", Util.plain(LibraryManager.catalogError))
        actions: Kirigami.Action {
            text: i18n("Try Again")
            icon.name: "view-refresh"
            onTriggered: LibraryManager.refreshCatalog()
        }
    }
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        type: Kirigami.MessageType.Information
        visible: view.catalog.category !== "wikipedia" && view.catalog.totalCount > 0
        text: i18n("KAthenaeum searches and answers from Wikipedia-style libraries. Other kinds can be downloaded, but may not be searchable.")
    }

    QQC2.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: list
            model: view.catalog
            clip: true
            reuseItems: true

            delegate: QQC2.ItemDelegate {
                id: row
                required property var model
                required property int index
                width: ListView.view.width
                down: false

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    LibraryIcon {
                        source: row.model.illustration
                        Layout.alignment: Qt.AlignTop
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Heading {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                level: 4
                                text: row.model.title
                                elide: Text.ElideRight
                            }
                            Kirigami.Icon {
                                visible: !row.model.hasFulltext
                                source: "data-warning"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                HoverHandler { id: warnHover }
                                QQC2.ToolTip.visible: warnHover.hovered
                                QQC2.ToolTip.text: i18n("No full-text index: articles can only be found by title, and answers will be poor.")
                            }
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: row.model.summary
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            visible: text !== ""
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            opacity: 0.7
                            font: Kirigami.Theme.smallFont
                            wrapMode: Text.Wrap
                            text: Util.metaLine(row.model.flavour, row.model.size, row.model.articleCount, row.model.date)
                        }
                    }

                    // what can be done with it depends on what is already here
                    AppButton {
                        kind: "secondary"
                        Layout.alignment: Qt.AlignTop
                        visible: row.model.state === "available" || row.model.state === "update"
                        enabled: LibraryManager.aria2Available
                        text: row.model.state === "update" ? i18n("Update") : i18n("Download")
                        icon.name: "download"
                        onClicked: {
                            LibraryManager.download(row.index);
                            view.showDownloads();
                        }
                    }
                    QQC2.ProgressBar {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                        visible: row.model.state === "downloading"
                        value: row.model.progress
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.alignment: Qt.AlignTop
                        visible: row.model.state === "installed"
                        text: i18n("Installed")
                        opacity: 0.7
                    }
                }
            }

            QQC2.BusyIndicator {
                anchors.centerIn: parent
                running: LibraryManager.catalogLoading && list.count === 0
                visible: running
            }
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: list.count === 0 && !LibraryManager.catalogLoading && view.catalog.totalCount > 0
                icon.name: "search"
                text: i18n("Nothing matches")
                explanation: i18n("Try other words, or another kind or size.")
            }
        }
    }

    QQC2.Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        visible: view.catalog.totalCount > 0
        text: i18n("%1 of %2 libraries · catalog from %3", list.count, view.catalog.totalCount,
                   LibraryManager.catalogFetchedAt.toLocaleString(Qt.locale(), Locale.ShortFormat))
    }
}
