// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// An article to open, as a rounded pill (the desktop style draws Kirigami.Chip as a square grey button)
QQC2.AbstractButton {
    id: chip

    hoverEnabled: true
    leftPadding: Kirigami.Units.largeSpacing
    rightPadding: Kirigami.Units.largeSpacing
    topPadding: Kirigami.Units.smallSpacing + 1
    bottomPadding: Kirigami.Units.smallSpacing + 1
    Accessible.role: Accessible.Link

    contentItem: QQC2.Label {
        textFormat: Text.PlainText
        text: chip.text
        elide: Text.ElideRight
        color: chip.hovered ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
    }
    background: Rectangle {
        radius: height / 2
        color: chip.down ? Qt.alpha(Kirigami.Theme.highlightColor, 0.25)
             : chip.hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.12) : Qt.alpha(Kirigami.Theme.textColor, 0.05)
        border.width: 1
        border.color: chip.hovered || chip.visualFocus ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.15)
        Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
