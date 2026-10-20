// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
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
                : Util.plain(Models.error)
            icon.name: !Models.checked || Models.checking ? "view-refresh" : Models.reachable ? "dialog-ok" : "dialog-error"
            trailing: AppButton {
                kind: "flat"
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

    FormCard.FormHeader { title: i18n("Library Updates") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Check for newer versions once a week")
            description: i18n("Kiwix publishes new versions of its libraries every few months. KAthenaeum looks in the catalog and tells you; it downloads nothing unless you choose to.")
            checked: LibraryManager.autoCheckUpdates
            onToggled: LibraryManager.autoCheckUpdates = checked
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: LibraryManager.updateCount > 0 ? i18np("%1 update available", "%1 updates available", LibraryManager.updateCount) : i18n("Up to date")
            description: LibraryManager.checkingUpdates ? i18n("Checking…")
                : isNaN(LibraryManager.lastUpdateCheck.getTime()) ? i18n("Not checked yet")
                : i18n("Last checked %1", LibraryManager.lastUpdateCheck.toLocaleString(Qt.locale(), Locale.ShortFormat))
            icon.name: LibraryManager.updateCount > 0 ? "update-medium" : "checkmark"
            trailing: AppButton {
                kind: "flat"
                text: i18n("Check Now")
                icon.name: "view-refresh"
                enabled: !LibraryManager.checkingUpdates
                onClicked: LibraryManager.checkForUpdates()
            }
        }
    }

    FormCard.FormHeader { title: i18n("Reading") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Use the whole window for articles")
            description: i18n("Off: articles are set in a column of comfortable reading width, centred in the window.")
            checked: LibraryBrowser.fullWidth
            onToggled: LibraryBrowser.fullWidth = checked
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormComboBoxDelegate {
            text: i18n("Citation style")
            description: i18n("How Cite, a project's Bibliography and exported documents cite articles.")
            textRole: "label"
            valueRole: "style"
            model: LibraryBrowser.citationStyles
            currentIndex: Math.max(0, LibraryBrowser.citationStyles.findIndex(s => s.style === LibraryBrowser.citationStyle))
            onActivated: LibraryBrowser.citationStyle = currentValue
        }
    }

    FormCard.FormHeader { title: i18n("Network") }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Use the internet")
            description: i18n("For the library catalog and downloads, update checks and the map. Reading, search, Ask, the Classroom, notes and projects never need it.")
            checked: Connectivity.allowed
            onToggled: Connectivity.allowed = checked
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("Connection")
            description: !Connectivity.allowed ? i18n("Not used: KAthenaeum is working offline")
                       : Connectivity.reachable ? i18n("Online") : i18n("No connection: KAthenaeum is working offline until there is one")
            icon.name: Connectivity.online ? "network-connect" : "network-disconnect"
        }
    }

    FormCard.FormHeader { title: i18n("Map") }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18n("Kept on this computer")
            description: i18n("%1 of map, shared with the Marble application. Areas you view are kept as you go; on the map, Keep This Area Offline keeps a whole town or city.",
                              KCoreAddons.Format.formatByteSize(MapManager.cacheBytes, 1))
            Component.onCompleted: MapManager.measureCache()
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            text: i18n("Forget the Kept Map…")
            icon.name: "edit-clear-history"
            enabled: MapManager.cacheBytes > 0
            onClicked: clearMapDialog.open()
        }
    }

    Kirigami.PromptDialog {
        id: clearMapDialog
        title: i18n("Forget the kept map?")
        subtitle: i18n("The %1 of map kept on this computer is deleted, for Marble too. Areas are fetched again when viewed online.",
                       KCoreAddons.Format.formatByteSize(MapManager.cacheBytes, 1))
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Forget")
                icon.name: "edit-clear-history"
                onTriggered: {
                    MapManager.clearCache();
                    clearMapDialog.close();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: clearMapDialog.close() }
        ]
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
            description: Util.plain(AppState.databasePath)
        }
        FormCard.FormDelegateSeparator { visible: AppState.databaseError !== "" }
        FormCard.FormTextDelegate {
            visible: AppState.databaseError !== ""
            text: i18n("The database could not be opened")
            description: Util.plain(AppState.databaseError)
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
