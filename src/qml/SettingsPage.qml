// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.coreaddons as KCoreAddons
import KAthenaeum

FormCard.FormCardPage {
    id: page
    title: i18n("Settings")

    Component.onCompleted: Models.refresh()

    // the installed models, with the chosen one first if Ollama does not have it
    function choices(current) {
        const list = Models.installed.slice();
        if (current !== "" && !list.some(m => m === current || m === current + ":latest"))
            list.unshift(current);
        return list;
    }
    function indexOf(list, current) {
        return Math.max(0, list.findIndex(m => m === current || m === current + ":latest"));
    }

    FormCard.FormHeader { title: i18n("Models") }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18n("Ollama")
            description: Models.checking ? i18n("Checking…")
                : !Models.checked ? ""
                : Models.reachable ? i18np("Running at %2 · %1 model installed", "Running at %2 · %1 models installed", Models.installed.length, Models.url)
                : Models.error
            icon.name: Models.reachable ? "dialog-ok" : "dialog-error"
            trailing: QQC2.Button {
                text: i18n("Check Again")
                icon.name: "view-refresh"
                enabled: !Models.checking
                onClicked: Models.refresh()
            }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextFieldDelegate {
            id: urlField
            label: i18n("Ollama address")
            text: Models.url
            placeholderText: "http://localhost:11434"
            onEditingFinished: Models.url = text
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            id: answerBox
            text: i18n("Answer model")
            description: Models.answerModel === Models.defaultAnswerModel
                ? i18n("Writes the answers. %1 is the measured default for laptops.", Models.defaultAnswerModel)
                : i18n("Writes the answers. The measured default is %1.", Models.defaultAnswerModel)
            model: page.choices(Models.answerModel)
            currentIndex: page.indexOf(model, Models.answerModel)
            onActivated: Models.answerModel = currentText
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            text: i18n("Embedding model")
            description: Models.embeddingModel === Models.defaultEmbeddingModel
                ? i18n("Ranks passages by meaning and links sentences to sources.")
                : i18n("Ranks passages by meaning and links sentences to sources. The thresholds for flagging sentences were measured with %1; another model may flag too many or too few.", Models.defaultEmbeddingModel)
            model: page.choices(Models.embeddingModel)
            currentIndex: page.indexOf(model, Models.embeddingModel)
            onActivated: Models.embeddingModel = currentText
        }
        FormCard.FormDelegateSeparator { visible: Models.reachable && (!Models.answerModelInstalled || !Models.embeddingModelInstalled) }
        FormCard.FormTextDelegate {
            visible: Models.reachable && (!Models.answerModelInstalled || !Models.embeddingModelInstalled)
            text: i18n("Not installed in Ollama")
            description: [Models.answerModelInstalled ? "" : "ollama pull " + Models.answerModel,
                          Models.embeddingModelInstalled ? "" : "ollama pull " + Models.embeddingModel].filter(s => s !== "").join("\n")
            icon.name: "dialog-warning"
            textItem.color: Kirigami.Theme.neutralTextColor
        }
        FormCard.FormDelegateSeparator { visible: Models.swapping }
        FormCard.FormTextDelegate {
            visible: Models.swapping
            text: i18n("Ollama keeps only one model loaded")
            description: i18n("It unloads the answer model to check citations and loads it again for the next question, which adds about 2 seconds to every answer. To keep both loaded, run “sudo systemctl edit ollama”, add the two lines below, then “sudo systemctl restart ollama”:\n\n[Service]\nEnvironment=\"OLLAMA_MAX_LOADED_MODELS=2\"")
            icon.name: "dialog-information"
        }
    }

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
