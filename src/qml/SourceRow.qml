// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One source (or document, or chat) of a project: a heading or a quoted passage, a line about where it is from, the
// user's note, and buttons to edit the note or remove it
QQC2.ItemDelegate {
    id: row

    property string heading
    property string quote
    property string detail
    property string note
    property string iconName
    property bool canNote: true
    property bool removable: true
    property string removeText: i18n("Remove from Project")

    signal open()
    signal editNote()
    signal remove()

    onClicked: row.open()

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing
        Kirigami.Icon {
            source: row.iconName
            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
            Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2
            QQC2.Label {
                Layout.fillWidth: true
                visible: row.heading !== ""
                text: row.heading
                elide: Text.ElideRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: row.quote !== ""
                text: "“" + row.quote + "”"
                wrapMode: Text.Wrap
                maximumLineCount: 5
                elide: Text.ElideRight
                font.italic: true
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: row.detail !== ""
                text: row.quote !== "" ? "— " + row.detail : row.detail
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                elide: Text.ElideRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: row.note !== ""
                text: i18n("Note: %1", row.note)
                wrapMode: Text.Wrap
                color: Kirigami.Theme.neutralTextColor
            }
        }
        QQC2.ToolButton {
            visible: row.canNote
            icon.name: "document-edit"
            text: row.note === "" ? i18n("Add a Note") : i18n("Edit Note")
            display: QQC2.AbstractButton.IconOnly
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: row.editNote()
        }
        QQC2.ToolButton {
            visible: row.removable
            icon.name: "edit-delete-remove"
            text: row.removeText
            display: QQC2.AbstractButton.IconOnly
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: row.remove()
        }
    }
}
