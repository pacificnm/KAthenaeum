// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// The map: OpenStreetMap drawn by Marble from KDE's server, cached as it is viewed, with any area kept for use offline.
// A click on a place shows what it is and opens its Wikipedia article from the installed libraries.
Kirigami.Page {
    id: page

    signal openArticle(var libraryId, string path)
    signal openSearch(string query)

    title: i18n("Map")
    padding: 0

    // the clicked place: {name, address, lat, lon, article}
    property var selected: ({})
    readonly property bool hasSelection: selected.name !== undefined
    // "Show on Map" from an article: {lat, lon, zoom, name, article}
    property var target: null
    readonly property var map: loader.item
    // the area shown, and how many tiles keeping it at street level takes
    property var area: null
    readonly property int areaTiles: area ? MapManager.tileCount(area.west, area.south, area.east, area.north) : 0

    function select(place) {
        page.selected = Object.assign({}, place, { article: MapManager.article(place.wikipedia ?? "", place.name) });
    }
    // a moment after the map stops moving: where it is (to open there again) and what is shown (to keep it)
    Timer {
        id: viewTimer
        interval: 500
        onTriggered: {
            if (!page.map)
                return;
            MapManager.saveView(page.map.latitude(), page.map.longitude(), page.map.zoom);
            page.area = page.map.bounds();
        }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Keep This Area Offline")
            icon.name: "folder-download"
            enabled: Connectivity.online && !MapManager.saving && page.area !== null && page.areaTiles <= MapManager.maxSaveTiles
            tooltip: !Connectivity.online ? i18n("Needs the internet")
                   : page.areaTiles > MapManager.maxSaveTiles ? i18n("Zoom in to a town or a city to keep it")
                   : i18n("Keeps the streets and places shown, so they can be seen without the internet")
            onTriggered: MapManager.saveArea(page.area.west, page.area.south, page.area.east, page.area.north)
        }
    ]

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("MarbleMap.qml")
        focus: true
        onLoaded: {
            const v = page.target ?? MapManager.startView();
            if (v.lat !== undefined)
                item.show(v.lat, v.lon, v.zoom);
            if (page.target)
                page.selected = page.target;
            item.forceActiveFocus();
            viewTimer.restart();
            targetMarker.update();
        }
        Connections {
            target: loader.item
            ignoreUnknownSignals: true
            function onPlaceSelected(place) { page.select(place); }
            function onViewChanged() {
                viewTimer.restart();
                targetMarker.update();
            }
        }
        Keys.onPressed: event => {
            if (!page.map)
                return;
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) page.map.zoomIn();
            else if (event.key === Qt.Key_Minus) page.map.zoomOut();
            else if (event.key === Qt.Key_Escape) page.selected = {};
            else return;
            event.accepted = true;
        }
    }
    Component.onDestruction: if (page.map) MapManager.saveView(page.map.latitude(), page.map.longitude(), page.map.zoom)

    // where "Show on Map" opened (a place clicked on the map is marked by Marble itself)
    Rectangle {
        id: targetMarker
        property point at: Qt.point(-100, -100)
        function update() {
            if (page.map && page.target && page.selected === page.target)
                at = page.map.pixelFor(page.target.lat, page.target.lon);
        }
        visible: page.map !== null && page.target !== null && page.selected === page.target
        x: at.x - width / 2
        y: at.y - height / 2
        width: Kirigami.Units.gridUnit
        height: width
        radius: width / 2
        color: Kirigami.Theme.highlightColor
        border.color: Kirigami.Theme.highlightedTextColor
        border.width: 2
    }

    // offline, or keeping an area
    ColumnLayout {
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: Kirigami.Units.largeSpacing }
        spacing: Kirigami.Units.smallSpacing
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.map !== null && !Connectivity.online
            type: Kirigami.MessageType.Information
            text: Connectivity.allowed ? i18n("Offline: the map shows the areas kept on this computer and those viewed before.")
                                       : i18n("Internet use is off in Settings: the map shows the areas kept on this computer and those viewed before.")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: MapManager.saving
            type: Kirigami.MessageType.Information
            text: i18n("Keeping this area for use offline: %1 of %2 parts", MapManager.saveDone, MapManager.saveTotal)
            actions: Kirigami.Action {
                text: i18n("Stop")
                icon.name: "process-stop"
                onTriggered: MapManager.cancelSave()
            }
        }
    }

    // what was clicked, and its article
    QQC2.Pane {
        id: card
        visible: page.hasSelection && page.map !== null
        anchors { right: parent.right; bottom: attribution.top; margins: Kirigami.Units.largeSpacing }
        width: Math.min(Kirigami.Units.gridUnit * 20, parent.width - 2 * Kirigami.Units.largeSpacing)
        padding: Kirigami.Units.largeSpacing
        background: Kirigami.ShadowedRectangle {
            color: Kirigami.Theme.backgroundColor
            radius: Kirigami.Units.cornerRadius
            shadow.size: Kirigami.Units.largeSpacing
            shadow.color: Qt.rgba(0, 0, 0, 0.3)
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            RowLayout {
                Layout.fillWidth: true
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    textFormat: Text.PlainText
                    text: page.selected.name ?? ""
                    wrapMode: Text.Wrap
                }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    display: QQC2.AbstractButton.IconOnly
                    text: i18n("Close")
                    onClicked: page.selected = {}
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: page.selected.address ?? ""
                wrapMode: Text.Wrap
                opacity: 0.7
                visible: text !== ""
            }
            AppButton {
                kind: "primary"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: page.selected.article?.path !== undefined
                icon.name: "document-open"
                text: page.selected.article?.path ? i18n("Read “%1”", Util.plain(page.selected.article.title)) : ""
                onClicked: page.openArticle(page.selected.article.libraryId, page.selected.article.path)
                QQC2.ToolTip.text: page.selected.article?.path
                    ? (page.selected.article.exact ? i18n("The article OpenStreetMap links to this place, in %1", Util.plain(page.selected.article.library))
                                                  : i18n("An article with this place's name, in %1", Util.plain(page.selected.article.library))) : ""
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            AppButton {
                kind: page.selected.article?.path !== undefined ? "flat" : "primary" // the main thing to do when there is no article
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                icon.name: "search"
                text: i18n("Search the Libraries for “%1”", Util.plain(page.selected.name ?? ""))
                onClicked: page.openSearch(page.selected.name)
            }
        }
    }

    // zoom
    ColumnLayout {
        visible: page.map !== null
        anchors { left: parent.left; bottom: attribution.top; margins: Kirigami.Units.largeSpacing; bottomMargin: Kirigami.Units.gridUnit * 3 }
        spacing: Kirigami.Units.smallSpacing
        QQC2.RoundButton {
            icon.name: "zoom-in"
            text: i18n("Zoom In")
            display: QQC2.AbstractButton.IconOnly
            onClicked: page.map.zoomIn()
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        QQC2.RoundButton {
            icon.name: "zoom-out"
            text: i18n("Zoom Out")
            display: QQC2.AbstractButton.IconOnly
            onClicked: page.map.zoomOut()
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }

    // the map data's licence asks for this
    QQC2.Label {
        id: attribution
        visible: page.map !== null
        anchors { right: parent.right; bottom: parent.bottom }
        padding: Kirigami.Units.smallSpacing
        textFormat: Text.PlainText
        text: i18n("© OpenStreetMap contributors · Marble")
        font: Kirigami.Theme.smallFont
        background: Rectangle { color: Kirigami.Theme.backgroundColor; opacity: 0.8 }
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: loader.status === Loader.Error
        icon.name: "map-flat"
        text: i18n("The map needs Marble")
        explanation: i18n("Install Marble's QML module and plugins (on Debian and Ubuntu: qml6-module-org-kde-marble and marble-plugins), then open the map again.")
    }
}
