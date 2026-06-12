import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Left annotation toolbar. Tools are grouped; a group shows the last-used tool
// and exposes its siblings through a chevron-triggered popover so the column
// never needs to scroll.
Rectangle {
    id: root
    width: 46
    color: theme.bgToolbar

    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: theme.borderSubtle
    }

    // A single tool group: a main icon plus an optional list of variants.
    component ToolGroup: Item {
        id: group
        width: 40
        height: 38
        // variants: list of { tool, icon, tip, enabled }
        property var variants: []
        property int currentIndex: 0
        property var current: variants.length > 0 ? variants[currentIndex] : ({})
        property bool selected: controller.annotationTool === (current.tool !== undefined ? current.tool : -99)

        IconButton {
            anchors.fill: parent
            anchors.margins: 3
            icon: group.current.icon !== undefined ? group.current.icon : "cursor"
            tip: group.current.tip !== undefined ? group.current.tip : ""
            active: group.selected
            enabled: group.current.enabled !== false
            onClicked: controller.annotationTool = group.current.tool
        }

        // Chevron to open variants
        Image {
            visible: group.variants.length > 1
            anchors { right: parent.right; bottom: parent.bottom; rightMargin: 1; bottomMargin: 1 }
            width: 8; height: 8
            sourceSize.width: 8; sourceSize.height: 8
            source: theme.icon("chevron-right", "muted", 8)
            MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: popover.open() }
        }

        Popup {
            id: popover
            x: parent.width + 4
            y: 0
            padding: 4
            background: Rectangle {
                color: theme.bgElevated
                border.color: theme.borderSubtle
                radius: 8
            }
            contentItem: RowLayout {
                spacing: 2
                Repeater {
                    model: group.variants
                    IconButton {
                        icon: modelData.icon
                        tip: modelData.tip
                        enabled: modelData.enabled !== false
                        active: controller.annotationTool === modelData.tool
                        onClicked: {
                            group.currentIndex = index
                            controller.annotationTool = modelData.tool
                            popover.close()
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4

        ToolGroup { variants: [{ tool: 0, icon: "cursor", tip: "光标" }] }
        ToolGroup { variants: [{ tool: -1, icon: "crosshair", tip: "十字光标", enabled: false }] }

        Rectangle { Layout.preferredWidth: 24; Layout.preferredHeight: 1; Layout.alignment: Qt.AlignHCenter; color: theme.borderSubtle }

        ToolGroup {
            variants: [
                { tool: 3, icon: "trend-line", tip: "趋势线" },
                { tool: 4, icon: "h-line", tip: "水平线" },
                { tool: 5, icon: "v-line", tip: "垂直线" },
                { tool: 6, icon: "brush", tip: "折线" }
            ]
        }
        ToolGroup { variants: [{ tool: 7, icon: "rectangle", tip: "矩形" }] }
        ToolGroup {
            variants: [
                { tool: -1, icon: "fibonacci", tip: "斐波那契（即将推出）", enabled: false }
            ]
        }

        Rectangle { Layout.preferredWidth: 24; Layout.preferredHeight: 1; Layout.alignment: Qt.AlignHCenter; color: theme.borderSubtle }

        ToolGroup {
            variants: [
                { tool: 1, icon: "long", tip: "做多仓位" },
                { tool: 2, icon: "short", tip: "做空仓位" }
            ]
        }
        ToolGroup { variants: [{ tool: -1, icon: "text", tip: "文本（即将推出）", enabled: false }] }
        ToolGroup { variants: [{ tool: -1, icon: "ruler", tip: "测量（即将推出）", enabled: false }] }
    }

    ColumnLayout {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4
        IconButton {
            icon: "magnet"
            tip: "磁吸"
            active: controller.magnetEnabled
            onClicked: controller.magnetEnabled = !controller.magnetEnabled
        }
    }
}
