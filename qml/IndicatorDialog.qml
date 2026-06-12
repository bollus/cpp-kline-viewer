import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    anchors.centerIn: parent
    width: 760
    height: 560
    modal: true
    padding: 0

    property int initialTab: 0

    function openAt(tab) {
        root.initialTab = tab
        tabList.currentIndex = tab
        searchField.text = ""
        root.open()
    }

    background: Rectangle {
        color: theme.bgElevated
        border.color: theme.borderSubtle
        radius: 12
    }

    // ----- categories & their (placeholder) indicator lists -----
    property var categories: [
        { name: "常用指标", icon: "indicator" },
        { name: "自定义指标", icon: "function" },
        { name: "公开指标", icon: "eye" }
    ]
    property var indicatorSets: [
        [
            { name: "MACD", desc: "Moving Average Convergence Divergence" },
            { name: "Simple Move Average (SMA)", desc: "计算指定周期的简单移动平均线" },
            { name: "Volume", desc: "成交量指标" }
        ],
        [
            { name: "新建自定义指标", desc: "使用脚本编辑器编写你自己的指标" }
        ],
        [
            { name: "Order Block", desc: "社区共享 · 订单块结构识别" },
            { name: "Fair Value Gap", desc: "社区共享 · 公允价值缺口" }
        ]
    ]

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 24
            Layout.rightMargin: 16
            Text {
                text: "指标"
                color: theme.textPrimary
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
                verticalAlignment: Text.AlignVCenter
            }
            IconButton {
                iconName: "close"
                tip: "关闭"
                onClicked: root.close()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: theme.borderSubtle }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Left vertical tabs (pinned width so the right side keeps its room)
            ColumnLayout {
                Layout.preferredWidth: 168
                Layout.minimumWidth: 168
                Layout.maximumWidth: 168
                Layout.fillHeight: true
                Layout.margins: 14
                spacing: 6

                Repeater {
                    model: root.categories
                    delegate: ItemDelegate {
                        id: tabDelegate
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 42
                        property bool active: tabList.currentIndex === index
                        onClicked: tabList.currentIndex = index
                        background: Rectangle {
                            radius: 8
                            color: tabDelegate.active ? theme.brandBlueSoft
                                 : tabDelegate.hovered ? theme.bgHover : "transparent"
                            border.width: tabDelegate.active ? 1 : 0
                            border.color: theme.borderStrong
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Image {
                                width: 16; height: 16
                                fillMode: Image.PreserveAspectFit
                                source: theme.icon(tabDelegate.modelData.icon,
                                                   tabDelegate.active ? "brand" : "secondary", 16)
                            }
                            Text {
                                text: tabDelegate.modelData.name
                                color: tabDelegate.active ? theme.brandBlue : theme.textPrimary
                                font.pixelSize: 13
                                font.bold: tabDelegate.active
                                Layout.fillWidth: true
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: theme.borderSubtle }

            // Right content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 320
                Layout.margins: 18
                spacing: 14

                // search
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 8
                    color: theme.bgPanel2
                    border.color: searchField.activeFocus ? theme.borderStrong : theme.borderSubtle
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8
                        Image {
                            width: 16; height: 16
                            fillMode: Image.PreserveAspectFit
                            source: theme.icon("search", "muted", 16)
                        }
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "搜索指标"
                            color: theme.textPrimary
                            placeholderTextColor: theme.textMuted
                            font.pixelSize: 13
                            background: null
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                QtObject {
                    id: tabList
                    property int currentIndex: 0
                }

                // indicator list
                ListView {
                    id: indicatorList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: {
                        var all = root.indicatorSets[tabList.currentIndex] || []
                        var q = searchField.text.trim().toLowerCase()
                        if (q.length === 0) return all
                        return all.filter(function (it) {
                            return it.name.toLowerCase().indexOf(q) >= 0
                                || it.desc.toLowerCase().indexOf(q) >= 0
                        })
                    }
                    delegate: ItemDelegate {
                        id: rowDelegate
                        required property var modelData
                        width: ListView.view.width
                        implicitHeight: 64
                        background: Rectangle {
                            radius: 8
                            color: rowDelegate.hovered ? theme.bgHover : theme.bgPanel
                            border.color: theme.borderSubtle
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            anchors.fill: parent
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                spacing: 3
                                Text {
                                    text: rowDelegate.modelData.name
                                    color: theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: rowDelegate.modelData.desc
                                    color: theme.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            Image {
                                Layout.rightMargin: 16
                                width: 16; height: 16
                                fillMode: Image.PreserveAspectFit
                                source: theme.icon("chevron-right", "muted", 16)
                            }
                        }
                    }
                    ScrollIndicator.vertical: ScrollIndicator {}
                }
            }
        }
    }

    onAboutToShow: tabList.currentIndex = root.initialTab
}
