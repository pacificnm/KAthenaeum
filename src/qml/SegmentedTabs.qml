// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Switching between a page's views: a rounded track with the chosen segment raised (the desktop style's tab bar of
// square grey tabs looked dated). `tabs`: [{text, icon}]
Item {
    id: root

    property int currentIndex: 0
    property var tabs: []

    implicitHeight: track.implicitHeight + Kirigami.Units.largeSpacing * 2
    implicitWidth: track.implicitWidth + Kirigami.Units.largeSpacing * 2
    Accessible.role: Accessible.PageTabList

    Rectangle {
        id: track
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: Kirigami.Units.largeSpacing }
        width: Math.min(row.implicitWidth + 6, parent.width - Kirigami.Units.largeSpacing * 2)
        implicitWidth: row.implicitWidth + 6
        implicitHeight: row.implicitHeight + 6
        radius: Kirigami.Units.cornerRadius * 2
        color: Qt.alpha(Kirigami.Theme.textColor, 0.07)

        // the chosen segment, sliding between them
        Rectangle {
            readonly property Item target: repeater.count > root.currentIndex ? repeater.itemAt(root.currentIndex) : null
            x: target ? target.x + 3 : 3
            y: 3
            width: target ? target.width : 0
            height: parent.height - 6
            radius: Kirigami.Units.cornerRadius * 1.5
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            Behavior on x { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic } }
        }

        RowLayout {
            id: row
            anchors { fill: parent; margins: 3 }
            spacing: 0
            Repeater {
                id: repeater
                model: root.tabs
                delegate: QQC2.AbstractButton {
                    id: segment
                    required property var modelData
                    required property int index
                    readonly property bool chosen: root.currentIndex === index
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    leftPadding: Kirigami.Units.largeSpacing * 1.5
                    rightPadding: Kirigami.Units.largeSpacing * 1.5
                    topPadding: Kirigami.Units.smallSpacing + 1
                    bottomPadding: Kirigami.Units.smallSpacing + 1
                    text: modelData.text
                    Accessible.role: Accessible.PageTab
                    Accessible.checked: chosen
                    onClicked: root.currentIndex = index
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Item { Layout.fillWidth: true }
                        Kirigami.Icon {
                            visible: (segment.modelData.icon ?? "") !== ""
                            source: segment.modelData.icon ?? ""
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            color: segment.chosen ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                            opacity: segment.chosen || segment.hovered ? 1 : 0.7
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            text: segment.text
                            font.weight: segment.chosen ? Font.DemiBold : Font.Normal
                            opacity: segment.chosen || segment.hovered ? 1 : 0.75
                        }
                        Item { Layout.fillWidth: true }
                    }
                    background: Rectangle {
                        radius: Kirigami.Units.cornerRadius * 1.5
                        color: "transparent"
                        border.width: segment.visualFocus ? 2 : 0
                        border.color: Kirigami.Theme.focusColor
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
    }
}
