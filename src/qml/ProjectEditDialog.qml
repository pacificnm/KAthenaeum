// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A project's name and goal (the research question; the project chat's model is told it)
Kirigami.Dialog {
    id: dialog

    property string name
    property string goal

    signal done(string name, string goal)

    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

    readonly property bool canAccept: nameField.text.trim() !== ""
    onCanAcceptChanged: updateOk()
    function updateOk() {
        const ok = standardButton(Kirigami.Dialog.Ok);  // not there before the dialog is first shown
        if (ok)
            ok.enabled = canAccept;
    }

    onOpened: {
        nameField.text = dialog.name;
        goalField.text = dialog.goal;
        nameField.forceActiveFocus();
        updateOk();
    }
    onAccepted: if (nameField.text.trim() !== "") dialog.done(nameField.text, goalField.text)

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing
        QQC2.Label { textFormat: Text.PlainText; text: i18n("Name") }
        QQC2.TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: i18n("Water as a solvent")
            onAccepted: if (dialog.canAccept) dialog.accept()
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.topMargin: Kirigami.Units.largeSpacing
            text: i18n("Goal or research question (optional)")
        }
        QQC2.TextArea {
            id: goalField
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            wrapMode: TextEdit.Wrap
            placeholderText: i18n("Why does water dissolve so many things?")
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: i18n("Answers in the project's chat are written with this goal in mind.")
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
            wrapMode: Text.Wrap
        }
    }
}
