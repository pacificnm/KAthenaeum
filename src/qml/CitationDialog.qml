// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A citation (an article) or a bibliography (a project's articles) in each style schools ask for, to copy. The chosen
// style (Settings › Reading) is shown first; another can be made the usual one from here.
// `citations`: [{style, label, text, html}]
Kirigami.Dialog {
    id: dialog

    property var citations: []
    property string explanation

    title: i18n("Cite")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 32
    standardButtons: Kirigami.Dialog.Close

    readonly property var current: citations.length > 0 ? citations[Math.min(tabs.currentIndex, citations.length - 1)] : ({})

    onOpened: tabs.currentIndex = Math.max(0, citations.findIndex(c => c.style === LibraryBrowser.citationStyle))

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        SegmentedTabs {
            id: tabs
            Layout.fillWidth: true
            Layout.leftMargin: -Kirigami.Units.largeSpacing
            tabs: dialog.citations.map(c => ({ text: c.label.replace(/ \(.*\)$/, ""), icon: "" }))
        }

        QQC2.Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: dialog.current.label ?? ""
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        // the citation, selectable; Copy keeps the italics when pasted into a word processor
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(citationText.implicitHeight + Kirigami.Units.largeSpacing, Kirigami.Units.gridUnit * 16)
            QQC2.TextArea {
                id: citationText
                textFormat: TextEdit.PlainText
                text: dialog.current.text ?? ""
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.family: "Noto Serif"
                background: Rectangle {
                    radius: Kirigami.Units.cornerRadius
                    color: Qt.alpha(Kirigami.Theme.textColor, 0.05)
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.explanation !== ""
            textFormat: Text.PlainText
            text: dialog.explanation
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            AppButton {
                kind: "primary"
                icon.name: "edit-copy"
                text: i18n("Copy")
                enabled: (dialog.current.text ?? "") !== ""
                onClicked: {
                    LibraryBrowser.copyCitation(dialog.current.text, dialog.current.html);
                    applicationWindow().showPassiveNotification(i18n("Citation copied"));
                }
            }
            AppButton {
                kind: "flat"
                icon.name: "starred-symbolic"
                text: i18n("Use %1 by Default", (dialog.current.label ?? "").replace(/ \(.*\)$/, ""))
                visible: dialog.current.style !== undefined && dialog.current.style !== LibraryBrowser.citationStyle
                onClicked: LibraryBrowser.citationStyle = dialog.current.style
            }
            Item { Layout.fillWidth: true }
        }
    }
}
