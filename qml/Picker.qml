import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Window {
    id: root

    property bool privateMode: false
    property int current: 0

    visible: false
    flags: Qt.FramelessWindowHint
    color: "transparent"

    function choose(index) {
        if (index >= 0 && index < picker.targets.rowCount()) {
            picker.choose(index, root.privateMode)
        }
    }

    // Reset per invocation: a stale selection or a lingering private toggle
    // from the previous link would be a nasty surprise.
    onVisibleChanged: if (visible) {
        root.current = 0
        root.privateMode = false
        keyHandler.forceActiveFocus()
    }

    // Deliberately not a theme colour: Kirigami.Theme does not resolve inside a
    // plain Window, and a scrim needs to dim whatever is behind it regardless
    // of which theme is active.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)

        TapHandler {
            onTapped: picker.cancel()
        }
    }

    Item {
        id: keyHandler
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            switch (event.key) {
            case Qt.Key_Escape:
                picker.cancel(); event.accepted = true; return
            case Qt.Key_Return:
            case Qt.Key_Enter:
                root.choose(root.current); event.accepted = true; return
            case Qt.Key_Right:
            case Qt.Key_Down:
                root.current = Math.min(root.current + 1, picker.targets.rowCount() - 1)
                event.accepted = true; return
            case Qt.Key_Left:
            case Qt.Key_Up:
                root.current = Math.max(root.current - 1, 0)
                event.accepted = true; return
            case Qt.Key_P:
                root.privateMode = !root.privateMode; event.accepted = true; return
            case Qt.Key_C:
                picker.copyUrl(); event.accepted = true; return
            }

            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                root.choose(event.key - Qt.Key_1)
                event.accepted = true
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 44)
            spacing: Kirigami.Units.largeSpacing

            // The host is the thing you actually decide on, so it carries the
            // emphasis and the rest of the URL is subordinate to it. Both bind
            // their width to the outer column: nesting a layout here sizes them
            // to their own content instead, which elides the URL to nothing.
            Kirigami.Heading {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                level: 1
                text: picker.displayHost
                elide: Text.ElideRight
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.topMargin: -Kirigami.Units.smallSpacing
                horizontalAlignment: Text.AlignHCenter
                text: picker.url
                opacity: 0.7
                elide: Text.ElideMiddle
            }

            Kirigami.Card {
                Layout.fillWidth: true

                contentItem: Flow {
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: picker.targets

                        delegate: QQC2.ItemDelegate {
                            required property int index
                            required property string label
                            required property string iconName
                            required property string shortcut
                            required property bool isBrowser

                            width: Kirigami.Units.gridUnit * 9
                            height: Kirigami.Units.gridUnit * 8
                            highlighted: root.current === index
                            opacity: isBrowser ? 1.0 : 0.65

                            onClicked: root.choose(index)

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    Layout.alignment: Qt.AlignHCenter
                                    source: iconName
                                    implicitWidth: Kirigami.Units.iconSizes.large
                                    implicitHeight: Kirigami.Units.iconSizes.large
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: label
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                QQC2.Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    visible: shortcut !== ""
                                    text: shortcut
                                    opacity: 0.6
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                        }
                    }
                }
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: root.privateMode
                    ? i18n("Private window · P to turn off · C copy · Esc cancel")
                    : i18n("1–9 pick · P private · C copy · Esc cancel")
            }
        }
    }
}
