// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

// Choosing a region to download: Geofabrik's list (continents, then countries, then states and smaller), searchable
Kirigami.ScrollablePage {
    id: page

    title: i18n("Add a Map Region")

    property string parentId: ""  // whose parts are listed ("" for the continents)
    property var trail: []  // the regions above, for going back up: [{id, name}]
    readonly property var regions: MapManager.catalogLoaded ? (search.text.trim() !== "" ? MapManager.searchCatalog(search.text) : MapManager.catalogChildren(parentId)) : []

    Component.onCompleted: MapManager.loadCatalog()

    function enter(region) {
        trail = trail.concat([{ id: region.id, name: region.name }]);
        parentId = region.id;
        search.text = "";
    }
    function up(index) {  // to trail[index - 1], or the continents for -1
        parentId = index < 0 ? "" : trail[index].id;
        trail = index < 0 ? [] : trail.slice(0, index + 1);
    }

    header: ColumnLayout {
        spacing: 0
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !MapManager.hasWorld
            type: Kirigami.MessageType.Information
            text: i18n("Start with the world overview: every country's coasts, cities and main roads, for about 45 MB. Regions add their streets and places on top of it.")
            actions: Kirigami.Action {
                text: i18n("Download the World Overview")
                icon.name: "folder-download"
                onTriggered: MapManager.downloadWorld()
            }
        }
        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                Kirigami.SearchField {
                    id: search
                    Layout.fillWidth: true
                    placeholderText: i18n("Find a country or region…")
                    autoAccept: false
                }
            }
        }
        // where in the list we are: Continents > North America > United States
        Flow {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: search.text.trim() === "" && page.trail.length > 0
            spacing: Kirigami.Units.smallSpacing
            QQC2.ToolButton {
                text: i18n("All continents")
                icon.name: "go-home"
                onClicked: page.up(-1)
            }
            Repeater {
                model: page.trail
                delegate: QQC2.ToolButton {
                    required property var modelData
                    required property int index
                    text: "› " + Util.plain(modelData.name)
                    enabled: index < page.trail.length - 1
                    onClicked: page.up(index)
                }
            }
        }
    }

    ListView {
        id: list
        model: page.regions
        delegate: QQC2.ItemDelegate {
            id: row
            required property var modelData
            width: ListView.view.width
            down: false
            onClicked: modelData.hasChildren ? page.enter(modelData) : sheet.show(modelData)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: row.modelData.name
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                        visible: text !== ""
                        text: row.modelData.status === "complete" ? i18n("Downloaded")
                            : row.modelData.status !== "" && row.modelData.status !== "failed" && row.modelData.status !== "cancelled" ? i18n("Downloading")
                            : search.text.trim() !== "" ? row.modelData.parentName : ""
                    }
                }
                QQC2.Button {
                    visible: !row.modelData.continent
                    text: i18n("Download…")
                    icon.name: "folder-download"
                    onClicked: sheet.show(row.modelData)
                }
                QQC2.ToolButton {
                    visible: row.modelData.hasChildren
                    icon.name: "go-next"
                    display: QQC2.AbstractButton.IconOnly
                    text: i18n("Show Its Parts")
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: page.enter(row.modelData)
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: MapManager.catalogError !== "" ? "network-disconnect" : "map-flat"
            text: MapManager.catalogLoading ? i18n("Loading the list of regions…")
                : MapManager.catalogError !== "" ? i18n("Could not load the list of regions")
                : i18n("No region by that name")
            explanation: MapManager.catalogError
            helpfulAction: Kirigami.Action {
                enabled: MapManager.catalogError !== ""
                text: i18n("Try Again")
                icon.name: "view-refresh"
                onTriggered: MapManager.loadCatalog()
            }
        }
    }

    // a region chosen: how much detail, how big that is, and Download
    Kirigami.Dialog {
        id: sheet

        property var region: ({})
        property int zoom: 15
        property bool measuring: false
        property double mapBytes: 0
        property double osmBytes: 0
        property string error: ""

        function show(region) {
            sheet.region = region;
            sheet.zoom = region.streetTiles > 2500000 ? 13 : 15;  // big countries start with less detail
            sheet.measure();
            sheet.open();
        }
        function measure() {
            measuring = true;
            mapBytes = 0;
            osmBytes = 0;
            error = "";
            MapManager.estimate(region.id, zoom);
        }

        title: Util.plain(region.name ?? "")
        preferredWidth: Kirigami.Units.gridUnit * 26
        padding: Kirigami.Units.largeSpacing
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: sheet.region.status === "complete" ? i18n("Download Again") : i18n("Download")
                icon.name: "folder-download"
                enabled: !sheet.measuring && sheet.error === ""
                onTriggered: {
                    MapManager.download(sheet.region.id, sheet.zoom);
                    sheet.close();
                    applicationWindow().showPassiveNotification(i18n("Downloading %1. Settings shows its progress.", Util.plain(sheet.region.name)));
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: sheet.close() }
        ]

        Connections {
            target: MapManager
            function onEstimated(id, maxZoom, mapBytes, tiles, osmBytes, error) {
                if (id !== sheet.region.id || maxZoom !== sheet.zoom)
                    return;
                sheet.measuring = false;
                sheet.mapBytes = mapBytes;
                sheet.osmBytes = osmBytes;
                sheet.error = error;
            }
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            QQC2.RadioButton {
                text: i18n("Streets and buildings")
                checked: sheet.zoom === 15
                onClicked: { sheet.zoom = 15; sheet.measure(); }
            }
            QQC2.RadioButton {
                text: i18n("Roads, towns and parks (about a fifth of the size)")
                checked: sheet.zoom === 13
                onClicked: { sheet.zoom = 13; sheet.measure(); }
            }
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: sheet.measuring ? i18n("Measuring…")
                    : sheet.error !== "" ? sheet.error
                    : sheet.osmBytes > 0 ? i18n("The map takes %1. To find which places have Wikipedia articles, KAthenaeum also downloads the region's OpenStreetMap data (%2), reads it and deletes it.",
                                                KCoreAddons.Format.formatByteSize(sheet.mapBytes, 1), KCoreAddons.Format.formatByteSize(sheet.osmBytes, 1))
                    : i18n("The map takes %1.", KCoreAddons.Format.formatByteSize(sheet.mapBytes, 1))
                color: sheet.error !== "" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
            }
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: i18n("%1 free in %2", KCoreAddons.Format.formatByteSize(MapManager.freeSpace, 1), MapManager.folder)
            }
        }
    }
}
