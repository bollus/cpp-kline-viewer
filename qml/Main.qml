import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs

ApplicationWindow {
    id: appWindow
    visible: true
    width: 1480
    height: 900
    minimumWidth: 960
    minimumHeight: 600
    title: "AlgoHub 量化复盘终端"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: theme.bgApp

    property bool sidebarVisible: true

    function toggleMaximize() {
        appWindow.visibility = (appWindow.visibility === Window.Maximized)
            ? Window.Windowed : Window.Maximized
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            onRequestMinimize: appWindow.showMinimized()
            onRequestMaximize: appWindow.toggleMaximize()
            onRequestClose: Qt.quit()
            onOpenSettings: settingsPopover.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            DrawingToolbar { Layout.fillHeight: true }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ChartToolbar {
                    Layout.fillWidth: true
                    onToggleSidebar: appWindow.sidebarVisible = !appWindow.sidebarVisible
                    onOpenIndicators: indicatorDialog.open()
                    onOpenCustomIndicators: indicatorDialog.open()
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: theme.bgApp

                    ChartItem {
                        id: chart
                        anchors.fill: parent
                        Component.onCompleted: controller.chart = chart
                    }
                }

                ReplayBar { Layout.fillWidth: true }
            }

            RightSidebar {
                Layout.preferredWidth: 288
                Layout.fillHeight: true
                visible: appWindow.sidebarVisible
            }
        }

        LogPanel {
            id: logPanel
            Layout.fillWidth: true
            Layout.preferredHeight: collapsed ? 34 : 220
            onRequestExport: exportDialog.open()
        }

        StatusBar { Layout.fillWidth: true }
    }

    // Edge resize handles (frameless window).
    Repeater {
        model: [
            { e: Qt.LeftEdge,  x: 0,                  y: 0, w: 4, h: appWindow.height, c: Qt.SizeHorCursor },
            { e: Qt.RightEdge, x: appWindow.width-4,  y: 0, w: 4, h: appWindow.height, c: Qt.SizeHorCursor },
            { e: Qt.TopEdge,   x: 0, y: 0,                 w: appWindow.width, h: 4, c: Qt.SizeVerCursor },
            { e: Qt.BottomEdge,x: 0, y: appWindow.height-4,w: appWindow.width, h: 4, c: Qt.SizeVerCursor }
        ]
        MouseArea {
            x: modelData.x; y: modelData.y
            width: modelData.w; height: modelData.h
            cursorShape: modelData.c
            onPressed: appWindow.startSystemResize(modelData.e)
        }
    }

    // ---- Settings popover ----
    Popup {
        id: settingsPopover
        x: appWindow.width - width - 12
        y: 56
        width: 240
        padding: 12
        background: Rectangle { color: theme.bgElevated; border.color: theme.borderSubtle; radius: 10 }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "设置"; color: theme.textPrimary; font.pixelSize: 14; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "深色模式"; color: theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                Switch {
                    checked: theme.dark
                    onToggled: { theme.dark = checked; controller.dark = checked }
                }
            }
            Text { text: "AlgoHub · 量化复盘终端"; color: theme.textMuted; font.pixelSize: 11 }
        }
    }

    // ---- Indicator dialog (basic shell) ----
    Dialog {
        id: indicatorDialog
        anchors.centerIn: parent
        width: 420
        modal: true
        title: "指标"
        standardButtons: Dialog.Close
        background: Rectangle { color: theme.bgElevated; border.color: theme.borderSubtle; radius: 10 }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: "指标与自定义指标管理将在此处提供。"
                color: theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: "导出日志"
        fileMode: FileDialog.SaveFile
        nameFilters: ["TSV Files (*.tsv)", "All Files (*)"]
        onAccepted: controller.logModel.exportTsv(selectedFile.toString().replace("file://", ""))
    }

    Component.onCompleted: controller.start()
}
