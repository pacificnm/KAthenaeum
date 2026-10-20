// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.marble
import KAthenaeum

// Marble's map with OpenStreetMap's data from KDE's server ("Vector OSM"), in its own file so the Map page can say what
// to install when Marble's QML module is missing. Marble caches what it shows; offline it draws only what it has kept.
MarbleItem {
    id: map

    // a place was clicked: {name, wikipedia, address, lat, lon}
    signal placeSelected(var place)
    // the view moved or zoomed
    signal viewChanged()

    function show(lat, lon, zoomLevel) {
        map.centerOn(lon, lat);
        if (zoomLevel !== undefined)
            map.zoom = zoomLevel;
    }
    // where a coordinate is on the map now
    function pixelFor(lat, lon) {
        probe.latitude = lat;
        probe.longitude = lon;
        return map.screenCoordinatesFromCoordinate(probe);
    }
    function latitude() { return map.centerLatitude(); }
    function longitude() { return map.centerLongitude(); }
    // the area shown, in degrees: {west, south, east, north}, or null when the view is not on the map (around the globe)
    function bounds() {
        const ok1 = map.screenCoordinatesToCoordinate(Qt.point(0, 0), topLeft);
        const ok2 = map.screenCoordinatesToCoordinate(Qt.point(map.width, map.height), bottomRight);
        if (!ok1 || !ok2)
            return null;
        return { west: topLeft.longitude, north: topLeft.latitude, east: bottomRight.longitude, south: bottomRight.latitude };
    }

    Coordinate { id: probe }
    Coordinate { id: topLeft }
    Coordinate { id: bottomRight }

    mapThemeId: "earth/vectorosm/vectorosm.dgml"
    projection: MarbleItem.Mercator
    showCompass: false
    showCrosshairs: false
    showOverviewMap: false
    showScaleBar: true
    showOtherPlaces: false
    showAtmosphere: false
    showClouds: false
    workOffline: !Connectivity.online

    onVisibleLatLonAltBoxChanged: map.viewChanged()
    onZoomChanged: map.viewChanged()
    onLmbMenuRequested: point => map.selectPlacemarkAt(point.x, point.y)

    // Marble hands the clicked feature to this, at its place on the map: it is the marker
    placemarkDelegate: Rectangle {
        property var placemark
        property int xPos // where Marble draws the place, set by Marble as the map moves
        property int yPos
        x: xPos - width / 2
        y: yPos - height / 2
        width: 16
        height: 16
        radius: 8
        color: Kirigami.Theme.highlightColor
        border.color: Kirigami.Theme.highlightedTextColor
        border.width: 2
        onPlacemarkChanged: {
            if (placemark && placemark.name !== "")
                map.placeSelected({ name: placemark.name, wikipedia: placemark.wikipedia, address: placemark.address,
                                    lat: placemark.latitude, lon: placemark.longitude });
        }
    }
}
