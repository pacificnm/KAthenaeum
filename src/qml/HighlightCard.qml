// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// One highlight: the passage in its colour, its note (written here), and what can be done with it. In an article's Notes
// panel and on the Notes page.
Kirigami.ShadowedRectangle {
    id: card

    required property var highlight  // Notes.forArticle() / Notes.all() item

    signal goTo()
    signal addToProject()

    // the highlighter's colours, as the article shows them
    function swatch(name) {
        return { yellow: "#facc15", green: "#4ade80", blue: "#60a5fa", pink: "#f472b6" }[name] ?? "#facc15";
    }

    implicitHeight: layout.implicitHeight + Kirigami.Units.largeSpacing * 2
    radius: Kirigami.Units.cornerRadius * 1.5
    color: Kirigami.Theme.backgroundColor
    border.width: 1
    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.1)
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    // the colour, down the left edge
    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
        width: 4
        radius: 2
        color: card.swatch(card.highlight.color)
    }

    ColumnLayout {
        id: layout
        anchors { fill: parent; margins: Kirigami.Units.largeSpacing; leftMargin: Kirigami.Units.largeSpacing + 4 }
        spacing: Kirigami.Units.smallSpacing

        // the passage, as a highlighter would have it
        QQC2.Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: card.highlight.text
            wrapMode: Text.Wrap
            maximumLineCount: 8
            elide: Text.ElideRight
            font.family: "Noto Serif"
            background: Rectangle {
                color: Qt.alpha(card.swatch(card.highlight.color), 0.22)
                radius: 3
                anchors.margins: -2
            }
        }

        // the note: written here, kept when the field is left
        QQC2.TextArea {
            id: noteField
            Layout.fillWidth: true
            textFormat: TextEdit.PlainText
            placeholderText: i18n("Add a note…")
            wrapMode: TextEdit.Wrap
            text: card.highlight.note
            function save() {
                if (text.trim() !== card.highlight.note)
                    Notes.setNote(card.highlight.id, text);
            }
            onEditingFinished: save()
            Keys.onEscapePressed: { text = card.highlight.note; focus = false; }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            // change the colour
            Repeater {
                model: Notes.colors
                delegate: QQC2.AbstractButton {
                    required property string modelData
                    implicitWidth: Kirigami.Units.iconSizes.small + 4
                    implicitHeight: implicitWidth
                    hoverEnabled: true
                    Accessible.name: modelData
                    QQC2.ToolTip.text: { return { yellow: i18n("Yellow"), green: i18n("Green"), blue: i18n("Blue"), pink: i18n("Pink") }[modelData]; }
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: Notes.setColor(card.highlight.id, modelData)
                    contentItem: Item {}
                    background: Rectangle {
                        radius: width / 2
                        color: card.swatch(modelData)
                        border.width: card.highlight.color === modelData ? 2 : (parent.hovered ? 1 : 0)
                        border.color: Kirigami.Theme.textColor
                    }
                }
            }
            Item { Layout.fillWidth: true }
            QQC2.ToolButton {
                icon.name: "go-jump"
                text: i18n("Show in Article")
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: card.goTo()
            }
            QQC2.ToolButton {
                icon.name: "bookmark-new"
                text: i18n("Save to Project…")
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: { noteField.save(); card.addToProject(); }
            }
            QQC2.ToolButton {
                icon.name: "edit-delete"
                text: i18n("Remove Highlight")
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                // no question asked: it can be put back
                onClicked: {
                    const h = card.highlight;
                    if (Notes.remove(h.id))
                        applicationWindow().showPassiveNotification(i18n("Highlight removed"), "long", i18n("Undo"),
                                                                    () => Notes.add(h.libraryId, h.path, h.title, h.text, h.color, h.note));
                }
            }
        }
    }
}
