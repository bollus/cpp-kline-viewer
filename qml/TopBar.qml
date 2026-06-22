import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Top navigation bar: logo, symbol search, market summary, action icons,
// window controls. Distinctly elevated from the body via a stronger fill and
// a bottom divider, per the design feedback.
Rectangle {
    id: root
    height: 60
    color: "transparent"

    signal requestMinimize()
    signal requestMaximize()
    signal requestClose()
    signal openSettings()
    signal openLayout()

    // Bottom divider to separate the bar from the chart area.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.dark ? "#0F1923" : "#FFFFFF" }
            GradientStop { position: 1.0; color: theme.bgToolbar }
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: theme.borderSubtle
    }

    // Window drag on empty areas of the bar.
    MouseArea {
        anchors.fill: parent
        onPressed: Window.window.startSystemMove()
        onDoubleClicked: root.requestMaximize()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 10
        spacing: 12

        Rectangle {
            width: 34; height: 34; radius: 11
            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.brandBlueHover }
                GradientStop { position: 1.0; color: theme.brandBlue }
            }
            border.color: "#38FFFFFF"
            Text {
                anchors.centerIn: parent
                text: "Q"
                color: theme.dark ? "#06110F" : "white"
                font.pixelSize: 17
                font.bold: true
            }
        }
        ColumnLayout {
            spacing: -1
            Text {
                text: "Q4J Terminal"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }
            Text {
                text: "strategy replay desk"
                color: theme.textMuted
                font.pixelSize: 10
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
            }
        }

        // Symbol search
        Rectangle {
            Layout.preferredWidth: 260
            height: 36
            radius: 12
            color: theme.dark ? "#101A24" : theme.bgPanel2
            border.color: symbolInput.activeFocus ? theme.borderStrong : theme.borderSubtle
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 6
                Image {
                    width: 15; height: 15
                    sourceSize.width: 15; sourceSize.height: 15
                    source: theme.icon("search", "muted", 15)
                }
                TextField {
                    id: symbolInput
                    Layout.fillWidth: true
                    text: controller.symbol
                    placeholderText: "搜索品种 / 例：XAUUSD, BTCUSDT"
                    color: theme.textPrimary
                    placeholderTextColor: theme.textMuted
                    background: null
                    font.pixelSize: 13
                    onEditingFinished: { controller.symbol = text; controller.refresh() }
                    onAccepted: { controller.symbol = text; controller.refresh() }
                }
            }
        }

        // Market summary
        ColumnLayout {
            RowLayout {
                spacing: 8  // 元素间距，可调节

                Rectangle {
                    radius: 12
                    color: controller.marketUp ? Qt.rgba(0.18, 0.90, 0.65, 0.10) : Qt.rgba(1.0, 0.36, 0.45, 0.10)
                    border.color: controller.marketUp ? Qt.rgba(0.18, 0.90, 0.65, 0.22) : Qt.rgba(1.0, 0.36, 0.45, 0.22)
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: marketRow.implicitWidth + 24
                    RowLayout {
                        id: marketRow
                        anchors.centerIn: parent
                        spacing: 9
                        Text {
                            text: controller.symbol
                            color: theme.textSecondary
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Text {
                            text: controller.marketPrice
                            color: controller.marketUp ? theme.green : theme.red
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text {
                            text: controller.marketChange
                            color: controller.marketUp ? theme.green : theme.red
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Hover OHLC readout
        Text {
            visible: controller.ohlcText.length > 0
            text: controller.ohlcText
            color: theme.textSecondary
            font.pixelSize: 12
        }

        IconButton { id: layoutBtn; iconName: "layout"; tip: "视图布局"; onClicked: root.openLayout() }
        IconButton { iconName: "refresh"; tip: "刷新"; onClicked: controller.refresh() }
        IconButton { iconName: "bell"; tip: "通知" }
        IconButton {
            iconName: theme.dark ? "eye" : "settings"
            tip: theme.dark ? "切换浅色" : "切换深色"
            onClicked: { theme.dark = !theme.dark; controller.dark = theme.dark }
        }
        IconButton { iconName: "settings"; tip: "设置"; onClicked: root.openSettings() }

        // Window controls
        Rectangle { width: 1; height: 24; color: theme.borderSubtle }
        IconButton { iconName: "minus"; px: 14; onClicked: root.requestMinimize() }
        IconButton {
            iconName: Window.window && Window.window.visibility === Window.Maximized ? "restore" : "square"
            px: 13
            onClicked: root.requestMaximize()
        }
        IconButton {
            iconName: "close"; px: 14
            hoverBg: theme.red
            onClicked: root.requestClose()
        }
    }
}
