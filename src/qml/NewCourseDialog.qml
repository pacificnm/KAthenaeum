// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A new course: a core subject or one of the learner's own, at a level, of a length. The teacher plans it from the
// installed libraries; when none covers the subject, this says which to get.
Kirigami.Dialog {
    id: dialog

    property string subject: "other"  // a core subject's key, or "other"
    readonly property bool own: subject === "other"
    readonly property var coverage: Classroom.coverage(subject, level.currentValue ?? "adult")

    signal created(var courseId)
    signal getLibrary(string title)

    title: own ? i18n("Your Own Course") : i18n("New %1 Course", Classroom.subject(subject).name)
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 30
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: i18n("Plan the Course")
            icon.name: "applications-education"
            enabled: titleField.text.trim() !== "" && dialog.coverage.ok
            onTriggered: {
                const id = Classroom.createCourse(dialog.own ? ownSubject.currentValue : dialog.subject, titleField.text, level.currentValue,
                                                  focusField.text, lengths.values[lengths.currentIndex]);
                if (id > 0) {
                    dialog.close();
                    dialog.created(id);
                }
            }
        }
    ]

    onOpened: {
        titleField.text = own ? "" : Classroom.subject(subject).name;
        focusField.text = "";
        level.currentIndex = Math.max(0, Classroom.levels.findIndex(l => l.key === Classroom.defaultLevel()));
        lengths.currentIndex = 0;
        ownSubject.currentIndex = Classroom.subjects.length;  // "Other"
        (own ? titleField : focusField).forceActiveFocus();
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.TextField {
                id: titleField
                Kirigami.FormData.label: i18n("Course:")
                placeholderText: i18n("Volcanoes, The Roman Empire, Fractions…")
            }
            QQC2.ComboBox {
                id: ownSubject
                visible: dialog.own
                Kirigami.FormData.label: i18n("Subject:")
                model: Classroom.subjects.concat([{ key: "other", name: i18n("Other") }])
                textRole: "name"
                valueRole: "key"
            }
            QQC2.ComboBox {
                id: level
                Kirigami.FormData.label: i18n("Level:")
                model: Classroom.levels
                textRole: "label"
                valueRole: "key"
            }
            SegmentedTabs {
                id: lengths
                Kirigami.FormData.label: i18n("Length:")
                readonly property var values: ["short", "standard", "long"]
                tabs: [{ text: i18n("Short (about 9 lessons)") }, { text: i18n("Standard (16)") }, { text: i18n("Long (24)") }]
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: i18n("What should it cover? (optional)")
        }
        QQC2.TextArea {
            id: focusField
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            placeholderText: dialog.own ? i18n("What you want to learn about it, in your own words")
                                        : i18n("Topics you want, or leave empty for the usual ones at this level")
        }

        // where the lessons will come from
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: dialog.coverage.ok ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
            text: dialog.coverage.ok ? i18n("Readings will come from: %1", Util.plain((dialog.coverage.libraries ?? []).join(", ")))
                                     : i18n("No installed library covers this subject. Get “%1” from the Library Manager first.", dialog.coverage.suggestion ?? "")
            actions: Kirigami.Action {
                visible: !dialog.coverage.ok
                text: i18n("Get It…")
                icon.name: "folder-download"
                onTriggered: {
                    dialog.close();
                    dialog.getLibrary(dialog.coverage.suggestion);
                }
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: (dialog.coverage.simpleSuggestion ?? "") !== ""
            type: Kirigami.MessageType.Information
            text: i18n("Tip: “Wikipedia in simple English” (530 MB) is written for younger readers. With it installed, lessons at this level read from it first.")
            actions: Kirigami.Action {
                text: i18n("Get It…")
                icon.name: "folder-download"
                onTriggered: {
                    dialog.close();
                    dialog.getLibrary(dialog.coverage.simpleSuggestion);
                }
            }
        }
    }
}
