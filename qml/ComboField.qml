import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 28
    implicitWidth: 120
    font.pixelSize: 12

    background: Rectangle {
        radius: 6
        color: theme.bgPanel2
        border.width: 1
        border.color: control.activeFocus || control.hovered ? theme.borderStrong : theme.borderSubtle
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: control.indicator.width + 6
        text: control.displayText
        font: control.font
        color: theme.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Image {
        x: control.width - width - 8
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 12
        height: 12
        fillMode: Image.PreserveAspectFit
        source: theme.icon("chevron-down", "muted", 12)
    }

    delegate: ItemDelegate {
        width: ListView.view ? ListView.view.width : control.width
        height: 28
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: modelData
            color: theme.textPrimary
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.highlighted ? theme.bgHover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        padding: 2
        background: Rectangle {
            color: theme.bgElevated
            border.color: theme.borderSubtle
            radius: 6
        }
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 220)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
