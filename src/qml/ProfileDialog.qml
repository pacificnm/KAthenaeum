// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// The learner's profile: what the teacher knows about them. Kept on this computer only.
Kirigami.Dialog {
    id: dialog

    title: Classroom.hasProfile ? i18n("Your Profile") : i18n("Meet Your Teacher")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 28
    standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Cancel

    function gradeIndex(key) {
        return Math.max(0, Classroom.grades.findIndex(g => g.key === key));
    }

    onOpened: {
        const p = Classroom.profile;
        nameField.text = p.name ?? "";
        grade.currentIndex = gradeIndex(p.grade ?? "none");
        interests.text = p.interests ?? "";
        goals.text = p.goals ?? "";
        updateSave();
        nameField.forceActiveFocus();
    }
    function updateSave() {
        const save = standardButton(Kirigami.Dialog.Save);  // not there before the dialog is first shown
        if (save)
            save.enabled = nameField.text.trim() !== "";
    }
    onAccepted: Classroom.saveProfile({ name: nameField.text, grade: grade.currentValue, interests: interests.text, goals: goals.text })

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: i18n("Your teacher uses this to pitch lessons at the right level and to choose examples you will like. It stays on this computer.")
            opacity: 0.8
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.TextField {
                id: nameField
                Layout.fillWidth: true
                Kirigami.FormData.label: i18n("Your name:")
                placeholderText: i18n("What should the teacher call you?")
                onTextChanged: dialog.updateSave()
            }
            QQC2.ComboBox {
                id: grade
                Kirigami.FormData.label: i18n("Highest grade completed:")
                model: Classroom.grades
                textRole: "label"
                valueRole: "key"
            }
            QQC2.TextField {
                id: interests
                Layout.fillWidth: true
                Kirigami.FormData.label: i18n("What you enjoy:")
                placeholderText: i18n("Soccer, drawing, space, video games…")
            }
            QQC2.TextArea {
                id: goals
                Kirigami.FormData.label: i18n("What you want to learn for:")
                Layout.fillWidth: true
                implicitHeight: Kirigami.Units.gridUnit * 4
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                placeholderText: i18n("Catching up in maths, getting ready for college, just curious…")
            }
        }
    }
}
