// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import MapLibre 4.0
import KAthenaeum

// The offline map: the regions downloaded in Settings, drawn by MapLibre. A click on a place shows what it is and opens
// its Wikipedia article from the installed libraries.
Kirigami.Page {
    id: page

    signal openArticle(var libraryId, string path)
    signal openSearch(string query)
    signal openMapSettings()

    title: i18n("Map")
    padding: 0

    readonly property bool dark: Kirigami.Theme.backgroundColor.hslLightness < 0.5
    property var selected: ({})  // what placeAt() returned for the last click
    readonly property bool hasSelection: selected.name !== undefined

    // the pixel of a coordinate on the map as it is now (the marker follows the map)
    function pixelFor(lat, lon) {
        const center = map.coordinate;
        const scale = Math.pow(2, map.zoomLevel) * 512;
        const x = (lon - center[1]) / 360 * scale;
        const merc = l => Math.log(Math.tan(Math.PI / 4 + l * Math.PI / 360));
        const y = -(merc(lat) - merc(center[0])) / (2 * Math.PI) * scale;
        return Qt.point(map.width / 2 + x, map.height / 2 + y);
    }
    function select(x, y) {
        const c = MapManager.coordinateAt(map.coordinate[0], map.coordinate[1], map.zoomLevel, x - map.width / 2, y - map.height / 2);
        page.selected = MapManager.placeAt(c[0], c[1], map.zoomLevel, Kirigami.Units.gridUnit * 1.5);
    }
    // set, not animated: after an animated easeTo() the item's coordinate and zoomLevel still say where it started (MapLibre
    // Qt reads them back from the camera before the animation runs), and clicks were then placed ~100 px off
    function flyTo(lat, lon, zoom) {
        map.zoomLevel = zoom;
        map.coordinate = [lat, lon];
    }
    function zoomBy(factor) {
        map.scale(factor, Qt.point(map.width / 2, map.height / 2));
    }

    Component.onCompleted: {
        const v = MapManager.startView();
        map.coordinate = [v.lat, v.lon];
        map.zoomLevel = v.zoom;
    }

    MapLibre {
        id: map
        anchors.fill: parent
        visible: MapManager.hasMaps
        // a new style when regions change or the colours do
        style: MapManager.revision >= 0 ? MapManager.styleUrl(page.dark) : ""

        DragHandler {
            id: drag
            target: null
            onTranslationChanged: delta => map.pan(delta)
        }
        PinchHandler {
            id: pinch
            target: null
            onScaleChanged: delta => map.scale(delta, pinch.centroid.position)
            grabPermissions: PointerHandler.TakeOverForbidden
        }
        WheelHandler {
            id: wheel
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => map.scale(Math.pow(2.0, event.angleDelta.y / 480), wheel.point.position)
        }
        TapHandler {
            onTapped: eventPoint => page.select(eventPoint.position.x, eventPoint.position.y)
            onDoubleTapped: eventPoint => map.scale(2, eventPoint.position)
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) page.zoomBy(2);
            else if (event.key === Qt.Key_Minus) page.zoomBy(0.5);
            else if (event.key === Qt.Key_Escape) page.selected = {};
            else return;
            event.accepted = true;
        }
        focus: true
    }

    // the clicked place
    Rectangle {
        visible: page.hasSelection && map.visible
        readonly property point at: page.hasSelection ? page.pixelFor(page.selected.lat, page.selected.lon) : Qt.point(0, 0)
        x: at.x - width / 2
        y: at.y - height / 2
        width: Kirigami.Units.gridUnit * 0.9
        height: width
        radius: width / 2
        color: Kirigami.Theme.highlightColor
        border.color: "white"
        border.width: 2
    }

    // search by place name
    ColumnLayout {
        visible: map.visible
        anchors { top: parent.top; left: parent.left; margins: Kirigami.Units.largeSpacing }
        width: Math.min(Kirigami.Units.gridUnit * 18, parent.width - 2 * Kirigami.Units.largeSpacing)
        spacing: 0

        Kirigami.SearchField {
            id: search
            Layout.fillWidth: true
            placeholderText: i18n("Find a place…")
            autoAccept: false
            onTextChanged: results.model = text.length >= 2 ? MapManager.searchPlaces(text) : []
            onAccepted: if (results.count > 0) results.itemAtIndex(0).clicked()
            Keys.onDownPressed: results.forceActiveFocus()
        }
        QQC2.Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(results.contentHeight + topPadding + bottomPadding, Kirigami.Units.gridUnit * 16)
            visible: search.text.length >= 2 && search.activeFocus || results.activeFocus
            padding: 1
            background: Rectangle { color: Kirigami.Theme.backgroundColor; border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2) }
            ListView {
                id: results
                anchors.fill: parent
                clip: true
                keyNavigationEnabled: true
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    contentItem: ColumnLayout {
                        spacing: 0
                        QQC2.Label { textFormat: Text.PlainText; text: modelData.name; elide: Text.ElideRight; Layout.fillWidth: true }
                        QQC2.Label { textFormat: Text.PlainText; text: modelData.kindLabel; opacity: 0.7; font: Kirigami.Theme.smallFont; visible: text !== "" }
                    }
                    onClicked: {
                        page.flyTo(modelData.lat, modelData.lon, modelData.zoom);
                        search.text = "";
                        map.forceActiveFocus();
                    }
                }
                QQC2.Label {
                    anchors.centerIn: parent
                    visible: results.count === 0
                    textFormat: Text.PlainText
                    text: i18n("No place by that name in the downloaded regions")
                    opacity: 0.7
                }
            }
        }
    }

    // what was clicked, and its article
    QQC2.Pane {
        id: card
        visible: page.hasSelection && map.visible
        // on the right, where it covers neither the search box nor (usually) what was clicked
        anchors { right: parent.right; top: parent.top; margins: Kirigami.Units.largeSpacing }
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
                textFormat: Text.PlainText
                text: page.selected.kindLabel ?? ""
                opacity: 0.7
                visible: text !== ""
            }
            AppButton {
                kind: "primary"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: page.selected.article !== undefined
                icon.name: "document-open"
                text: page.selected.article ? i18n("Read “%1”", Util.plain(page.selected.article.title)) : ""
                onClicked: page.openArticle(page.selected.article.libraryId, page.selected.article.path)
                QQC2.ToolTip.text: page.selected.article
                    ? (page.selected.article.exact ? i18n("The article OpenStreetMap links to this place, in %1", Util.plain(page.selected.article.library))
                                                  : i18n("An article with this place's name, in %1", Util.plain(page.selected.article.library))) : ""
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            AppButton {
                kind: page.selected.article !== undefined ? "flat" : "primary"  // the main thing to do when there is no article
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                icon.name: "search"
                text: i18n("Search the Libraries for “%1”", Util.plain(page.selected.name ?? ""))
                onClicked: page.openSearch(page.selected.name)
            }
            QQC2.Label {
                Layout.topMargin: Kirigami.Units.smallSpacing
                visible: (page.selected.around ?? []).length > 0
                textFormat: Text.PlainText
                text: i18n("In")
                font.bold: true
            }
            Repeater {
                model: page.selected.around ?? []
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    leftPadding: 0
                    enabled: modelData.article !== undefined
                    contentItem: RowLayout {
                        QQC2.Label {
                            textFormat: Text.PlainText
                            text: modelData.name
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            color: modelData.article ? Kirigami.Theme.linkColor : Kirigami.Theme.textColor
                        }
                        QQC2.Label { textFormat: Text.PlainText; text: modelData.kindLabel; opacity: 0.7 }
                    }
                    onClicked: page.openArticle(modelData.article.libraryId, modelData.article.path)
                }
            }
        }
    }

    // zoom
    ColumnLayout {
        visible: map.visible
        anchors { right: parent.right; bottom: attribution.top; margins: Kirigami.Units.largeSpacing }
        spacing: Kirigami.Units.smallSpacing
        QQC2.RoundButton {
            icon.name: "zoom-in"
            onClicked: page.zoomBy(2)
            QQC2.ToolTip.text: i18n("Zoom In")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        QQC2.RoundButton {
            icon.name: "zoom-out"
            onClicked: page.zoomBy(0.5)
            QQC2.ToolTip.text: i18n("Zoom Out")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }

    // the map data's licence asks for this
    QQC2.Label {
        id: attribution
        visible: map.visible
        anchors { right: parent.right; bottom: parent.bottom }
        padding: Kirigami.Units.smallSpacing
        textFormat: Text.PlainText
        text: i18n("© OpenStreetMap contributors · Protomaps · MapLibre")
        font: Kirigami.Theme.smallFont
        background: Rectangle { color: Kirigami.Theme.backgroundColor; opacity: 0.8 }
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: !MapManager.hasMaps
        icon.name: "map-flat"
        text: !MapManager.available ? i18n("The map's style is not installed") : i18n("No maps downloaded")
        explanation: i18n("Download the world overview and the regions you want in Settings. Click a place on the map to read about it in your libraries.")
        helpfulAction: Kirigami.Action {
            text: i18n("Download Maps…")
            icon.name: "folder-download"
            onTriggered: page.openMapSettings()
        }
    }
}
