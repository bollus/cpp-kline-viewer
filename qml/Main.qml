import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import AlgoHub

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
        // showMaximized()/showNormal() honour the screen's available geometry,
        // so a frameless window no longer slides under the macOS menu bar.
        if (appWindow.visibility === Window.Maximized)
            appWindow.showNormal()
        else
            appWindow.showMaximized()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            id: topBar
            Layout.fillWidth: true
            onRequestMinimize: appWindow.showMinimized()
            onRequestMaximize: appWindow.toggleMaximize()
            onRequestClose: Qt.quit()
            onOpenSettings: settingsPopover.open()
            onOpenLayout: layoutMenu.open()
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
                    onOpenIndicators: indicatorDialog.openAt(0)
                    onOpenCustomIndicators: indicatorDialog.openAt(1)
                }

                ChartGrid {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
        width: 280
        padding: 14
        background: Rectangle { color: theme.bgElevated; border.color: theme.borderSubtle; radius: 10 }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "设置"; color: theme.textPrimary; font.pixelSize: 14; font.bold: true }

            Rectangle { Layout.fillWidth: true; height: 1; color: theme.borderSubtle }

            // Update check
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "软件更新"; color: theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                    Text { text: "v" + controller.appVersion; color: theme.textMuted; font.pixelSize: 11 }
                }
                Text {
                    text: controller.updateStatus
                    color: controller.updateAvailable ? theme.brandBlue : theme.textMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        text: controller.updateChecking ? "检查中..." : "检查更新"
                        enabled: !controller.updateChecking
                        font.pixelSize: 12
                        onClicked: controller.checkForUpdates()
                        contentItem: Text { text: parent.text; color: theme.textPrimary; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 6; color: parent.hovered ? theme.bgHover : theme.bgPanel2; border.color: theme.borderSubtle; implicitHeight: 28 }
                    }
                    Button {
                        visible: controller.updateAvailable
                        text: "前往下载"
                        font.pixelSize: 12
                        onClicked: controller.openDownloadPage()
                        contentItem: Text { text: parent.text; color: "#FFFFFF"; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 6; color: theme.brandBlue; implicitHeight: 28 }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: theme.borderSubtle }
            Text { text: "AlgoHub · 量化复盘终端"; color: theme.textMuted; font.pixelSize: 11 }
        }
    }

    // ---- Layout preset menu ----
    LayoutMenu {
        id: layoutMenu
        x: appWindow.width - width - 12
        y: 56
    }

    // ---- Indicator dialog ----
    IndicatorDialog { id: indicatorDialog }

    FileDialog {
        id: exportDialog
        title: "导出日志"
        fileMode: FileDialog.SaveFile
        nameFilters: ["TSV Files (*.tsv)", "All Files (*)"]
        onAccepted: controller.logModel.exportTsv(selectedFile.toString().replace("file://", ""))
    }

    Component.onCompleted: controller.start()
}
