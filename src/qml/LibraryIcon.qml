// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A library's 48x48 illustration, or a generic book icon while it loads or when there is none
Item {
    property alias source: image.source

    Layout.preferredWidth: Kirigami.Units.iconSizes.large
    Layout.preferredHeight: Kirigami.Units.iconSizes.large

    Image {
        id: image
        anchors.fill: parent
        sourceSize.width: Math.max(48, width)
        sourceSize.height: Math.max(48, height)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
    Kirigami.Icon {
        anchors.fill: parent
        source: "bookmarks"
        visible: image.status !== Image.Ready
    }
}
