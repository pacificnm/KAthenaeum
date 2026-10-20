// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// Choose a research project (or start one) for something being added to it: an article, a selected passage, an answer,
// a chat. The most recently used project comes first.
Kirigami.Dialog {
    id: dialog

    property string preview      // the passage or answer being saved, shown quoted
    property bool withNote: true
    property var already: []     // projects that already have it

    signal chosen(var projectId, string projectTitle, string note)

    title: i18n("Add to Project")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

    // a new project needs a name: OK would do nothing (and close) with an empty one
    readonly property bool canAccept: combo.currentValue !== -1 || newTitle.text.trim() !== ""
    onCanAcceptChanged: updateOk()
    function updateOk() {
        const ok = standardButton(Kirigami.Dialog.Ok);  // not there before the dialog is first shown
        if (ok)
            ok.enabled = canAccept;
    }

    function choices() {
        return projects.list().map(p => ({ projectId: p.projectId, title: Util.plain(p.title) })).concat([{ projectId: -1, title: i18n("New Project…") }]);
    }

    onOpened: {
        projects.refresh();
        combo.model = choices();
        combo.currentIndex = 0;
        note.text = "";
        newTitle.text = "";
        if (combo.currentValue === -1)
            newTitle.forceActiveFocus();
        updateOk();
    }
    onAccepted: {
        let id = combo.currentValue;
        let name = combo.currentText;
        if (id === -1) {
            name = newTitle.text.trim();
            id = projects.create(name);
        }
        if (id > 0)
            dialog.chosen(id, name, dialog.withNote ? note.text : "");
    }

    ProjectList {
        id: projects
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            visible: dialog.preview !== ""
            text: "“" + dialog.preview + "”"
            wrapMode: Text.Wrap
            maximumLineCount: 6
            elide: Text.ElideRight
            font.italic: true
        }
        QQC2.ComboBox {
            id: combo
            Layout.fillWidth: true
            textRole: "title"
            valueRole: "projectId"
            displayText: currentValue > 0 && dialog.already.includes(currentValue) ? i18n("%1 (already there)", currentText) : currentText
        }
        QQC2.TextField {
            id: newTitle
            Layout.fillWidth: true
            visible: combo.currentValue === -1
            placeholderText: i18n("Name of the new project")
            onAccepted: if (dialog.canAccept) dialog.accept()
        }
        QQC2.TextArea {
            id: note
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            visible: dialog.withNote
            placeholderText: i18n("A note (optional)")
            wrapMode: TextEdit.Wrap
        }
    }
}
