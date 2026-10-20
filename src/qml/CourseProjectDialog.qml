// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami
import KAthenaeum

// Keeping a course in a research project: a new one named after it (one click), or one the learner already has. From
// then on its lessons' readings, introductions and highlights go into the project.
Kirigami.PromptDialog {
    id: dialog

    property var courseId: 0
    property string courseTitle

    signal tied(var projectId)

    title: i18n("Keep This Course in a Project")
    subtitle: i18n("Each lesson's readings become the project's sources, its introduction a document to write your answers in, and what you highlight while reading is saved there too. Use the project's chat, documents and drafts for the whole course.")
    standardButtons: Kirigami.Dialog.NoButton
    customFooterActions: [
        Kirigami.Action {
            text: i18n("New Project “%1”", Util.plain(dialog.courseTitle))
            icon.name: "folder-new"
            onTriggered: {
                dialog.close();
                const id = Classroom.tieProject(dialog.courseId, 0);
                if (id > 0)
                    dialog.tied(id);
            }
        },
        Kirigami.Action {
            text: i18n("Choose a Project…")
            icon.name: "folder-bookmark"
            onTriggered: {
                dialog.close();
                picker.open();
            }
        },
        Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: dialog.close() }
    ]

    ProjectPicker {
        id: picker
        title: i18n("Keep This Course in a Project")
        withNote: false
        onChosen: (projectId, projectTitle) => {
            const id = Classroom.tieProject(dialog.courseId, projectId);
            if (id > 0)
                dialog.tied(id);
        }
    }
}
