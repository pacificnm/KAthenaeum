// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

FormCard.FormCardPage {
    title: i18n("Settings")

    FormCard.FormHeader { title: i18n("Storage") }
    FormCard.FormCard {
        FormCard.FormButtonDelegate {
            text: i18n("Folder for downloaded libraries")
            description: i18n("%1 · %2 free", LibraryManager.folder, KCoreAddons.Format.formatByteSize(LibraryManager.freeSpace, 1))
            icon.name: "folder"
            onClicked: folderDialog.open()
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("Database")
            description: AppState.databasePath
        }
        FormCard.FormDelegateSeparator { visible: AppState.databaseError !== "" }
        FormCard.FormTextDelegate {
            visible: AppState.databaseError !== ""
            text: i18n("The database could not be opened")
            description: AppState.databaseError
            textItem.color: Kirigami.Theme.negativeTextColor
        }
    }

    FolderDialog {
        id: folderDialog
        title: i18n("Folder for Downloaded Libraries")
        currentFolder: "file://" + LibraryManager.folder
        onAccepted: LibraryManager.setFolder(selectedFolder)
    }
}
