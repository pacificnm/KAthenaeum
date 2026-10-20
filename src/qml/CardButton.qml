// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

// A card that is one big button (a library, a course, a map region): raised on hover, reachable with Tab, pressed with
// Enter or Space, and announced as a button with its name. Set `Accessible.name`.
Kirigami.ShadowedRectangle {
    id: card

    signal clicked()
    readonly property bool hovered: hover.hovered
    readonly property bool highlighted: hover.hovered || activeFocus

    radius: Kirigami.Units.cornerRadius * 2
    color: Kirigami.Theme.backgroundColor
    border.width: activeFocus ? 2 : 1
    border.color: highlighted ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.15)
    shadow.size: highlighted ? Kirigami.Units.largeSpacing * 1.5 : Kirigami.Units.smallSpacing
    shadow.color: Qt.rgba(0, 0, 0, highlighted ? 0.18 : 0.08)
    shadow.yOffset: highlighted ? 2 : 1
    Behavior on shadow.size { NumberAnimation { duration: Kirigami.Units.shortDuration } }
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    activeFocusOnTab: true
    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()
    Keys.onSpacePressed: card.clicked()

    Accessible.role: Accessible.Button
    Accessible.focusable: true
    Accessible.onPressAction: card.clicked()

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.clicked() }
}
