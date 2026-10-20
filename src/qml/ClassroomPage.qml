// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// The classroom: who is learning, their courses, and the subjects to start one in. The teacher (the local model) plans
// each course from the installed libraries.
Kirigami.ScrollablePage {
    id: page

    signal openCourse(var courseId)
    signal getLibrary(string title)

    title: i18n("Classroom")

    actions: [
        Kirigami.Action {
            text: i18n("Your Profile")
            icon.name: "user-identity"
            visible: Classroom.hasProfile
            onTriggered: profileDialog.open()
        }
    ]

    // a card, as the libraries are shown
    component Card: Kirigami.ShadowedRectangle {
        id: card
        signal clicked()
        property alias hovered: hover.hovered
        radius: Kirigami.Units.cornerRadius * 2
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: hover.hovered ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.15)
        shadow.size: hover.hovered ? Kirigami.Units.largeSpacing * 1.5 : Kirigami.Units.smallSpacing
        shadow.color: Qt.rgba(0, 0, 0, hover.hovered ? 0.18 : 0.08)
        shadow.yOffset: hover.hovered ? 2 : 1
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        Accessible.role: Accessible.Button
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: card.clicked() }
    }

    component SubjectIcon: Rectangle {
        property alias source: icon.source
        implicitWidth: Kirigami.Units.iconSizes.large + Kirigami.Units.largeSpacing * 2
        implicitHeight: implicitWidth
        radius: Kirigami.Units.cornerRadius * 2
        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.1)
        Kirigami.Icon {
            id: icon
            anchors.centerIn: parent
            width: Kirigami.Units.iconSizes.medium
            height: width
        }
    }

    readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 17)))

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // no profile yet: what the classroom is, and the first step
        Card {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !Classroom.hasProfile
            implicitHeight: welcome.implicitHeight + Kirigami.Units.gridUnit * 2
            onClicked: profileDialog.open()
            RowLayout {
                id: welcome
                anchors { fill: parent; margins: Kirigami.Units.gridUnit }
                spacing: Kirigami.Units.gridUnit
                SubjectIcon { source: "applications-education"; Layout.alignment: Qt.AlignTop }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Kirigami.Heading { level: 2; textFormat: Text.PlainText; text: i18n("A teacher for whatever you want to learn") }
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: i18n("Choose a subject and a level, and the AI teacher plans a course for you: units, lessons, and readings from your own libraries, with an introduction to each lesson. Start by telling it a little about yourself.")
                    }
                    AppButton {
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        kind: "primary"
                        icon.name: "user-identity"
                        text: i18n("Set Up Your Profile")
                        onClicked: profileDialog.open()
                    }
                }
            }
        }

        // the learner
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: Classroom.hasProfile
            Kirigami.Heading {
                level: 1
                textFormat: Text.PlainText
                text: i18n("Hi, %1", (Classroom.profile.name ?? "").split(" ")[0])
            }
            QQC2.Label {
                Layout.alignment: Qt.AlignBaseline
                textFormat: Text.PlainText
                text: Classroom.profile.grade !== "none" ? i18n("Completed: %1", Classroom.profile.gradeLabel) : ""
                opacity: 0.6
            }
            Item { Layout.fillWidth: true }
        }

        // the courses
        Kirigami.Heading {
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.smallSpacing
            visible: Classroom.courses.length > 0
            level: 3
            textFormat: Text.PlainText
            text: i18n("Your Courses")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: page.columns
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            visible: Classroom.courses.length > 0
            Repeater {
                model: Classroom.courses
                delegate: Card {
                    id: courseCard
                    required property var modelData
                    readonly property bool planning: Classroom.job === "plan" && Classroom.jobSubject === modelData.id
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: courseLayout.implicitHeight + Kirigami.Units.largeSpacing * 3
                    Accessible.name: modelData.title
                    onClicked: page.openCourse(modelData.id)
                    RowLayout {
                        id: courseLayout
                        anchors { fill: parent; margins: Kirigami.Units.largeSpacing * 1.5 }
                        spacing: Kirigami.Units.largeSpacing
                        SubjectIcon { source: courseCard.modelData.icon; Layout.alignment: Qt.AlignTop }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: courseCard.modelData.title
                                elide: Text.ElideRight
                                font.weight: Font.DemiBold
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.1
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: courseCard.modelData.subjectName + " · " + courseCard.modelData.levelLabel
                                elide: Text.ElideRight
                                opacity: 0.7
                                font: Kirigami.Theme.smallFont
                            }
                            QQC2.ProgressBar {
                                Layout.fillWidth: true
                                visible: courseCard.modelData.status === "ready" || courseCard.planning
                                indeterminate: courseCard.planning && Classroom.total <= 1
                                from: 0
                                to: courseCard.planning ? Math.max(1, Classroom.total) : Math.max(1, courseCard.modelData.lessons)
                                value: courseCard.planning ? Classroom.step : courseCard.modelData.completed
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                font: Kirigami.Theme.smallFont
                                elide: Text.ElideRight
                                color: courseCard.modelData.status === "failed" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                                opacity: courseCard.modelData.status === "failed" ? 1 : 0.7
                                text: courseCard.planning ? i18n("The teacher is planning it…")
                                    : courseCard.modelData.status === "failed" ? i18n("Not planned: open it to try again")
                                    : courseCard.modelData.status === "ready"
                                        ? (courseCard.modelData.completed >= courseCard.modelData.lessons
                                           ? i18n("All %1 lessons done", courseCard.modelData.lessons)
                                           : i18n("%1 of %2 lessons done", courseCard.modelData.completed, courseCard.modelData.lessons))
                                    : i18n("Waiting to be planned")
                            }
                        }
                    }
                }
            }
        }

        // the subjects
        Kirigami.Heading {
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.smallSpacing
            level: 3
            textFormat: Text.PlainText
            text: i18n("Start a Course")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: page.columns
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing
            Repeater {
                model: Classroom.subjects.concat([{ key: "other", name: i18n("Your Own Course…"), icon: "list-add", blurb: i18n("Anything you want to learn about") }])
                delegate: Card {
                    id: subjectCard
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: subjectLayout.implicitHeight + Kirigami.Units.largeSpacing * 3
                    Accessible.name: modelData.name
                    onClicked: {
                        if (!Classroom.hasProfile) {
                            profileDialog.open();
                            return;
                        }
                        newCourse.subject = modelData.key;
                        newCourse.open();
                    }
                    RowLayout {
                        id: subjectLayout
                        anchors { fill: parent; margins: Kirigami.Units.largeSpacing * 1.5 }
                        spacing: Kirigami.Units.largeSpacing
                        SubjectIcon { source: subjectCard.modelData.icon }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: subjectCard.modelData.name
                                elide: Text.ElideRight
                                font.weight: Font.DemiBold
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: subjectCard.modelData.blurb
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                opacity: 0.7
                                font: Kirigami.Theme.smallFont
                            }
                        }
                    }
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.margins: Kirigami.Units.smallSpacing
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            opacity: 0.6
            text: i18n("The teacher is a small AI model running on this computer. Its lessons come from your libraries, and it can be wrong: read the articles it gives you, and check what matters.")
        }
    }

    ProfileDialog {
        id: profileDialog
    }
    NewCourseDialog {
        id: newCourse
        onCreated: courseId => page.openCourse(courseId)
        onGetLibrary: title => page.getLibrary(title)
    }
}
