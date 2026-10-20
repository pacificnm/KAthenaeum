// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import KAthenaeum

// A unit's test: questions on each of its lessons, from their readings.
Kirigami.ScrollablePage {
    id: page

    required property var unitId
    property string unitTitle
    property string courseTitle

    signal openArticle(var libraryId, string path)
    signal askTeacher(var lessonId, string question)

    title: i18n("Test: %1", Util.pageTitle(unitTitle))

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            spacing: 0
            QQC2.Label {
                textFormat: Text.PlainText
                text: page.courseTitle
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                textFormat: Text.PlainText
                text: i18n("Unit test: %1", page.unitTitle)
                wrapMode: Text.Wrap
            }
        }
        QuizView {
            Layout.fillWidth: true
            kind: "unit"
            targetId: page.unitId
            onOpenReading: (libraryId, path) => page.openArticle(libraryId, path)
            onAskTeacher: (lessonId, question) => page.askTeacher(lessonId, question)
        }
    }
}
