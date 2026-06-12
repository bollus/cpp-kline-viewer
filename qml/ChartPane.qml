import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AlgoHub

// One chart pane: its own AppController + ChartItem, a compact header with an
// independent timeframe selector and (when more than one pane) a close button.
// Registers with the Workspace so crosshair/viewport stay in sync across panes.
Item {
    id: pane
    property int slot: 0
    readonly property bool isActive: controller.activeIndex === pane.slot

    property var timeframes: ["1M", "5M", "15M", "30M", "1H", "2H", "4H", "6H", "12H", "1D", "1W"]

    AppController {
        id: vc
        chart: chartItem
    }

    Component.onCompleted: controller.registerView(pane.slot, vc)
    Component.onDestruction: controller.unregisterView(pane.slot)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Pane header
        Rectangle {
            Layout.fillWidth: true
            height: 26
            color: pane.isActive ? theme.bgPanel2 : theme.bgToolbar

            MouseArea {
                anchors.fill: parent
                onPressed: controller.setActiveIndex(pane.slot)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 6

                // timeframe selector
                Button {
                    id: tfButton
                    flat: true
                    implicitHeight: 20
                    text: vc.timeframeLabel + " ▾"
                    onClicked: tfMenu.open()
                    contentItem: Text {
                        text: tfButton.text
                        color: pane.isActive ? theme.brandBlue : theme.textSecondary
                        font.pixelSize: 11
                        font.bold: pane.isActive
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: 4; color: tfButton.hovered ? theme.bgHover : "transparent" }
                    Menu {
                        id: tfMenu
                        Repeater {
                            model: pane.timeframes
                            MenuItem {
                                text: modelData
                                onTriggered: { controller.setActiveIndex(pane.slot); vc.setTimeframe(modelData) }
                            }
                        }
                    }
                }

                Text {
                    text: controller.symbol
                    color: theme.textMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                IconButton {
                    visible: controller.viewCount > 1
                    iconName: "close"
                    px: 12
                    tip: "关闭该视图"
                    onClicked: controller.removeView()
                }
            }
        }

        // Chart surface with an active-pane highlight border
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.bgApp
            border.width: pane.isActive ? 1 : 0
            border.color: theme.brandBlue

            ChartItem {
                id: chartItem
                anchors.fill: parent
                anchors.margins: pane.isActive ? 1 : 0
            }
        }
    }
}
