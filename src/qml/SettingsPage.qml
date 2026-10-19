// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import KAthenaeum

FormCard.FormCardPage {
    title: i18n("Settings")

    FormCard.FormHeader { title: i18n("Storage") }
    FormCard.FormCard {
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
}
