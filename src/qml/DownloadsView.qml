// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

QQC2.ScrollView {
    id: view

    function statusText(m) {
        const size = KCoreAddons.Format.formatByteSize;
        switch (m.status) {
        case "queued": return i18n("Starting…");
        case "downloading": {
            let text = m.total > 0 ? i18n("%1 of %2", size(m.done, 1), size(m.total, 1)) : size(m.done, 1);
            if (m.speed > 0)
                text += " · " + i18n("%1/s", size(m.speed, 1));
            if (m.eta >= 0)
                text += " · " + i18n("%1 left", KCoreAddons.Format.formatDuration(m.eta * 1000));
            return text;
        }
        case "verifying": return i18n("Checking the file… (a big library takes a few minutes)");
        case "complete": return i18n("Done: %1", size(m.total, 1));
        case "cancelled": return i18n("Cancelled. Resume continues where it stopped.");
        case "failed": return m.error;
        }
        return m.status;
    }

    ListView {
        id: list
        model: LibraryManager.downloads
        clip: true

        delegate: QQC2.ItemDelegate {
            id: row
            required property var model
            width: ListView.view.width
            down: false

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                    source: row.model.status === "complete" ? "checkmark"
                          : row.model.status === "failed" ? "dialog-error"
                          : row.model.status === "cancelled" ? "media-playback-pause"
                          : "download"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        level: 4
                        text: row.model.title
                        elide: Text.ElideRight
                    }
                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        visible: row.model.active
                        indeterminate: row.model.status === "verifying" || row.model.status === "queued"
                        value: row.model.progress
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: view.statusText(row.model)
                        color: row.model.status === "failed" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                        opacity: row.model.status === "failed" ? 1 : 0.7
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: row.model.filename
                        font: Kirigami.Theme.smallFont
                        opacity: 0.5
                        elide: Text.ElideMiddle
                    }
                }

                QQC2.Button {
                    Layout.alignment: Qt.AlignTop
                    visible: row.model.status === "downloading" || row.model.status === "queued"
                    text: i18n("Cancel")
                    icon.name: "dialog-cancel"
                    onClicked: LibraryManager.cancelDownload(row.model.downloadId)
                }
                QQC2.Button {
                    Layout.alignment: Qt.AlignTop
                    visible: row.model.status === "failed" || row.model.status === "cancelled"
                    text: i18n("Resume")
                    icon.name: "media-playback-start"
                    onClicked: LibraryManager.resumeDownload(row.model.downloadId)
                }
                QQC2.ToolButton {
                    Layout.alignment: Qt.AlignTop
                    visible: !row.model.active
                    icon.name: "edit-clear-history"
                    display: QQC2.AbstractButton.IconOnly
                    text: row.model.status === "complete" ? i18n("Clear from the list") : i18n("Remove, and delete the partial file")
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: LibraryManager.removeDownload(row.model.downloadId)
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "download"
            text: i18n("No downloads")
            explanation: i18n("Libraries you download from the catalog appear here while they arrive.")
        }
    }
}
