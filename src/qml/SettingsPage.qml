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
                : Models.error
            icon.name: !Models.checked || Models.checking ? "view-refresh" : Models.reachable ? "dialog-ok" : "dialog-error"
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

    FormCard.FormHeader { title: i18n("Maps") }
    FormCard.FormCard {
        FormCard.FormButtonDelegate {
            text: i18n("Add a region…")
            description: i18n("Maps are downloaded once and work offline. Click a place on the map to read about it in your libraries.")
            icon.name: "list-add"
            onClicked: applicationWindow().pageStack.push(Qt.resolvedUrl("MapRegionsPage.qml"))
        }
        Repeater {
            model: MapManager.regions
            delegate: ColumnLayout {
                id: region
                required property var modelData
                readonly property bool active: modelData.status === "queued" || modelData.status === "downloading" || modelData.status === "indexing"
                Layout.fillWidth: true
                spacing: 0

                FormCard.FormDelegateSeparator {}
                FormCard.FormTextDelegate {
                    Layout.fillWidth: true
                    text: region.modelData.parentName !== "" ? i18nc("region, in its parent", "%1, %2", region.modelData.name, region.modelData.parentName) : region.modelData.name
                    icon.name: region.modelData.status === "failed" ? "dialog-error" : region.active ? "folder-download" : "map-flat"
                    description: page.regionStatus(region.modelData)
                    textItem.textFormat: Text.PlainText
                    descriptionItem.textFormat: Text.PlainText
                    descriptionItem.color: region.modelData.status === "failed" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                    trailing: RowLayout {
                        QQC2.Button {
                            visible: region.active
                            text: i18n("Cancel")
                            icon.name: "dialog-cancel"
                            onClicked: MapManager.cancel(region.modelData.id)
                        }
                        QQC2.Button {
                            visible: region.modelData.status === "failed" || region.modelData.status === "cancelled"
                            text: i18n("Retry")
                            icon.name: "view-refresh"
                            onClicked: MapManager.retry(region.modelData.id)
                        }
                        QQC2.Button {
                            visible: region.modelData.status === "complete"
                            text: i18n("Update")
                            icon.name: "update-none"
                            QQC2.ToolTip.text: i18n("Download it again from the newest map (OpenStreetMap changes every day)")
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: MapManager.retry(region.modelData.id)
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-delete"
                            display: QQC2.AbstractButton.IconOnly
                            text: i18n("Remove…")
                            QQC2.ToolTip.text: text
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: removeMapDialog.ask(region.modelData)
                        }
                    }
                }
                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    visible: region.active && region.modelData.status !== "queued"
                    indeterminate: region.modelData.total <= 0
                    value: region.modelData.total > 0 ? region.modelData.done / region.modelData.total : 0
                }
            }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18n("Folder for maps")
            description: i18n("%1 · %2 free", MapManager.folder, KCoreAddons.Format.formatByteSize(MapManager.freeSpace, 1))
            descriptionItem.textFormat: Text.PlainText
        }
    }

    function regionStatus(r) {
        const size = KCoreAddons.Format.formatByteSize(r.size, 1);
        const detail = r.maxZoom >= 15 ? i18n("streets") : r.maxZoom >= 13 ? i18n("roads and towns") : i18n("overview");
        switch (r.status) {
        case "queued":
            return i18n("Waiting to download");
        case "downloading":
            if (r.step === "places")
                return r.total > 0 ? i18n("Downloading OpenStreetMap data: %1 of %2", KCoreAddons.Format.formatByteSize(r.done, 1), KCoreAddons.Format.formatByteSize(r.total, 1))
                                   : i18n("Downloading OpenStreetMap data…");
            return r.total > 0 ? i18n("Downloading the map: %1 of %2", KCoreAddons.Format.formatByteSize(r.done, 1), KCoreAddons.Format.formatByteSize(r.total, 1))
                               : i18n("Finding the region's map tiles…");
        case "indexing":
            return i18n("Finding places with Wikipedia articles…");
        case "failed":
            return r.error;
        case "cancelled":
            return r.hasMap ? i18n("Update cancelled · the earlier map is kept") : i18n("Cancelled");
        default:
            return r.region === "world" ? i18n("%1 · %2 · map of %3", size, detail, Util.buildDate(r.build))
                                        : i18np("%2 · %3 · %1 place · map of %4", "%2 · %3 · %1 places · map of %4", r.places, size, detail, Util.buildDate(r.build));
        }
    }

    Kirigami.PromptDialog {
        id: removeMapDialog
        property var region: ({})
        function ask(r) {
            region = r;
            open();
        }
        title: i18n("Remove the map of %1?", Util.plain(region.name ?? ""))
        subtitle: i18n("Its map file (%1) and its places are deleted. You can download it again later.", KCoreAddons.Format.formatByteSize(region.size ?? 0, 1))
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Remove")
                icon.name: "edit-delete"
                onTriggered: {
                    MapManager.remove(removeMapDialog.region.id);
                    removeMapDialog.close();
                }
            },
            Kirigami.Action { text: i18n("Cancel"); icon.name: "dialog-cancel"; onTriggered: removeMapDialog.close() }
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
