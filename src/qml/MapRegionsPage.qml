// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

// Choosing a region to download: Geofabrik's list (continents, countries, states and smaller) as cards showing each
// region's shape, searchable; a region opens a panel with its two detail levels, both measured
Kirigami.ScrollablePage {
    id: page

    title: i18n("Add a Map Region")

    property string parentId: ""  // whose parts are shown ("" for the continents)
    property var trail: []  // the regions above, for going back up: [{id, name}]
    readonly property bool searching: search.text.trim() !== ""
    readonly property var regions: MapManager.catalogLoaded ? (searching ? MapManager.searchCatalog(search.text) : MapManager.catalogChildren(parentId)) : []

    Component.onCompleted: MapManager.loadCatalog()

    function enter(region) {
        trail = trail.concat([{ id: region.id, name: region.name }]);
        parentId = region.id;
        search.text = "";
        grid.positionViewAtBeginning();
    }
    function up(index) {  // to trail[index], or the continents for -1
        parentId = index < 0 ? "" : trail[index].id;
        trail = index < 0 ? [] : trail.slice(0, index + 1);
    }
    function openSheetFor(id) {
        sheet.show(MapManager.catalogRegion(id));
    }
    function statusText(status) {
        return status === "complete" ? i18n("Downloaded")
            : status === "queued" || status === "downloading" || status === "indexing" ? i18n("Downloading")
            : "";
    }

    header: QQC2.ToolBar {
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.SearchField {
                id: search
                Layout.fillWidth: true
                placeholderText: i18n("Find a country, state or region…")
                autoAccept: false
            }
            // where we are: World › North America › United States
            RowLayout {
                visible: !page.searching
                spacing: 0
                QQC2.ToolButton {
                    text: i18n("World")
                    icon.name: "globe"
                    checked: page.trail.length === 0
                    onClicked: page.up(-1)
                }
                Repeater {
                    model: page.trail
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        spacing: 0
                        Kirigami.Icon {
                            source: "go-next-symbolic"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.6
                        }
                        QQC2.ToolButton {
                            text: Util.plain(modelData.name)
                            checked: index === page.trail.length - 1
                            onClicked: page.up(index)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            QQC2.Label {
                visible: page.searching
                textFormat: Text.PlainText
                text: i18np("%1 region found", "%1 regions found", page.regions.length)
                opacity: 0.7
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
            }
        }
    }

    GridView {
        id: grid

        readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 11)))
        cellWidth: Math.floor(width / columns)
        cellHeight: Kirigami.Units.gridUnit * 11
        model: page.regions
        topMargin: Kirigami.Units.largeSpacing
        bottomMargin: Kirigami.Units.largeSpacing

        // first: the world overview, until it is downloaded
        header: Item {
            width: grid.width
            height: visible ? worldCard.height + Kirigami.Units.largeSpacing * 2 : 0
            visible: !MapManager.hasWorld && !page.searching && page.trail.length === 0
            Kirigami.ShadowedRectangle {
                id: worldCard
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Kirigami.Units.largeSpacing }
                height: worldRow.implicitHeight + Kirigami.Units.largeSpacing * 3
                radius: Kirigami.Units.cornerRadius * 2
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0.12)
                border.color: Qt.alpha(Kirigami.Theme.highlightColor, 0.4)
                border.width: 1
                RowLayout {
                    id: worldRow
                    anchors { fill: parent; margins: Kirigami.Units.largeSpacing * 1.5 }
                    spacing: Kirigami.Units.largeSpacing * 2
                    Kirigami.Icon {
                        source: "globe"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                        Layout.preferredHeight: Kirigami.Units.iconSizes.huge
                        color: Kirigami.Theme.highlightColor
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading {
                            level: 2
                            textFormat: Text.PlainText
                            text: i18n("Start with the world")
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: i18n("Every country's coasts, borders, cities and main roads, about 45 MB. The regions you add draw their streets and places on top of it.")
                        }
                    }
                    AppButton {
                        kind: "primary"
                        text: i18n("Download")
                        icon.name: "folder-download"
                        onClicked: MapManager.downloadWorld()
                    }
                }
            }
        }

        delegate: Item {
            id: cell
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight

            Kirigami.ShadowedRectangle {
                id: card
                anchors { fill: parent; margins: Kirigami.Units.largeSpacing / 2 + Kirigami.Units.smallSpacing }
                radius: Kirigami.Units.cornerRadius * 2
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: hover.hovered ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.15)
                shadow.size: hover.hovered ? Kirigami.Units.largeSpacing * 1.5 : Kirigami.Units.smallSpacing
                shadow.color: Qt.rgba(0, 0, 0, hover.hovered ? 0.18 : 0.08)
                shadow.yOffset: hover.hovered ? 2 : 1
                Behavior on shadow.size { NumberAnimation { duration: Kirigami.Units.shortDuration } }

                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: cell.modelData.hasChildren ? page.enter(cell.modelData) : sheet.show(cell.modelData)
                }

                ColumnLayout {
                    anchors { fill: parent; margins: Kirigami.Units.largeSpacing }
                    spacing: Kirigami.Units.smallSpacing

                    // its shape, on a tinted panel
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Kirigami.Units.cornerRadius
                        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.06)
                        RegionSilhouette {
                            anchors { fill: parent; margins: Kirigami.Units.largeSpacing }
                            regionId: cell.modelData.id
                            color: cell.modelData.status === "complete" ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.highlightColor
                        }
                        // downloaded or downloading
                        Rectangle {
                            visible: page.statusText(cell.modelData.status) !== ""
                            anchors { top: parent.top; right: parent.right; margins: Kirigami.Units.smallSpacing }
                            radius: height / 2
                            height: badge.implicitHeight + Kirigami.Units.smallSpacing
                            width: badge.implicitWidth + Kirigami.Units.largeSpacing
                            color: cell.modelData.status === "complete" ? Kirigami.Theme.positiveBackgroundColor : Kirigami.Theme.neutralBackgroundColor
                            QQC2.Label {
                                id: badge
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: page.statusText(cell.modelData.status)
                                font: Kirigami.Theme.smallFont
                                color: cell.modelData.status === "complete" ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: cell.modelData.name
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                                elide: Text.ElideRight
                                text: cell.modelData.hasChildren ? i18np("%1 region", "%1 regions", cell.modelData.childCount)
                                    : page.searching ? cell.modelData.parentName
                                    : i18n("Ready to download")
                            }
                        }
                        // a country with parts can also be downloaded whole
                        QQC2.ToolButton {
                            visible: cell.modelData.hasChildren && !cell.modelData.continent
                            icon.name: "folder-download"
                            display: QQC2.AbstractButton.IconOnly
                            text: i18n("Download All of %1…", Util.plain(cell.modelData.name))
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: sheet.show(cell.modelData)
                        }
                        Kirigami.Icon {
                            visible: cell.modelData.hasChildren
                            source: "go-next-symbolic"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.6
                        }
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: grid.count === 0
            icon.name: MapManager.catalogError !== "" ? "network-disconnect" : "globe"
            text: MapManager.catalogLoading ? i18n("Loading the list of regions…")
                : MapManager.catalogError !== "" ? i18n("Could not load the list of regions")
                : i18n("No region by that name")
            explanation: Util.plain(MapManager.catalogError)
            helpfulAction: Kirigami.Action {
                enabled: MapManager.catalogError !== ""
                text: i18n("Try Again")
                icon.name: "view-refresh"
                onTriggered: MapManager.loadCatalog()
            }
        }
    }

    // a region chosen: its shape, both detail levels with their sizes, and Download
    Kirigami.Dialog {
        id: sheet

        property var region: ({})
        property int zoom: 15
        property var sizes: ({})  // zoom -> {measuring, mapBytes, osmBytes, error}

        function show(region) {
            sheet.region = region;
            sheet.sizes = { 15: { measuring: true }, 13: { measuring: true } };
            sheet.zoom = region.streetTiles > 2500000 ? 13 : 15;  // big countries start with less detail
            MapManager.estimate(region.id, 15);
            MapManager.estimate(region.id, 13);
            sheet.open();
        }
        function size(zoom) {
            return sheet.sizes[zoom] ?? {};
        }

        title: Util.plain(region.name ?? "")
        preferredWidth: Kirigami.Units.gridUnit * 34
        padding: Kirigami.Units.largeSpacing * 2
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: sheet.region.status === "complete" ? i18n("Download Again") : i18n("Download")
                icon.name: "folder-download"
                enabled: !(sheet.size(sheet.zoom).measuring ?? true) && (sheet.size(sheet.zoom).error ?? "") === ""
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
                if (id !== sheet.region.id)
                    return;
                const s = Object.assign({}, sheet.sizes);
                s[maxZoom] = { measuring: false, mapBytes: mapBytes, osmBytes: osmBytes, error: error };
                sheet.sizes = s;
                // too big with streets: offer the lighter one
                if (maxZoom === 15 && error !== "" && sheet.zoom === 15)
                    sheet.zoom = 13;
            }
        }

        RowLayout {
            spacing: Kirigami.Units.largeSpacing * 2

            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                Layout.preferredHeight: Kirigami.Units.gridUnit * 10
                Layout.alignment: Qt.AlignTop
                radius: Kirigami.Units.cornerRadius * 2
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0.06)
                RegionSilhouette {
                    anchors { fill: parent; margins: Kirigami.Units.largeSpacing }
                    regionId: sheet.region.id ?? ""
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    visible: (sheet.region.parentName ?? "") !== ""
                    textFormat: Text.PlainText
                    text: sheet.region.parentName ?? ""
                    opacity: 0.7
                }

                Repeater {
                    model: [
                        { zoom: 15, title: i18n("Streets and buildings"), detail: i18n("Every street, building and place, as close as you can zoom.") },
                        { zoom: 13, title: i18n("Roads and towns"), detail: i18n("Roads, towns, parks and lakes. About a fifth of the size.") }
                    ]
                    delegate: QQC2.ItemDelegate {
                        id: option
                        required property var modelData
                        readonly property var size: sheet.size(modelData.zoom)
                        readonly property bool chosen: sheet.zoom === modelData.zoom
                        Layout.fillWidth: true
                        enabled: (size.error ?? "") === ""
                        padding: Kirigami.Units.largeSpacing
                        onClicked: sheet.zoom = modelData.zoom
                        background: Rectangle {
                            radius: Kirigami.Units.cornerRadius * 1.5
                            color: option.chosen ? Qt.alpha(Kirigami.Theme.highlightColor, 0.12)
                                 : option.hovered ? Qt.alpha(Kirigami.Theme.textColor, 0.05) : "transparent"
                            border.width: option.chosen ? 2 : 1
                            border.color: option.chosen ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.2)
                        }
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.largeSpacing
                            QQC2.RadioButton {
                                checked: option.chosen
                                enabled: option.enabled
                                onClicked: sheet.zoom = option.modelData.zoom
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    text: option.modelData.title
                                    font.weight: Font.DemiBold
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.75
                                    text: (option.size.error ?? "") !== "" ? option.size.error : option.modelData.detail
                                    color: (option.size.error ?? "") !== "" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                                }
                            }
                            QQC2.BusyIndicator {
                                visible: option.size.measuring ?? false
                                running: visible
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                            }
                            QQC2.Label {
                                visible: !(option.size.measuring ?? true) && (option.size.error ?? "") === ""
                                textFormat: Text.PlainText
                                text: KCoreAddons.Format.formatByteSize(option.size.mapBytes ?? 0, 1)
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: (sheet.size(sheet.zoom).osmBytes ?? 0) > 0
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.75
                    text: i18n("To link places to their Wikipedia articles, KAthenaeum also fetches the region's OpenStreetMap data (%1), reads it once and deletes it.",
                               KCoreAddons.Format.formatByteSize(sheet.size(sheet.zoom).osmBytes ?? 0, 1))
                }
                // room on the disk, and what this takes of it
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, MapManager.freeSpace)
                        value: sheet.size(sheet.zoom).mapBytes ?? 0
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                        elide: Text.ElideMiddle
                        text: i18n("%1 free in %2", KCoreAddons.Format.formatByteSize(MapManager.freeSpace, 1), MapManager.folder)
                    }
                }
            }
        }
    }
}
