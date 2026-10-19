// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
    title: i18n("Chats")

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "view-history"
        text: i18n("No chats yet")
        explanation: i18n("Your saved conversations will appear here.")
    }
}
