// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A word's meaning from an installed Wiktionary, beside where it was selected. Without a dictionary it says how to get
// one.
QQC2.Popup {
    id: popup

    property var entry: ({})  // LibraryBrowser.define()

    signal openEntry(var libraryId, string path)
    signal getDictionary()

    function show(word, at) {
        entry = LibraryBrowser.define(word);
        // beside the selection, inside the page
        const p = parent;
        width = Math.min(Kirigami.Units.gridUnit * 24, p.width - Kirigami.Units.largeSpacing * 2);
        x = Math.max(Kirigami.Units.largeSpacing, Math.min(at.x, p.width - width - Kirigami.Units.largeSpacing));
        y = Math.max(Kirigami.Units.largeSpacing, Math.min(at.y + Kirigami.Units.gridUnit, p.height - Kirigami.Units.gridUnit * 14));
        open();
    }

    padding: Kirigami.Units.largeSpacing
    modal: false
    focus: true
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - y - Kirigami.Units.largeSpacing)
    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius * 2
        border.width: 1
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.25)
        shadow.yOffset: 2
    }

    contentItem: QQC2.ScrollView {
        id: scroll
        implicitHeight: body.implicitHeight
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        ColumnLayout {
            id: body
            width: scroll.availableWidth
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Heading {
                    level: 3
                    textFormat: Text.PlainText
                    text: popup.entry.word ?? ""
                    font.family: "Noto Serif"
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    text: popup.entry.pronunciation ?? ""
                    visible: text !== ""
                    opacity: 0.7
                }
                Item { Layout.fillWidth: true }
                QQC2.ToolButton {
                    icon.name: "window-close"
                    display: QQC2.AbstractButton.IconOnly
                    text: i18n("Close")
                    onClicked: popup.close()
                }
            }

            // "molecules": the plural of molecule, then what a molecule is
            QQC2.Label {
                Layout.fillWidth: true
                visible: (popup.entry.formOf ?? "") !== ""
                textFormat: Text.PlainText
                text: i18n("“%1”: %2", popup.entry.asked ?? "", popup.entry.formOf ?? "")
                wrapMode: Text.Wrap
                font.italic: true
                opacity: 0.8
            }

            // the parts of speech, each with its first senses
            Repeater {
                model: popup.entry.found ? popup.entry.parts : []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Label {
                        textFormat: Text.PlainText
                        text: modelData.name
                        font.italic: true
                        color: Kirigami.Theme.highlightColor
                    }
                    Repeater {
                        model: modelData.senses
                        delegate: RowLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            QQC2.Label {
                                Layout.alignment: Qt.AlignTop
                                textFormat: Text.PlainText
                                text: (index + 1) + "."
                                opacity: 0.6
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    text: modelData.definition
                                    wrapMode: Text.Wrap
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: modelData.example !== ""
                                    textFormat: Text.PlainText
                                    text: modelData.example
                                    wrapMode: Text.Wrap
                                    font.italic: true
                                    opacity: 0.7
                                }
                            }
                        }
                    }
                }
            }

            // nothing found, or no dictionary
            QQC2.Label {
                Layout.fillWidth: true
                visible: popup.entry.found === false
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: popup.entry.hasDictionary ? i18n("Not found in your dictionary.")
                                                : i18n("Install Wiktionary from the Library Manager to look up words. The Simple English edition is small (31 MB); the full one knows far more words (9 GB).")
                opacity: 0.8
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                AppButton {
                    kind: "flat"
                    visible: popup.entry.found === true
                    icon.name: "document-open"
                    text: i18n("Full Entry")
                    onClicked: {
                        popup.close();
                        popup.openEntry(popup.entry.libraryId, popup.entry.path);
                    }
                }
                AppButton {
                    kind: "primary"
                    visible: popup.entry.found === false && !popup.entry.hasDictionary
                    icon.name: "folder-download"
                    text: i18n("Get a Dictionary…")
                    onClicked: {
                        popup.close();
                        popup.getDictionary();
                    }
                }
                Item { Layout.fillWidth: true }
                QQC2.Label {
                    visible: popup.entry.found === true
                    textFormat: Text.PlainText
                    text: Util.plain(popup.entry.library ?? "")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
            }
        }
    }
}
