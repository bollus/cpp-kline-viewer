import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Toolbar above the chart: timeframe selector, indicators, replay toggle and
// sidebar collapse.
Rectangle {
    id: root
    height: 44
    color: theme.dark ? Qt.rgba(10/255, 16/255, 23/255, 0.92) : theme.bgToolbar

    signal toggleSidebar()
    signal openIndicators()
    signal openCustomIndicators()

    property var primaryTimeframes: ["1M", "5M", "15M", "1H", "4H", "1D"]
    property var moreTimeframes: ["1S", "5S", "30S", "2M", "3M", "10M", "30M", "2H", "6H", "12H", "1W", "1MO"]

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 7

        Repeater {
            model: root.primaryTimeframes
            Button {
                text: modelData
                flat: true
                checkable: true
                checked: controller.timeframe === modelData
                implicitHeight: 30
                implicitWidth: 42
                onClicked: controller.setTimeframe(modelData)
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? (theme.dark ? "#06110F" : "white") : theme.textSecondary
                    font.pixelSize: 12
                    font.bold: parent.checked
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 10
                    color: parent.checked ? theme.brandBlue : (parent.hovered ? theme.bgHover : "transparent")
                    border.color: parent.checked ? theme.brandBlueHover : (parent.hovered ? theme.borderSubtle : "transparent")
                }
            }
        }

        Button {
            text: "更多 ▾"
            flat: true
            implicitHeight: 30
            onClicked: moreMenu.open()
            contentItem: Text {
                text: parent.text; color: theme.textSecondary; font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: 10; color: parent.hovered ? theme.bgHover : "transparent"; border.color: parent.hovered ? theme.borderSubtle : "transparent" }
            Menu {
                id: moreMenu
                Repeater {
                    model: root.moreTimeframes
                    MenuItem {
                        text: modelData
                        onTriggered: controller.setTimeframe(modelData)
                    }
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: theme.borderSubtle }

        Button {
            text: "指标"
            flat: true
            implicitHeight: 30
            onClicked: root.openIndicators()
            contentItem: Text { text: parent.text; color: theme.textSecondary; font.pixelSize: 12 }
            background: Rectangle { radius: 10; color: parent.hovered ? theme.bgHover : "transparent"; border.color: parent.hovered ? theme.borderSubtle : "transparent" }
        }

        Item { Layout.fillWidth: true }

        ComboField {
            id: timeZoneBox
            implicitWidth: 132
            model: controller.timeZoneOptions
            currentIndex: controller.timeZoneIndex
            displayText: controller.timeZoneLabel
            ToolTip.visible: hovered
            ToolTip.text: "X轴时间时区"
            onActivated: controller.timeZoneId = currentText
        }

        Button {
            text: controller.replayActive ? "退出回放" : "K线回放"
            flat: true
            checkable: true
            checked: controller.replayActive
            implicitHeight: 30
            onClicked: controller.replayActive = !controller.replayActive
            contentItem: Text {
                text: parent.text
                color: parent.checked ? (theme.dark ? "#06110F" : "white") : theme.textSecondary
                font.pixelSize: 12
            }
            background: Rectangle {
                radius: 10
                color: parent.checked ? theme.brandBlue : (parent.hovered ? theme.bgHover : "transparent")
                border.color: parent.checked ? theme.brandBlueHover : (parent.hovered ? theme.borderSubtle : "transparent")
            }
        }

        IconButton {
            iconName: "chevron-right"
            tip: "折叠侧栏"
            onClicked: root.toggleSidebar()
        }
    }
}
