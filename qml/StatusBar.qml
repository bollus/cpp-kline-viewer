import QtQuick
import QtQuick.Layouts

// Bottom status bar: connection state on the left; backend / realtime info on
// the right (relocated here from the old top-right indicator).
Rectangle {
    id: root
    height: 28
    color: theme.dark ? "#080D13" : theme.bgToolbar

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 16

        Rectangle {
            width: 8; height: 8; radius: 4
            color: controller.connectionLive ? theme.green : theme.textMuted
            border.color: controller.connectionLive ? Qt.rgba(0.18, 0.90, 0.65, 0.38) : "transparent"
        }
        Text {
            text: controller.connectionStatus
            color: controller.connectionLive ? theme.green : theme.textSecondary
            font.pixelSize: 11
        }

        Item { Layout.fillWidth: true }

        Text {
            visible: controller.backendConfigured
            text: "后端 " + controller.backendBase
            color: theme.textMuted
            font.pixelSize: 11
        }
        Text {
            text: controller.realtimeEnabled ? "实时 开启" : "实时 关闭"
            color: controller.realtimeEnabled ? theme.green : theme.textMuted
            font.pixelSize: 11
        }
    }
}
