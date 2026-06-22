import QtQuick
import QtQuick.Controls

// Themed icon button backed by the C++ IconImageProvider.
AbstractButton {
    id: control

    property string iconName: ""
    property string role: "secondary"
    property int px: 18
    property string tip: ""
    property bool active: false
    property color hoverBg: theme.bgHover

    implicitWidth: 34
    implicitHeight: 32
    hoverEnabled: true

    ToolTip.visible: control.tip.length > 0 && control.hovered
    ToolTip.text: control.tip
    ToolTip.delay: 500

    background: Rectangle {
        radius: 10
        color: control.active ? theme.brandBlueSoft
               : (control.hovered ? control.hoverBg : "transparent")
        border.color: control.active ? theme.borderStrong
                    : (control.hovered ? theme.borderSubtle : "transparent")
        border.width: control.active || control.hovered ? 1 : 0
    }

    contentItem: Item {
        Image {
            anchors.centerIn: parent
            width: control.px
            height: control.px
            sourceSize.width: control.px
            sourceSize.height: control.px
            source: theme.icon(control.iconName,
                               control.active ? "brand" : (control.hovered ? "primary" : control.role),
                               control.px)
        }
    }
}
