// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// One course: its syllabus (units and lessons, each lesson's readings found in the libraries), or the teacher planning it.
Kirigami.ScrollablePage {
    id: page

    required property var courseId
    property var course: ({})
    readonly property bool planning: Classroom.job === "plan" && Classroom.jobSubject === courseId

    signal openLesson(var lessonId)
    signal openProject(var projectId)
    signal openUnitTest(var unitId, string unitTitle)
    signal openReading(var libraryId, string path)
    signal askTeacher(var lessonId, string question)
    signal closed()

    title: Util.pageTitle(course.title ?? i18n("Course"))

    property var missed: []
    property bool showAllMissed: false
    function refresh() {
        page.course = Classroom.course(page.courseId);
        page.missed = Classroom.missed(page.courseId);
    }
    Component.onCompleted: refresh()
    Connections {
        target: Classroom
        function onCoursesChanged() { page.refresh(); }
    }

    // the first lesson not done yet
    readonly property var nextLesson: {
        for (const u of (course.units ?? []))
            for (const l of u.lessons)
                if (!l.completed)
                    return l;
        return null;
    }

    actions: [
        Kirigami.Action {
            text: page.course.completed > 0 ? i18n("Continue") : i18n("Start")
            icon.name: "media-playback-start"
            visible: page.course.status === "ready" && page.nextLesson !== null
            onTriggered: page.openLesson(page.nextLesson.id)
        },
        Kirigami.Action {
            text: (page.course.projectId ?? 0) > 0 ? i18n("Project") : i18n("Keep in a Project…")
            icon.name: "folder-bookmark"
            tooltip: (page.course.projectId ?? 0) > 0 ? i18n("Open “%1”, where this course's readings, introductions and highlights are kept", Util.plain(page.course.projectTitle))
                                                      : i18n("Keep this course's readings, introductions and highlights in a research project")
            onTriggered: (page.course.projectId ?? 0) > 0 ? page.openProject(page.course.projectId) : projectDialog.open()
        },
        Kirigami.Action {
            text: i18n("Stop Keeping in “%1”", Util.plain(page.course.projectTitle ?? ""))
            icon.name: "edit-clear"
            visible: (page.course.projectId ?? 0) > 0
            displayHint: Kirigami.DisplayHint.AlwaysHide
            tooltip: i18n("The project and what is in it stay; new lessons and highlights no longer go there")
            onTriggered: Classroom.untieProject(page.courseId)
        },
        Kirigami.Action {
            text: i18n("Plan Again")
            icon.name: "view-refresh"
            visible: !page.planning
            enabled: !Classroom.busy
            displayHint: page.course.status === "ready" ? Kirigami.DisplayHint.AlwaysHide : Kirigami.DisplayHint.NoPreference
            onTriggered: page.course.status === "ready" ? replanDialog.open() : Classroom.planAgain(page.courseId)
        },
        Kirigami.Action {
            text: i18n("Delete Course")
            icon.name: "edit-delete"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: deleteDialog.open()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // what it is
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon {
                source: page.course.icon ?? "applications-education"
                Layout.preferredWidth: Kirigami.Units.iconSizes.large
                Layout.preferredHeight: Kirigami.Units.iconSizes.large
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 1
                    textFormat: Text.PlainText
                    text: page.course.title ?? ""
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    text: (page.course.subjectName ?? "") + " · " + (page.course.levelLabel ?? "")
                          + (page.course.status === "ready" ? " · " + i18n("%1 of %2 lessons done", page.course.completed, page.course.lessons) : "")
                    opacity: 0.7
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: (page.course.focus ?? "") !== ""
                    textFormat: Text.PlainText
                    text: page.course.focus ?? ""
                    wrapMode: Text.Wrap
                    font.italic: true
                    opacity: 0.7
                }
            }
        }

        // the teacher at work
        Kirigami.ShadowedRectangle {
            Layout.fillWidth: true
            visible: page.planning
            implicitHeight: planningLayout.implicitHeight + Kirigami.Units.gridUnit * 2
            radius: Kirigami.Units.cornerRadius * 2
            color: Qt.alpha(Kirigami.Theme.highlightColor, 0.08)
            ColumnLayout {
                id: planningLayout
                anchors { fill: parent; margins: Kirigami.Units.gridUnit }
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    QQC2.BusyIndicator { running: page.planning; Layout.preferredHeight: Kirigami.Units.iconSizes.medium; Layout.preferredWidth: Layout.preferredHeight }
                    Kirigami.Heading { level: 3; textFormat: Text.PlainText; text: i18n("Your teacher is planning this course") }
                    Item { Layout.fillWidth: true }
                    AppButton { kind: "danger"; icon.name: "process-stop"; text: i18n("Stop"); onClicked: Classroom.stop() }
                }
                QQC2.Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: Classroom.label; elide: Text.ElideRight; opacity: 0.8 }
                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: Classroom.total <= 1
                    from: 0
                    to: Math.max(1, Classroom.total)
                    value: Classroom.step
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("It writes the syllabus, then finds each lesson's readings in your libraries. A lesson nothing in them is about is left out. This takes a minute or so; you can leave this page meanwhile.")
                }
            }
        }

        // not planned
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !page.planning && (page.course.status === "failed" || page.course.status === "new")
            type: page.course.status === "failed" ? Kirigami.MessageType.Warning : Kirigami.MessageType.Information
            text: page.course.status === "failed" ? Util.plain(page.course.error) : i18n("This course has not been planned yet.")
            actions: Kirigami.Action {
                text: i18n("Plan Again")
                icon.name: "view-refresh"
                enabled: !Classroom.busy
                onTriggered: Classroom.planAgain(page.courseId)
            }
        }

        // what to look at again: questions got wrong in the latest quizzes and tests
        Kirigami.ShadowedRectangle {
            Layout.fillWidth: true
            visible: !page.planning && page.missed.length > 0
            implicitHeight: reviewLayout.implicitHeight + Kirigami.Units.gridUnit * 1.5
            radius: Kirigami.Units.cornerRadius * 2
            color: Qt.alpha(Kirigami.Theme.neutralTextColor, 0.08)
            ColumnLayout {
                id: reviewLayout
                anchors { fill: parent; margins: Kirigami.Units.largeSpacing * 1.5 }
                spacing: Kirigami.Units.largeSpacing
                RowLayout {
                    Kirigami.Icon {
                        source: "view-refresh"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    }
                    Kirigami.Heading {
                        level: 3
                        textFormat: Text.PlainText
                        text: i18np("To review: %1 question", "To review: %1 questions", page.missed.length)
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        visible: page.missed.length > 3
                        kind: "flat"
                        text: page.showAllMissed ? i18n("Show Fewer") : i18n("Show All")
                        onClicked: page.showAllMissed = !page.showAllMissed
                    }
                }
                Repeater {
                    model: page.showAllMissed ? page.missed : page.missed.slice(0, 3)
                    delegate: ColumnLayout {
                        id: miss
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: miss.modelData.text
                            wrapMode: Text.Wrap
                            font.weight: Font.DemiBold
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: i18n("The answer: %1", miss.modelData.options[miss.modelData.answer])
                            color: Kirigami.Theme.positiveTextColor
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: miss.modelData.chosen >= 0
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: i18n("You answered: %1", miss.modelData.chosen >= 0 ? miss.modelData.options[miss.modelData.chosen] : "")
                            opacity: 0.7
                        }
                        RowLayout {
                            QQC2.Label {
                                textFormat: Text.PlainText
                                text: miss.modelData.lessonTitle
                                font: Kirigami.Theme.smallFont
                                opacity: 0.6
                            }
                            AppButton {
                                kind: "flat"
                                icon.name: "document-open"
                                text: i18n("Read It")
                                onClicked: page.openReading(miss.modelData.libraryId, miss.modelData.path)
                            }
                            AppButton {
                                kind: "flat"
                                icon.name: "dialog-messages"
                                text: i18n("Ask Your Teacher")
                                onClicked: page.askTeacher(miss.modelData.lessonId,
                                                           i18n("I got this question wrong: “%1” I answered “%2”. Can you help me understand?", miss.modelData.text,
                                                                miss.modelData.chosen >= 0 ? miss.modelData.options[miss.modelData.chosen] : i18n("nothing")))
                            }
                        }
                    }
                }
            }
        }

        // the syllabus
        Repeater {
            model: page.planning ? [] : (page.course.units ?? [])
            delegate: ColumnLayout {
                id: unit
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    Layout.leftMargin: Kirigami.Units.smallSpacing
                    QQC2.Label {
                        textFormat: Text.PlainText
                        text: i18n("Unit %1", unit.index + 1)
                        font: Kirigami.Theme.smallFont
                        opacity: 0.6
                    }
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        textFormat: Text.PlainText
                        text: unit.modelData.title
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        visible: unit.modelData.testScore >= 0
                        textFormat: Text.PlainText
                        text: i18n("Test: %1%", unit.modelData.testScore)
                        color: unit.modelData.testScore >= 70 ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                        font: Kirigami.Theme.smallFont
                    }
                    AppButton {
                        kind: "flat"
                        icon.name: "checkmark"
                        text: unit.modelData.testScore >= 0 ? i18n("Unit Test") : i18n("Take the Unit Test")
                        onClicked: page.openUnitTest(unit.modelData.id, unit.modelData.title)
                    }
                }

                Repeater {
                    model: unit.modelData.lessons
                    delegate: QQC2.ItemDelegate {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        onClicked: page.openLesson(modelData.id)
                        background: Rectangle {
                            radius: Kirigami.Units.cornerRadius
                            color: row.hovered ? Qt.alpha(Kirigami.Theme.highlightColor, 0.1) : Qt.alpha(Kirigami.Theme.textColor, 0.03)
                        }
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.largeSpacing
                            // done: a tick in a filled circle; not yet: an empty one
                            Rectangle {
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Layout.preferredWidth
                                radius: width / 2
                                color: row.modelData.completed ? Kirigami.Theme.positiveTextColor : "transparent"
                                border.width: row.modelData.completed ? 0 : 2
                                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.3)
                                Kirigami.Icon {
                                    anchors.centerIn: parent
                                    visible: row.modelData.completed
                                    source: "checkmark"
                                    width: parent.width * 0.7
                                    height: width
                                    color: "white"
                                    isMask: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    text: row.modelData.title
                                    elide: Text.ElideRight
                                    font.weight: Font.DemiBold
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    textFormat: Text.PlainText
                                    text: row.modelData.objective
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    opacity: 0.7
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                            QQC2.Label {
                                visible: row.modelData.quizScore >= 0
                                textFormat: Text.PlainText
                                text: i18n("Quiz %1%", row.modelData.quizScore)
                                color: row.modelData.quizScore >= 70 ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                                font: Kirigami.Theme.smallFont
                            }
                            QQC2.Label {
                                textFormat: Text.PlainText
                                text: i18np("%1 reading", "%1 readings", row.modelData.readings)
                                opacity: 0.6
                                font: Kirigami.Theme.smallFont
                            }
                            Kirigami.Icon {
                                source: "go-next"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.5
                            }
                        }
                    }
                }
            }
        }
    }

    CourseProjectDialog {
        id: projectDialog
        courseId: page.courseId
        courseTitle: page.course.title ?? ""
        onTied: projectId => applicationWindow().showPassiveNotification(i18n("This course is now kept in a project"), "long",
                                                                          i18n("Open"), () => applicationWindow().openProject(projectId))
    }

    Kirigami.PromptDialog {
        id: replanDialog
        title: i18n("Plan this course again?")
        subtitle: i18n("The teacher writes a new syllabus with new readings. Lessons you marked done and their introductions are not kept.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Plan Again")
                icon.name: "view-refresh"
                onTriggered: {
                    Classroom.planAgain(page.courseId);
                    replanDialog.close();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: replanDialog.close() }
        ]
    }
    Kirigami.PromptDialog {
        id: deleteDialog
        title: i18n("Delete “%1”?", Util.plain(page.course.title ?? ""))
        subtitle: i18n("Its syllabus, introductions and progress are deleted. Your libraries and notes are not touched.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    deleteDialog.close();
                    Classroom.removeCourse(page.courseId);
                    page.closed();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: deleteDialog.close() }
        ]
    }
}
