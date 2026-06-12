import QtQuick
import QtQuick.Controls
import AlgoHub

// Renders the active layout preset. Panes live inside (possibly nested)
// SplitViews so the user can drag to resize, and switching preset / add /
// remove re-creates the panes (each re-registers with the Workspace).
Item {
    id: grid

    // Shared, themed split handle.
    Component {
        id: splitHandle
        Rectangle {
            implicitWidth: 5
            implicitHeight: 5
            color: SplitHandle.pressed ? theme.brandBlue
                 : (SplitHandle.hovered ? theme.bgHover : theme.borderSubtle)
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (controller.layoutId) {
            case "2h": return c2h
            case "2v": return c2v
            case "3h": return c3h
            case "3lr": return c3lr
            case "4": return c4
            default: return c1
            }
        }
    }

    Component { id: c1; ChartPane { slot: 0 } }

    Component {
        id: c2h
        SplitView {
            orientation: Qt.Horizontal
            handle: splitHandle
            ChartPane { SplitView.preferredWidth: parent.width / 2; slot: 0 }
            ChartPane { SplitView.fillWidth: true; slot: 1 }
        }
    }

    Component {
        id: c2v
        SplitView {
            orientation: Qt.Vertical
            handle: splitHandle
            ChartPane { SplitView.preferredHeight: parent.height / 2; slot: 0 }
            ChartPane { SplitView.fillHeight: true; slot: 1 }
        }
    }

    Component {
        id: c3h
        SplitView {
            orientation: Qt.Horizontal
            handle: splitHandle
            ChartPane { SplitView.preferredWidth: parent.width / 3; slot: 0 }
            ChartPane { SplitView.preferredWidth: parent.width / 3; slot: 1 }
            ChartPane { SplitView.fillWidth: true; slot: 2 }
        }
    }

    Component {
        id: c3lr
        SplitView {
            orientation: Qt.Horizontal
            handle: splitHandle
            ChartPane { SplitView.preferredWidth: parent.width / 2; slot: 0 }
            SplitView {
                SplitView.fillWidth: true
                orientation: Qt.Vertical
                handle: splitHandle
                ChartPane { SplitView.preferredHeight: parent.height / 2; slot: 1 }
                ChartPane { SplitView.fillHeight: true; slot: 2 }
            }
        }
    }

    Component {
        id: c4
        SplitView {
            orientation: Qt.Vertical
            handle: splitHandle
            SplitView {
                SplitView.preferredHeight: parent.height / 2
                orientation: Qt.Horizontal
                handle: splitHandle
                ChartPane { SplitView.preferredWidth: parent.width / 2; slot: 0 }
                ChartPane { SplitView.fillWidth: true; slot: 1 }
            }
            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal
                handle: splitHandle
                ChartPane { SplitView.preferredWidth: parent.width / 2; slot: 2 }
                ChartPane { SplitView.fillWidth: true; slot: 3 }
            }
        }
    }
}
