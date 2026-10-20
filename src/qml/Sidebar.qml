// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The app's sidebar: who it is, a search box, and the places grouped, the current one marked. Beside the pages on a
// wide window, over them (opened from the header) on a narrow one.
Kirigami.OverlayDrawer {
    id: drawer

    // the section shown ("libraries", "map", "classroom", "ask", "chats", "projects", "notes", "manager", "settings", "about", "search")
    property string current
    property int activeDownloads: 0
    property int updates: 0

    signal navigate(string section)
    signal search(string text)
    signal help()

    function focusSearch() {
        if (modal)
            open();
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    edge: Qt.application.layoutDirection === Qt.RightToLeft ? Qt.RightEdge : Qt.LeftEdge
    width: Kirigami.Units.gridUnit * 13
    handleVisible: modal
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    component Place: QQC2.ItemDelegate {
        id: place
        required property string section
        property int count: 0
        readonly property bool here: drawer.current === section

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.smallSpacing
        Layout.rightMargin: Kirigami.Units.smallSpacing
        topPadding: Kirigami.Units.mediumSpacing
        bottomPadding: Kirigami.Units.mediumSpacing
        leftPadding: Kirigami.Units.largeSpacing
        rightPadding: Kirigami.Units.largeSpacing
        Accessible.role: Accessible.PageTab
        Accessible.checked: here
        onClicked: {
            drawer.navigate(section);
            if (drawer.modal)
                drawer.close();
        }
        background: Rectangle {
            radius: Kirigami.Units.cornerRadius
            color: place.here ? Qt.alpha(Kirigami.Theme.highlightColor, place.hovered ? 0.3 : 0.22)
                 : place.hovered || place.visualFocus ? Qt.alpha(Kirigami.Theme.textColor, 0.07) : "transparent"
            Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }
        }
        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon {
                source: place.icon.name
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                color: place.here ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                isMask: place.here
            }
            QQC2.Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: place.text
                elide: Text.ElideRight
                font.weight: place.here ? Font.DemiBold : Font.Normal
            }
            // a count beside it (downloads running)
            Rectangle {
                visible: place.count > 0
                Layout.preferredHeight: countLabel.implicitHeight + Kirigami.Units.smallSpacing
                Layout.preferredWidth: Math.max(Layout.preferredHeight, countLabel.implicitWidth + Kirigami.Units.largeSpacing)
                radius: height / 2
                color: Kirigami.Theme.highlightColor
                QQC2.Label {
                    id: countLabel
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: place.count
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.highlightedTextColor
                }
            }
        }
    }

    component Group: QQC2.Label {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
        Layout.topMargin: Kirigami.Units.largeSpacing
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        textFormat: Text.PlainText
        font: Kirigami.Theme.smallFont
        opacity: 0.6
    }

    contentItem: RowLayout {
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Kirigami.Units.smallSpacing / 2

            // the search box, in a bar as tall as the page's header beside it
            QQC2.ToolBar {
                Layout.fillWidth: true
                Layout.preferredHeight: applicationWindow().pageStack.globalToolBar.preferredHeight
                position: QQC2.ToolBar.Header
                leftPadding: Kirigami.Units.smallSpacing
                rightPadding: Kirigami.Units.smallSpacing
                contentItem: Kirigami.SearchField {
                    id: searchField
                    placeholderText: i18n("Search libraries…")
                    autoAccept: false
                    onAccepted: {
                        drawer.search(text.trim());
                        text = "";
                        if (drawer.modal)
                            drawer.close();
                    }
                }
            }

            Group { text: i18n("Read") }
            Place { section: "libraries"; text: i18n("Libraries"); icon.name: "bookmarks" }
            Place { section: "search"; text: i18n("Search"); icon.name: "search" }
            Place { section: "map"; text: i18n("Map"); icon.name: "map-flat" }

            Group { text: i18n("Learn") }
            Place { section: "classroom"; text: i18n("Classroom"); icon.name: "applications-education-symbolic" }

            Group { text: i18n("Research") }
            Place { section: "ask"; text: i18n("Ask"); icon.name: "dialog-messages" }
            Place { section: "chats"; text: i18n("Chats"); icon.name: "view-history" }
            Place { section: "projects"; text: i18n("Projects"); icon.name: "folder-bookmark" }
            Place { section: "notes"; text: i18n("Notes"); icon.name: "draw-highlight" }

            Item { Layout.fillHeight: true }

            Kirigami.Separator { Layout.fillWidth: true }
            Place { section: "manager"; text: i18n("Library Manager"); icon.name: "folder-download"; count: drawer.activeDownloads > 0 ? drawer.activeDownloads : drawer.updates; Layout.topMargin: Kirigami.Units.smallSpacing }
            Place { section: "settings"; text: i18n("Settings"); icon.name: "settings-configure" }
            // the handbook, in KDE's Help Center
            QQC2.ItemDelegate {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                topPadding: Kirigami.Units.mediumSpacing
                bottomPadding: Kirigami.Units.mediumSpacing
                leftPadding: Kirigami.Units.largeSpacing
                text: i18n("Handbook")
                icon.name: "help-contents"
                onClicked: drawer.help()
                background: Rectangle {
                    radius: Kirigami.Units.cornerRadius
                    color: parent.hovered ? Qt.alpha(Kirigami.Theme.textColor, 0.07) : "transparent"
                }
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    Kirigami.Icon {
                        source: "help-contents"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: i18n("Handbook")
                    }
                }
            }
            Place { section: "about"; text: i18n("About KAthenaeum"); icon.name: "help-about"; Layout.bottomMargin: Kirigami.Units.smallSpacing }
        }
        Kirigami.Separator {
            Layout.fillHeight: true
            visible: !drawer.modal
        }
    }
}
