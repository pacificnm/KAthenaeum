// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A lesson's quiz or a unit's test: written by the teacher from the readings (each answer checked against its passage),
// answered here, graded with the right answer, why, and the passage it came from.
ColumnLayout {
    id: view

    required property string kind  // lesson | unit
    required property var targetId
    property var quiz: ({})
    property var chosen: []      // the option picked for each question, -1 none
    property bool answering: false  // taking it (again), rather than looking at the result
    readonly property bool writing: Classroom.job === "quiz" && Classroom.quizKind === kind && Classroom.jobSubject === targetId
    readonly property var result: quiz.result ?? null

    signal openReading(var libraryId, string path)
    signal askTeacher(var lessonId, string question)

    spacing: Kirigami.Units.largeSpacing

    function refresh() {
        view.quiz = Classroom.quiz(view.kind, view.targetId);
        view.answering = view.quiz.id !== undefined && !view.quiz.result;
        view.chosen = (view.quiz.questions ?? []).map(() => -1);
    }
    Component.onCompleted: refresh()
    Connections {
        target: Classroom
        function onQuizReady(kind, targetId) {
            if (kind === view.kind && targetId === view.targetId)
                view.refresh();
        }
    }
    function letter(i) { return String.fromCharCode(65 + i); }

    // ---- none yet, or being written ----
    Kirigami.ShadowedRectangle {
        Layout.fillWidth: true
        visible: view.quiz.id === undefined || view.writing
        implicitHeight: emptyLayout.implicitHeight + Kirigami.Units.gridUnit * 2
        radius: Kirigami.Units.cornerRadius * 2
        color: Qt.alpha(Kirigami.Theme.textColor, 0.04)
        ColumnLayout {
            id: emptyLayout
            anchors { fill: parent; margins: Kirigami.Units.gridUnit }
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Heading {
                level: 3
                textFormat: Text.PlainText
                text: view.writing ? i18n("Your teacher is writing the questions…")
                    : view.kind === "unit" ? i18n("Test yourself on this unit") : i18n("Check what you learned")
            }
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                opacity: 0.8
                text: view.writing ? Classroom.label
                    : view.kind === "unit" ? i18n("Questions on each of the unit's lessons, written from their readings. Every answer is checked against the reading it comes from. It takes a minute or two to write.")
                    : i18n("Five questions on this lesson's readings. Every answer is checked against the reading it comes from. Pass with %1% or more and the lesson is marked finished. It takes about a minute to write.", 70)
            }
            QQC2.ProgressBar {
                Layout.fillWidth: true
                visible: view.writing
                from: 0
                to: Math.max(1, Classroom.total)
                value: Classroom.step
                indeterminate: Classroom.total <= 1
            }
            RowLayout {
                AppButton {
                    visible: !view.writing
                    kind: "primary"
                    icon.name: "document-edit"
                    text: view.kind === "unit" ? i18n("Write the Test") : i18n("Write the Quiz")
                    enabled: !Classroom.busy
                    onClicked: Classroom.makeQuiz(view.kind, view.targetId)
                }
                AppButton {
                    visible: view.writing
                    kind: "danger"
                    icon.name: "process-stop"
                    text: i18n("Stop")
                    onClicked: Classroom.stop()
                }
                QQC2.Label {
                    visible: Classroom.busy && !view.writing
                    textFormat: Text.PlainText
                    text: i18n("The teacher is busy with something else. Try again in a moment.")
                    opacity: 0.7
                }
            }
        }
    }

    // ---- the result ----
    Kirigami.ShadowedRectangle {
        Layout.fillWidth: true
        visible: !view.writing && !view.answering && view.result !== null
        implicitHeight: scoreLayout.implicitHeight + Kirigami.Units.gridUnit * 2
        radius: Kirigami.Units.cornerRadius * 2
        color: view.result && view.result.passed ? Qt.alpha(Kirigami.Theme.positiveTextColor, 0.12) : Qt.alpha(Kirigami.Theme.neutralTextColor, 0.12)
        RowLayout {
            id: scoreLayout
            anchors { fill: parent; margins: Kirigami.Units.gridUnit }
            spacing: Kirigami.Units.gridUnit
            Kirigami.Heading {
                level: 1
                textFormat: Text.PlainText
                text: view.result ? view.result.percent + "%" : ""
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                QQC2.Label {
                    textFormat: Text.PlainText
                    font.weight: Font.DemiBold
                    text: !view.result ? "" : view.result.passed ? i18n("Well done: %1 of %2 right", view.result.score, view.result.total)
                                                               : i18n("%1 of %2 right: look again at what you missed", view.result.score, view.result.total)
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    text: view.kind === "lesson" && view.result && view.result.passed ? i18n("The lesson is marked finished.")
                                                                                      : i18n("Each answer below shows the passage it comes from.")
                }
            }
            AppButton {
                kind: "secondary"
                icon.name: "view-refresh"
                text: i18n("Try Again")
                onClicked: {
                    view.chosen = (view.quiz.questions ?? []).map(() => -1);
                    view.answering = true;
                }
            }
            AppButton {
                kind: "flat"
                icon.name: "document-edit"
                text: i18n("New Questions")
                enabled: !Classroom.busy
                onClicked: Classroom.makeQuiz(view.kind, view.targetId)
            }
        }
    }

    // ---- the questions ----
    Repeater {
        model: view.writing ? [] : view.answering ? (view.quiz.questions ?? []) : (view.result ? view.result.questions : [])
        delegate: Kirigami.ShadowedRectangle {
            id: card
            required property var modelData
            required property int index
            readonly property bool graded: !view.answering
            Layout.fillWidth: true
            implicitHeight: questionLayout.implicitHeight + Kirigami.Units.gridUnit * 1.5
            radius: Kirigami.Units.cornerRadius * 2
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: !graded ? Qt.alpha(Kirigami.Theme.textColor, 0.12)
                        : modelData.correct ? Qt.alpha(Kirigami.Theme.positiveTextColor, 0.6) : Qt.alpha(Kirigami.Theme.negativeTextColor, 0.6)
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false

            ColumnLayout {
                id: questionLayout
                anchors { fill: parent; margins: Kirigami.Units.largeSpacing * 1.5 }
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.alignment: Qt.AlignTop
                        textFormat: Text.PlainText
                        text: (card.index + 1) + "."
                        font.weight: Font.DemiBold
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: card.modelData.text
                        wrapMode: Text.Wrap
                        font.weight: Font.DemiBold
                    }
                    QQC2.Label {
                        visible: card.graded && view.kind === "unit" && (card.modelData.lessonTitle ?? "") !== ""
                        textFormat: Text.PlainText
                        text: card.modelData.lessonTitle ?? ""
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                    }
                }
                // the options: to choose, or graded
                Repeater {
                    model: card.modelData.options
                    delegate: QQC2.RadioDelegate {
                        id: option
                        required property string modelData
                        required property int index
                        readonly property bool isAnswer: card.graded && index === card.modelData.answer
                        readonly property bool wasChosen: card.graded && index === card.modelData.chosen
                        Layout.fillWidth: true
                        // graded: shown in full colour (disabled, the right answer was too faint to read) but not clickable
                        checkable: !card.graded
                        hoverEnabled: !card.graded
                        checked: card.graded ? wasChosen : view.chosen[card.index] === index
                        text: view.letter(index) + ")  " + modelData
                        onClicked: {
                            if (card.graded)
                                return;
                            const c = view.chosen.slice();
                            c[card.index] = index;
                            view.chosen = c;
                        }
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            QQC2.Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: option.text
                                wrapMode: Text.Wrap
                                leftPadding: option.indicator.width + option.spacing
                                color: option.isAnswer ? Kirigami.Theme.positiveTextColor
                                     : option.wasChosen ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                                font.weight: option.isAnswer ? Font.DemiBold : Font.Normal
                            }
                            Kirigami.Icon {
                                visible: option.isAnswer || option.wasChosen
                                source: option.isAnswer ? "checkmark" : "dialog-cancel"
                                color: option.isAnswer ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }
                        }
                    }
                }
                // why, and where it is in the reading
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: card.graded
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: (card.modelData.explanation ?? "") !== ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: card.modelData.explanation ?? ""
                    }
                    QQC2.Label {
                        id: evidence
                        Layout.fillWidth: true
                        property bool expanded: false
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: expanded ? 1000 : 3
                        elide: Text.ElideRight
                        font.italic: true
                        opacity: 0.7
                        text: i18n("From “%1”: %2", card.modelData.title ?? "", card.modelData.evidence ?? "")
                        TapHandler { onTapped: evidence.expanded = !evidence.expanded }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                    RowLayout {
                        AppButton {
                            kind: "flat"
                            icon.name: "document-open"
                            text: i18n("Read It")
                            onClicked: view.openReading(card.modelData.libraryId, card.modelData.path)
                        }
                        AppButton {
                            kind: "flat"
                            visible: !card.modelData.correct
                            icon.name: "dialog-messages"
                            text: i18n("Ask Your Teacher")
                            onClicked: view.askTeacher(card.modelData.lessonId,
                                                       i18n("I got this question wrong: “%1” I answered “%2”. Can you help me understand?", card.modelData.text,
                                                            card.modelData.chosen >= 0 ? card.modelData.options[card.modelData.chosen] : i18n("nothing")))
                        }
                    }
                }
            }
        }
    }

    // ---- hand it in ----
    RowLayout {
        visible: view.answering && !view.writing
        AppButton {
            kind: "primary"
            icon.name: "checkmark"
            text: i18n("Check My Answers")
            enabled: view.chosen.length > 0 && view.chosen.every(c => c >= 0)
            onClicked: {
                const r = Classroom.submitQuiz(view.quiz.id, view.chosen);
                const q = Object.assign({}, view.quiz);
                q.result = r;
                view.quiz = q;
                view.answering = false;
            }
        }
        QQC2.Label {
            visible: view.chosen.some(c => c < 0)
            textFormat: Text.PlainText
            text: i18np("%1 question left", "%1 questions left", view.chosen.filter(c => c < 0).length)
            opacity: 0.6
        }
    }
}
