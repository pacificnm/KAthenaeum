// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
    title: i18n("Libraries")

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "bookmarks"
        text: i18n("No libraries yet")
        explanation: i18n("Download a Wikipedia library from the Library Manager to start reading and asking questions.")
    }
}
