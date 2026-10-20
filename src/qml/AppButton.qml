// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The app's push button: compact, rounded, an icon beside its words. The desktop style's raised grey button looks
// dated. `kind`:
//   primary   — the main thing to do here (Ask, Read, Download): filled with the accent colour
//   secondary — other actions: a soft tint
//   flat      — links and minor actions ("Show All"): no background until hovered
//   danger    — stopping or cancelling something running: red words, a red tint when hovered
QQC2.AbstractButton {
    id: button

    property string kind: "secondary"
    property bool iconAfter: false  // "Show All ›"

    readonly property bool primary: kind === "primary"
    readonly property color foreground: primary ? Kirigami.Theme.highlightedTextColor
                                      : kind === "danger" ? Kirigami.Theme.negativeTextColor
                                      : kind === "flat" && hovered ? Kirigami.Theme.highlightColor
                                      : Kirigami.Theme.textColor

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    // disabled reads as faded, in its own colours (the disabled colour group turned the accent grey: Ask lost its fill)
    opacity: enabled ? 1 : 0.45
    Kirigami.Theme.colorGroup: Kirigami.Theme.Active
    leftPadding: Kirigami.Units.largeSpacing
    rightPadding: Kirigami.Units.largeSpacing + (icon.name !== "" ? Kirigami.Units.smallSpacing : 0)
    topPadding: Kirigami.Units.smallSpacing + 1
    bottomPadding: Kirigami.Units.smallSpacing + 1
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    // its words whole beside a stretching label ("Change…" was cut to "Chang…"); a button that may shrink says so
    Layout.minimumWidth: implicitWidth
    implicitHeight: Math.max(Kirigami.Units.gridUnit * 1.6, implicitContentHeight + topPadding + bottomPadding)
    Accessible.role: Accessible.Button
    Accessible.name: text

    // centred, so a button stretched across a card reads like one
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width)
            spacing: Kirigami.Units.smallSpacing
            layoutDirection: button.iconAfter !== (Qt.application.layoutDirection === Qt.RightToLeft) ? Qt.RightToLeft : Qt.LeftToRight
            Kirigami.Icon {
                visible: button.icon.name !== ""
                source: button.icon.name
                color: button.foreground
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.alignment: Qt.AlignVCenter
            }
            QQC2.Label {
                id: label
                visible: text !== ""
                textFormat: Text.PlainText
                text: button.text
                color: button.foreground
                font.weight: button.primary ? Font.DemiBold : Font.Medium
                // shortened only when the button is narrower than its words (rounding cut "Change…" to "Chang…")
                elide: button.width + 0.5 < button.implicitWidth ? Text.ElideRight : Text.ElideNone
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    background: Rectangle {
        radius: Kirigami.Units.cornerRadius * 1.5
        color: {
            switch (button.kind) {
            case "primary":
                return button.down ? Qt.darker(Kirigami.Theme.highlightColor, 1.15)
                     : button.hovered ? Qt.lighter(Kirigami.Theme.highlightColor, 1.1) : Kirigami.Theme.highlightColor;
            case "danger":
                return Qt.alpha(Kirigami.Theme.negativeTextColor, button.down ? 0.25 : button.hovered ? 0.14 : 0.06);
            case "flat":
                return Qt.alpha(Kirigami.Theme.textColor, button.down ? 0.12 : button.hovered ? 0.07 : 0);
            default:
                return Qt.alpha(Kirigami.Theme.textColor, button.down ? 0.16 : button.hovered ? 0.11 : 0.06);
            }
        }
        border.width: button.visualFocus ? 2 : 0
        border.color: Kirigami.Theme.focusColor
        Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
    }

    HoverHandler { cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
