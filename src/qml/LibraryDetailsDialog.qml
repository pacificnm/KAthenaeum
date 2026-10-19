// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

Kirigami.Dialog {
    id: dialog

    property var info: ({})

    function show(id) {
        info = LibraryManager.details(id);
        open();
    }

    title: info.title ?? ""
    standardButtons: Kirigami.Dialog.Close
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing

    Kirigami.FormLayout {
        QQC2.Label {
            Kirigami.FormData.label: i18n("Description:")
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: dialog.info.description ?? ""
            wrapMode: Text.Wrap
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Contents:")
            text: Util.flavourLabel(dialog.info.flavour ?? "")
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Entries:")
            text: Util.articles(dialog.info.articleCount ?? 0) + " " + i18n("(including redirects)")
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Full-text index:")
            text: dialog.info.hasFulltext ? i18n("Yes") : i18n("No: articles can only be found by title")
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Size:")
            text: KCoreAddons.Format.formatByteSize(dialog.info.size ?? 0, 1)
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Date:")
            text: dialog.info.date ?? ""
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Language:")
            text: dialog.info.language ?? ""
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Source:")
            text: [dialog.info.creator, dialog.info.publisher].filter(s => s).join(", ")
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Made with:")
            text: dialog.info.scraper ?? ""
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("File:")
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: dialog.info.path ?? ""
            wrapMode: Text.WrapAnywhere
            color: dialog.info.missing ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
        }
        QQC2.Label {
            Kirigami.FormData.label: i18n("Added:")
            text: dialog.info.addedAt ? dialog.info.addedAt.toLocaleString(Qt.locale(), Locale.ShortFormat) : ""
        }
    }
}
