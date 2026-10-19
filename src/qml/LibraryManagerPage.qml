// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Page {
    title: i18n("Library Manager")

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "folder-download"
        text: i18n("Library Manager")
        explanation: i18n("Browse the Kiwix catalog, download libraries and keep them up to date. Not built yet.")
    }
}
