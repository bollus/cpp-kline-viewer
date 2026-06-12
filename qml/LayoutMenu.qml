import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Popup of layout presets (TradingView-style) shown from the nav bar.
Popup {
    id: root
    padding: 12
    modal: false
    background: Rectangle { color: theme.bgElevated; border.color: theme.borderSubtle; radius: 10 }

    property var presets: ["1", "2h", "2v", "3h", "3lr", "4"]

    function previewFor(id) {
        switch (id) {
        case "2h": return p2h
        case "2v": return p2v
        case "3h": return p3h
        case "3lr": return p3lr
        case "4": return p4
        default: return p1
        }
    }

    component Tile: Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: 2
        color: theme.textMuted
        opacity: 0.55
    }

    Component { id: p1; Tile {} }
    Component { id: p2h; RowLayout { spacing: 3; Tile {} Tile {} } }
    Component { id: p2v; ColumnLayout { spacing: 3; Tile {} Tile {} } }
    Component { id: p3h; RowLayout { spacing: 3; Tile {} Tile {} Tile {} } }
    Component {
        id: p3lr
        RowLayout { spacing: 3; Tile {} ColumnLayout { spacing: 3; Tile {} Tile {} } }
    }
    Component {
        id: p4
        ColumnLayout {
            spacing: 3
            RowLayout { spacing: 3; Tile {} Tile {} }
            RowLayout { spacing: 3; Tile {} Tile {} }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        Text { text: "视图布局"; color: theme.textPrimary; font.pixelSize: 13; font.bold: true }
        Grid {
            columns: 3
            columnSpacing: 10
            rowSpacing: 10
            Repeater {
                model: root.presets
                delegate: Rectangle {
                    width: 62
                    height: 48
                    radius: 6
                    color: controller.layoutId === modelData ? theme.brandBlueSoft : "transparent"
                    border.width: controller.layoutId === modelData ? 2 : 1
                    border.color: controller.layoutId === modelData ? theme.brandBlue : theme.borderSubtle
                    Loader {
                        anchors.fill: parent
                        anchors.margins: 8
                        sourceComponent: root.previewFor(modelData)
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { controller.layoutId = modelData; root.close() }
                    }
                }
            }
        }
    }
}
