import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Window {
    id: root

    property bool privateMode: false
    property bool rememberChoice: false
    property int current: 0

    // Caps how wide the overlay content gets on a large screen, and how many
    // target cells fit on one row.
    readonly property int contentWidth:
        Math.min(width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 44)

    visible: false
    flags: Qt.FramelessWindowHint
    color: "transparent"

    function choose(index) {
        if (index >= 0 && index < picker.targets.rowCount()) {
            picker.choose(index, root.privateMode, root.rememberChoice)
        }
    }

    // Reset per invocation: a stale selection, a lingering private toggle or a
    // leftover "remember" tick from the previous link would all be surprises.
    onVisibleChanged: if (visible) {
        root.current = 0
        root.privateMode = false
        root.rememberChoice = false
        keyHandler.forceActiveFocus()
    }

    // Leaving hold mode must stop the countdown, or the decision fires anyway
    // a moment after the user has overridden it.
    Connections {
        target: picker
        function onContextChanged() {
            if (picker.holding) {
                holdTimer.restart()
            } else {
                holdTimer.stop()
            }
        }
    }

    Timer {
        id: holdTimer
        interval: picker.holdMs
        onTriggered: picker.holdCompleted()
    }

    // Deliberately not a theme colour: Kirigami.Theme does not resolve inside a
    // plain Window, and a scrim needs to dim whatever is behind it regardless
    // of which theme is active.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, picker.holding ? 0.35 : 0.6)

        Behavior on color { ColorAnimation { duration: Kirigami.Units.shortDuration } }

        TapHandler {
            onTapped: picker.holding ? picker.interruptHold() : picker.cancel()
        }
    }

    Item {
        id: keyHandler
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            // While holding, every key is an escape hatch: the point is that
            // the decision is easy to catch, not that you must catch it exactly.
            if (picker.holding) {
                picker.interruptHold()
                event.accepted = true
                return
            }

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
            case Qt.Key_R:
                root.rememberChoice = !root.rememberChoice; event.accepted = true; return
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
            width: root.contentWidth
            spacing: Kirigami.Units.largeSpacing

            // The host is the thing you actually decide on, so it carries the
            // emphasis and the rest of the URL is subordinate to it.
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

            // ---- Hold: a decision already made, shown so it can be caught ----
            Kirigami.Card {
                Layout.fillWidth: true
                visible: picker.holding

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: picker.holdTitle
                        font.bold: true
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: picker.holdReason
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                    }

                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: picker.holdMs
                        value: holdTimer.running ? picker.holdMs : 0

                        Behavior on value {
                            NumberAnimation { duration: picker.holdMs; easing.type: Easing.Linear }
                        }
                    }
                }
            }

            // ---- Picker ----
            //
            // Centred and sized to its contents rather than stretched: a Flow
            // filling the full width packs fixed-width cells from the left and
            // leaves the remainder as dead space on the right, which reads as a
            // layout bug even though the cells themselves are correct.
            Kirigami.Card {
                Layout.alignment: Qt.AlignHCenter
                visible: !picker.holding

                contentItem: GridLayout {
                    id: grid

                    readonly property int cellWidth: Kirigami.Units.gridUnit * 7
                    readonly property int cellHeight: Kirigami.Units.gridUnit * 6
                    readonly property int maxColumns:
                        Math.max(1, Math.floor(root.contentWidth / (cellWidth + Kirigami.Units.smallSpacing)))

                    // Balance the rows instead of leaving one item stranded:
                    // five targets read better as 3 + 2 than as 4 + 1.
                    columns: {
                        var n = Math.min(picker.targets.count, maxColumns)
                        if (n < 1)
                            return 1
                        var rows = Math.ceil(picker.targets.count / n)
                        return Math.ceil(picker.targets.count / rows)
                    }

                    rowSpacing: Kirigami.Units.smallSpacing
                    columnSpacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: picker.targets

                        delegate: QQC2.ItemDelegate {
                            required property int index
                            required property string label
                            required property string iconName
                            required property string shortcut
                            required property bool isBrowser

                            Layout.preferredWidth: grid.cellWidth
                            Layout.preferredHeight: grid.cellHeight
                            highlighted: root.current === index
                            opacity: isBrowser ? 1.0 : 0.65

                            onClicked: root.choose(index)

                            contentItem: ColumnLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    Layout.alignment: Qt.AlignHCenter
                                    source: iconName
                                    implicitWidth: Kirigami.Units.iconSizes.medium
                                    implicitHeight: Kirigami.Units.iconSizes.medium
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: label
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    font: Kirigami.Theme.smallFont
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

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                visible: !picker.holding
                spacing: Kirigami.Units.largeSpacing

                QQC2.CheckBox {
                    checked: root.rememberChoice
                    onToggled: root.rememberChoice = checked
                    text: picker.remembered
                        ? i18n("Update what I remember for %1", picker.displayHost)
                        : i18n("Remember for %1", picker.displayHost)
                }

                QQC2.Label {
                    visible: root.privateMode
                    text: i18n("Private window")
                    font.bold: true
                }
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                text: picker.holding
                    ? i18n("Any key or click to choose a different browser")
                    : i18n("1–9 pick · P private · R remember · C copy · Esc cancel")
            }
        }
    }
}
