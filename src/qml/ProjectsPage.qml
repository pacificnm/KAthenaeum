// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
    title: i18n("Projects")

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "project-development"
        text: i18n("No research projects yet")
        explanation: i18n("Collect articles, passages and chats, and write documents about them.")
    }
}
