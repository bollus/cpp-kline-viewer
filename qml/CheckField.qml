import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    property color tint: theme.brandBlue
    implicitHeight: 24
    spacing: 8
    font.pixelSize: 12

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        radius: 4
        x: 0
        y: (control.height - height) / 2
        color: control.checked ? control.tint : "transparent"
        border.width: 1.5
        border.color: control.checked ? control.tint : theme.textMuted

        Image {
            anchors.centerIn: parent
            width: 11
            height: 11
            visible: control.checked
            fillMode: Image.PreserveAspectFit
            source: theme.icon("check", "onAccent", 11)
        }
    }

    contentItem: Text {
        text: control.text
        color: theme.textPrimary
        font: control.font
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
