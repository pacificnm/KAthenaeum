// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
    title: i18n("Ask")

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "dialog-messages"
        text: i18n("Ask a question")
        explanation: i18n("Answers are written by a local model from your libraries, with every sentence linked to its source. Not built yet.")
    }
}
