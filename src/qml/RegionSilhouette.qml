// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import KAthenaeum

// A region's outline, filled, fitted into the item with its proportions kept (as it looks on the map)
Item {
    id: root

    property string regionId
    property color color: Kirigami.Theme.highlightColor
    readonly property var shape: regionId !== "" ? MapManager.regionShape(regionId) : ({})
    readonly property real aspect: shape.aspect ?? 1

    Shape {
        id: outline
        anchors.centerIn: parent
        width: root.aspect >= root.width / Math.max(1, root.height) ? root.width : root.height * root.aspect
        height: root.aspect >= root.width / Math.max(1, root.height) ? root.width / root.aspect : root.height
        visible: (root.shape.paths ?? []).length > 0
        antialiasing: true
        layer.enabled: true  // smooth edges with the default renderer
        layer.samples: 4

        ShapePath {
            fillColor: Qt.alpha(root.color, 0.35)
            strokeColor: root.color
            strokeWidth: 1.5
            fillRule: ShapePath.WindingFill  // parts of a region may overlap (Australia and Oceania)
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(outline.width, outline.height)
            PathMultiline {
                paths: root.shape.paths ?? []
            }
        }
    }
}
