import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Right sidebar with two tabs: strategy selection/loading and inline backend
// configuration (the modal dialog was removed per feedback).
Rectangle {
    id: root
    color: theme.bgPanel

    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        width: 1
        color: theme.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: theme.bgToolbar }
            TabButton {
                text: "策略"
                contentItem: Text {
                    text: parent.text; color: tabs.currentIndex === 0 ? theme.brandBlue : theme.textSecondary
                    font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "transparent" }
            }
            TabButton {
                text: "自定义服务端"
                contentItem: Text {
                    text: parent.text; color: tabs.currentIndex === 1 ? theme.brandBlue : theme.textSecondary
                    font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "transparent" }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.borderSubtle }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- Strategy tab ----
            ColumnLayout {
                spacing: 10
                ListView {
                    id: strategyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 12
                    clip: true
                    model: controller.strategyModel
                    currentIndex: -1
                    delegate: ItemDelegate {
                        id: strategyDelegate
                        width: ListView.view.width
                        height: 44
                        highlighted: ListView.isCurrentItem
                        onClicked: strategyList.currentIndex = index
                        background: Rectangle {
                            radius: 6
                            color: strategyDelegate.highlighted ? theme.brandBlueSoft
                                   : (strategyDelegate.hovered ? theme.bgHover : "transparent")
                        }
                        contentItem: ColumnLayout {
                            spacing: 1
                            Text { text: name; color: theme.textPrimary; font.pixelSize: 13 }
                            Text { text: type + " · " + time; color: theme.textMuted; font.pixelSize: 11 }
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    implicitHeight: 36
                    enabled: strategyList.currentIndex >= 0
                    text: strategyList.currentIndex >= 0 ? "加载策略" : "请选择策略"
                    onClicked: controller.loadStrategy(controller.strategyModel.nameAt(strategyList.currentIndex))
                    contentItem: Text {
                        text: parent.text; color: parent.enabled ? "white" : theme.textMuted
                        font.pixelSize: 13; font.bold: true
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.enabled ? (parent.hovered ? theme.brandBlueHover : theme.brandBlue) : theme.bgHover
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 12
                    Layout.preferredHeight: infoCol.implicitHeight + 20
                    radius: 8
                    color: theme.bgPanel2
                    border.color: theme.borderSubtle
                    ColumnLayout {
                        id: infoCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        Text { text: "当前策略信息"; color: theme.textSecondary; font.pixelSize: 12; font.bold: true }
                        Text { text: controller.dataStartText; color: theme.textMuted; font.pixelSize: 11 }
                        Text { text: "事件数量：" + controller.eventCount; color: theme.textMuted; font.pixelSize: 11 }
                    }
                }
            }

            // ---- Custom server tab ----
            ColumnLayout {
                spacing: 12
                Layout.margins: 14

                Text { text: "自定义服务端"; color: theme.textPrimary; font.pixelSize: 14; font.bold: true }

                Text { text: "HTTP 接口地址"; color: theme.textSecondary; font.pixelSize: 12 }
                TextField {
                    id: httpField
                    Layout.fillWidth: true
                    text: controller.backendBase
                    placeholderText: "http://127.0.0.1:8080"
                    color: theme.textPrimary
                    background: Rectangle {
                        radius: 6; color: theme.bgPanel2
                        border.color: parent.activeFocus ? theme.borderStrong : theme.borderSubtle
                    }
                }

                Text { text: "WebSocket 地址"; color: theme.textSecondary; font.pixelSize: 12 }
                TextField {
                    id: wsField
                    Layout.fillWidth: true
                    text: controller.wsBase
                    placeholderText: "ws://127.0.0.1:8080"
                    color: theme.textPrimary
                    background: Rectangle {
                        radius: 6; color: theme.bgPanel2
                        border.color: parent.activeFocus ? theme.borderStrong : theme.borderSubtle
                    }
                }

                CheckBox {
                    id: realtimeCheck
                    text: "启用实时推送"
                    checked: controller.realtimeEnabled
                    contentItem: Text {
                        text: parent.text; color: theme.textPrimary; font.pixelSize: 12
                        leftPadding: parent.indicator.width + 6; verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    Layout.fillWidth: true
                    implicitHeight: 34
                    text: "保存并连接"
                    onClicked: controller.configureBackend(httpField.text, wsField.text, realtimeCheck.checked)
                    contentItem: Text {
                        text: parent.text; color: "white"; font.pixelSize: 13; font.bold: true
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: 8; color: parent.hovered ? theme.brandBlueHover : theme.brandBlue }
                }

                Text {
                    visible: controller.backendConfigured
                    text: "当前已连接：" + controller.backendBase
                    color: theme.textMuted; font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
