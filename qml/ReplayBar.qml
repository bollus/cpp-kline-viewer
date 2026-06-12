import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Replay transport. Hidden unless replay is active (per design feedback).
Rectangle {
    id: root
    height: 44
    color: theme.bgPanel
    visible: controller.replayActive

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        IconButton { iconName: "skip-back"; tip: "回到起点"; onClicked: controller.replayJumpToStart() }
        IconButton { iconName: "chevron-left"; tip: "上一根"; onClicked: controller.replayStepBackward() }
        IconButton {
            iconName: controller.replayPlaying ? "pause" : "play"
            tip: controller.replayPlaying ? "暂停" : "播放"
            role: "primary"
            onClicked: controller.replayPlayPause()
        }
        IconButton { iconName: "chevron-right"; tip: "下一根"; onClicked: controller.replayStepForward() }
        IconButton { iconName: "skip-forward"; tip: "跳到末尾"; onClicked: controller.replayJumpToEnd() }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: theme.borderSubtle }

        Rectangle {
            Layout.preferredWidth: 184
            Layout.preferredHeight: 28
            radius: 6
            color: theme.bgPanel2
            border.color: cursorField.activeFocus ? theme.borderStrong : theme.borderSubtle
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 8
                Image {
                    width: 15; height: 15
                    sourceSize.width: 15; sourceSize.height: 15
                    source: theme.icon("calendar", "muted", 15)
                }
                TextField {
                    id: cursorField
                    Layout.fillWidth: true
                    font.pixelSize: 12
                    color: theme.textPrimary
                    background: null
                    verticalAlignment: Text.AlignVCenter
                    inputMask: "9999-99-99 99:99;_"
                    onEditingFinished: controller.setReplayCursorText(text)
                    Keys.onReturnPressed: controller.setReplayCursorText(text)
                    Component.onCompleted: text = controller.replayCursorText
                    Connections {
                        target: controller
                        function onReplayChanged() {
                            if (!cursorField.activeFocus) cursorField.text = controller.replayCursorText
                        }
                    }
                }
            }
        }

        Slider {
            id: progress
            Layout.fillWidth: true
            from: 0; to: 1
            value: controller.replayProgress
            onMoved: controller.replaySeek(value)
            background: Rectangle {
                x: progress.leftPadding
                y: progress.topPadding + progress.availableHeight / 2 - height / 2
                width: progress.availableWidth
                height: 4
                radius: 2
                color: theme.bgHover
                Rectangle {
                    width: progress.visualPosition * parent.width
                    height: parent.height
                    color: theme.brandBlue
                    radius: 2
                }
            }
            handle: Rectangle {
                x: progress.leftPadding + progress.visualPosition * (progress.availableWidth - width)
                y: progress.topPadding + progress.availableHeight / 2 - height / 2
                width: 14; height: 14; radius: 7
                color: theme.brandBlue
            }
        }

        ComboField {
            id: speedBox
            implicitWidth: 84
            model: ["1", "2", "5", "10", "20", "50"]
            currentIndex: 3
            onActivated: controller.replaySpeed = parseInt(currentText)
            displayText: currentText + "x"
        }
    }
}
