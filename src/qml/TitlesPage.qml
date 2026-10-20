// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import KAthenaeum

// A to Z (one letter at a time), or every title with a prefix ("List of "). Pages of titles load as you scroll.
Kirigami.ScrollablePage {
    id: page

    required property var libraryId
    required property string label
    required property string key        // a letter, or a title prefix
    required property bool isPrefix
    property string current: key
    property int next: 0

    signal openArticle(var libraryId, string path)

    title: Util.pageTitle(label)

    function reload() {
        titlesModel.clear();
        next = 0;
        loadMore();
    }
    function loadMore() {
        if (next < 0)
            return;
        const p = LibraryBrowser.titles(libraryId, current, isPrefix, next, 100);
        for (const e of p.entries)
            titlesModel.append(e);
        next = p.next;
    }
    Component.onCompleted: reload()

    header: QQC2.ToolBar {
        visible: !page.isPrefix
        contentItem: Flow {
            spacing: 0
            Repeater {
                model: ["#"].concat("ABCDEFGHIJKLMNOPQRSTUVWXYZ".split(""))
                delegate: QQC2.ToolButton {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: page.current === modelData
                    implicitWidth: Kirigami.Units.gridUnit * 2
                    onClicked: {
                        page.current = modelData;
                        page.reload();
                    }
                }
            }
        }
    }

    ListView {
        id: list
        model: ListModel { id: titlesModel }
        onAtYEndChanged: if (atYEnd) page.loadMore()

        delegate: QQC2.ItemDelegate {
            required property string title
            required property string path
            width: ListView.view.width
            text: Util.plain(title)
            highlighted: false
            onClicked: page.openArticle(page.libraryId, path)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            text: page.isPrefix ? i18n("No articles") : i18n("No articles under %1", page.current)
        }
    }
}
