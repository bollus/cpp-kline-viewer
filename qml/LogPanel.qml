import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Collapsible service log panel with a header row (Time / Level / Module /
// Info) and a row-per-entry list backed by LogModel.
Rectangle {
    id: root
    color: theme.bgPanel

    property bool collapsed: false
    signal requestExport()

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: theme.borderSubtle
    }

    function levelColor(level) {
        if (level === "ERROR") return theme.red
        if (level === "WARN") return theme.orange
        if (level === "DEBUG") return theme.textMuted
        return theme.green
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header / controls
        Rectangle {
            Layout.fillWidth: true
            height: 34
            color: theme.bgToolbar
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 12
                Text { text: "服务日志"; color: theme.textPrimary; font.pixelSize: 12; font.bold: true }
                Text { text: "INFO " + controller.logModel.infoCount; color: theme.green; font.pixelSize: 11 }
                Text { text: "WARN " + controller.logModel.warnCount; color: theme.orange; font.pixelSize: 11 }
                Text { text: "ERROR " + controller.logModel.errorCount; color: theme.red; font.pixelSize: 11 }
                Text { text: "DEBUG " + controller.logModel.debugCount; color: theme.textMuted; font.pixelSize: 11 }

                Item { Layout.fillWidth: true }

                ComboBox {
                    implicitWidth: 96
                    implicitHeight: 26
                    model: ["全部", "INFO", "WARN", "ERROR", "DEBUG"]
                    onActivated: controller.logModel.filterLevel = currentText
                }
                IconButton { icon: "trash"; px: 15; tip: "清空"; onClicked: controller.logModel.clear() }
                IconButton { icon: "download"; px: 15; tip: "导出"; onClicked: root.requestExport() }
                IconButton {
                    icon: root.collapsed ? "chevron-up" : "chevron-down"
                    px: 15
                    tip: root.collapsed ? "展开" : "收起"
                    onClicked: root.collapsed = !root.collapsed
                }
            }
        }

        // Column titles
        Rectangle {
            Layout.fillWidth: true
            height: 24
            visible: !root.collapsed
            color: theme.bgPanel2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 0
                Text { text: "时间"; color: theme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 110 }
                Text { text: "级别"; color: theme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 70 }
                Text { text: "模块"; color: theme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 90 }
                Text { text: "信息"; color: theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true }
            }
        }

        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.collapsed
            clip: true
            model: controller.logModel
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                width: logList.width
                height: 22
                color: index % 2 === 0 ? "transparent" : theme.bgPanel2
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 0
                    Text { text: time; color: theme.textMuted; font.pixelSize: 11; font.family: "monospace"; Layout.preferredWidth: 110 }
                    Text { text: level; color: root.levelColor(level); font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 70 }
                    Text { text: module; color: theme.textSecondary; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    Text { text: message; color: theme.textPrimary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }
    }
}
