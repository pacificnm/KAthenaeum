// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// One lesson: the teacher's introduction (written the first time the lesson is opened), its readings from the
// libraries, and marking it done.
Kirigami.ScrollablePage {
    id: page

    required property var lessonId
    property var lesson: ({})
    readonly property bool writing: Classroom.job === "intro" && Classroom.jobSubject === lessonId

    signal openArticle(var libraryId, string path)
    signal openLesson(var lessonId)
    signal openProject(var projectId)

    title: Util.pageTitle(lesson.title ?? i18n("Lesson"))

    function refresh() {
        page.lesson = Classroom.lesson(page.lessonId);
    }
    // the introduction is written when the lesson is first opened (unless the teacher is busy: then on request)
    Component.onCompleted: {
        refresh();
        if ((lesson.intro ?? "") === "" && (lesson.readings ?? []).length > 0)
            Classroom.writeIntro(page.lessonId);
    }
    Connections {
        target: Classroom
        function onIntroReady(lessonId) {
            if (lessonId === page.lessonId)
                page.refresh();
        }
        function onCoursesChanged() { page.refresh(); }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Previous Lesson")
            icon.name: "go-previous"
            enabled: (page.lesson.previousId ?? 0) > 0
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.openLesson(page.lesson.previousId)
        },
        Kirigami.Action {
            text: i18n("Next Lesson")
            icon.name: "go-next"
            enabled: (page.lesson.nextId ?? 0) > 0
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.openLesson(page.lesson.nextId)
        },
        Kirigami.Action {
            text: i18n("Write the Introduction Again")
            icon.name: "view-refresh"
            enabled: !Classroom.busy && (page.lesson.readings ?? []).length > 0
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: Classroom.writeIntro(page.lessonId)
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // where it is
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: i18n("%1 · %2 · Lesson %3 of %4", page.lesson.courseTitle ?? "", page.lesson.unitTitle ?? "", page.lesson.number ?? 0, page.lesson.count ?? 0)
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                opacity: 0.7  // the accent colour was nearly invisible on a light accent (the user, 2026-10-20)
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                textFormat: Text.PlainText
                text: page.lesson.title ?? ""
                wrapMode: Text.Wrap
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: text !== ""
                textFormat: Text.PlainText
                text: page.lesson.objective ?? ""
                wrapMode: Text.Wrap
                opacity: 0.75
            }
        }

        // the teacher's introduction
        Kirigami.ShadowedRectangle {
            Layout.fillWidth: true
            implicitHeight: introLayout.implicitHeight + Kirigami.Units.gridUnit * 2
            radius: Kirigami.Units.cornerRadius * 2
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false

            ColumnLayout {
                id: introLayout
                anchors { fill: parent; margins: Kirigami.Units.gridUnit }
                spacing: Kirigami.Units.largeSpacing
                RowLayout {
                    Kirigami.Icon {
                        source: "applications-education"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    }
                    Kirigami.Heading { level: 3; textFormat: Text.PlainText; text: i18n("From your teacher") }
                    Item { Layout.fillWidth: true }
                    QQC2.BusyIndicator {
                        running: page.writing
                        visible: page.writing
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredWidth: Layout.preferredHeight
                    }
                    AppButton {
                        visible: page.writing
                        kind: "danger"
                        icon.name: "process-stop"
                        text: i18n("Stop")
                        onClicked: Classroom.stop()
                    }
                }

                // written, or being written (Markdown from the model: tidied, no images; see ChatView)
                TextEdit {
                    Layout.fillWidth: true
                    visible: text !== ""
                    // while it is written, without the title the model may start with (it is dropped once written)
                    text: page.writing ? Classroom.streamed.replace(/^\s*# [^\n]*\n?/, "") : (page.lesson.intro ?? "")
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    color: Kirigami.Theme.textColor
                    selectedTextColor: Kirigami.Theme.highlightedTextColor
                    selectionColor: Kirigami.Theme.highlightColor
                    font: Kirigami.Theme.defaultFont
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: page.writing && Classroom.streamed === ""
                    textFormat: Text.PlainText
                    text: Classroom.label !== "" ? Classroom.label + "…" : i18n("Getting ready…")
                    opacity: 0.7
                }
                // not written, and not being written: the teacher was busy, or it failed
                RowLayout {
                    Layout.fillWidth: true
                    visible: !page.writing && (page.lesson.intro ?? "") === ""
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: Classroom.busy ? i18n("Your teacher is busy with another course. The introduction can be written once it has finished.")
                                             : i18n("No introduction yet.")
                        opacity: 0.7
                    }
                    AppButton {
                        kind: "primary"
                        icon.name: "document-edit"
                        text: i18n("Write It")
                        enabled: !Classroom.busy && (page.lesson.readings ?? []).length > 0
                        onClicked: Classroom.writeIntro(page.lessonId)
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: (page.lesson.intro ?? "") !== "" && !page.writing
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                    text: (page.lesson.readings ?? []).length > 1 ? i18n("[1], [2]: the reading a sentence comes from. [?]: a figure the readings do not have; check it.")
                                                                  : i18n("[?]: a figure the reading does not have; check it.")
                }
            }
        }

        // the readings
        Kirigami.Heading {
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.smallSpacing
            level: 3
            textFormat: Text.PlainText
            text: i18n("Read")
        }
        Repeater {
            model: page.lesson.readings ?? []
            delegate: QQC2.ItemDelegate {
                id: reading
                required property var modelData
                Layout.fillWidth: true
                enabled: modelData.available
                onClicked: page.openArticle(modelData.libraryId, modelData.path)
                background: Rectangle {
                    radius: Kirigami.Units.cornerRadius * 1.5
                    color: reading.hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.1) : Qt.alpha(Kirigami.Theme.textColor, 0.04)
                }
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 1.6
                        Layout.preferredHeight: Layout.preferredWidth
                        radius: width / 2
                        color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
                        QQC2.Label {
                            anchors.centerIn: parent
                            textFormat: Text.PlainText
                            text: reading.modelData.n
                            font.weight: Font.DemiBold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: reading.modelData.title
                            elide: Text.ElideRight
                            font.weight: Font.DemiBold
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: reading.modelData.available ? reading.modelData.library : i18n("Its library is no longer installed")
                            elide: Text.ElideRight
                            opacity: 0.6
                            font: Kirigami.Theme.smallFont
                        }
                    }
                    Kirigami.Icon {
                        source: "document-open"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                        opacity: 0.7
                    }
                }
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            opacity: 0.6
            text: i18n("While you read: highlight what matters, right-click a word to look it up, and use Ask beside the article if something is unclear.")
        }

        // the course's project: where the readings, this introduction and the highlights are kept
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                source: "folder-bookmark"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                opacity: 0.7
            }
            QQC2.Label {
                textFormat: Text.PlainText
                text: (page.lesson.projectId ?? 0) > 0 ? i18n("Kept in the project “%1”", page.lesson.projectTitle) : i18n("Not kept in a project")
                opacity: 0.7
            }
            AppButton {
                kind: "flat"
                text: (page.lesson.projectId ?? 0) > 0 ? i18n("Open Project") : i18n("Keep the Course in a Project…")
                onClicked: (page.lesson.projectId ?? 0) > 0 ? page.openProject(page.lesson.projectId) : projectDialog.open()
            }
            Item { Layout.fillWidth: true }
        }

        // done
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing
            AppButton {
                kind: page.lesson.completed ? "secondary" : "primary"
                icon.name: page.lesson.completed ? "edit-undo" : "checkmark"
                text: page.lesson.completed ? i18n("Mark as Not Done") : i18n("I've Finished This Lesson")
                onClicked: Classroom.setCompleted(page.lessonId, !page.lesson.completed)
            }
            AppButton {
                kind: "flat"
                iconAfter: true
                icon.name: "go-next"
                visible: (page.lesson.nextId ?? 0) > 0
                text: i18n("Next Lesson")
                onClicked: page.openLesson(page.lesson.nextId)
            }
            Item { Layout.fillWidth: true }
        }
    }

    CourseProjectDialog {
        id: projectDialog
        courseId: page.lesson.courseId ?? 0
        courseTitle: page.lesson.courseTitle ?? ""
        onTied: projectId => applicationWindow().showPassiveNotification(i18n("This course is now kept in a project"), "long",
                                                                          i18n("Open"), () => page.openProject(projectId))
    }
}
