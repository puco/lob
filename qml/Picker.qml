import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Window {
    id: root

    property bool privateMode: false

    // Filtering is a mode because P, R and C are bare letters: there is no
    // free alphabet to type into while they mean what they mean.
    property bool filtering: false

    // Index into picker.memoryScopes, or -1 for "do not remember". R walks it,
    // so the scopes offered are only ever ones that apply to this URL.
    property int rememberIndex: -1
    readonly property int rememberScope:
        rememberIndex < 0 ? 0 : picker.memoryScopes[rememberIndex].scope

    // Drains 1 -> 0 across the hold. Drives the bar directly rather than
    // animating a control's value, so what is on screen is the time actually
    // left rather than an approximation of it.
    property real holdProgress: 1

    // Caps how wide the overlay content gets on a large screen, and how many
    // target cells fit on one row.
    readonly property int contentWidth:
        Math.min(width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 44)

    visible: false
    flags: Qt.FramelessWindowHint
    color: "transparent"

    function choose(index) {
        if (index >= 0 && index < picker.targets.rowCount()) {
            picker.choose(index, root.privateMode, root.rememberScope)
        }
    }

    // R walks "do not remember" -> each applicable scope -> back to not
    // remembering, so the key both turns it on and takes it back without a
    // second key to learn.
    function cycleRemember() {
        root.rememberIndex = root.rememberIndex + 1 >= picker.memoryScopes.length
            ? -1 : root.rememberIndex + 1
    }

    // Esc gives back the filter before it gives up the link, so the key never
    // throws away more than the last thing that was done.
    function dismiss() {
        if (root.filtering) {
            root.filtering = false
            picker.filter = ""
        } else {
            picker.cancel()
        }
    }

    // Reset per invocation: a stale selection, a lingering private toggle or a
    // leftover "remember" tick from the previous link would all be surprises.
    onVisibleChanged: if (visible) {
        picker.currentIndex = 0
        root.privateMode = false
        root.rememberIndex = -1
        root.filtering = false
        keyHandler.forceActiveFocus()
    }

    // Leaving hold mode must stop the countdown, or the decision fires anyway
    // a moment after the user has overridden it.
    Connections {
        target: picker
        function onContextChanged() {
            if (picker.holding) {
                root.holdProgress = 1
                holdAnimation.restart()
            } else {
                holdAnimation.stop()
            }
        }
    }

    NumberAnimation {
        id: holdAnimation
        target: root
        property: "holdProgress"
        from: 1
        to: 0
        duration: picker.holdMs
        easing.type: Easing.Linear
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
            if (picker.launching) {
                if (event.key === Qt.Key_Escape) picker.cancel()
                event.accepted = true
                return
            }
            // While holding, every key is an escape hatch: the point is that
            // the decision is easy to catch, not that you must catch it exactly.
            if (picker.holding) {
                picker.interruptHold()
                event.accepted = true
                return
            }

            // Keys that mean the same thing whether or not a filter is being
            // typed. The digits are here on purpose: they number the cells on
            // screen, and a filter that took them away would remove the
            // fastest way to pick from the list it just narrowed.
            switch (event.key) {
            case Qt.Key_Escape:
                root.dismiss(); event.accepted = true; return
            case Qt.Key_Return:
            case Qt.Key_Enter:
                root.choose(picker.currentIndex); event.accepted = true; return
            case Qt.Key_Right:
            case Qt.Key_Down:
                picker.currentIndex = Math.min(picker.currentIndex + 1, picker.targets.rowCount() - 1)
                event.accepted = true; return
            case Qt.Key_Left:
            case Qt.Key_Up:
                picker.currentIndex = Math.max(picker.currentIndex - 1, 0)
                event.accepted = true; return
            case Qt.Key_F5:
                picker.refreshTargets(); event.accepted = true; return
            }

            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                root.choose(event.key - Qt.Key_1)
                event.accepted = true
                return
            }

            // Ctrl+F and / start filtering; Ctrl+R stays refresh, because that
            // is what every browser does.
            if ((event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier))
                    || (event.key === Qt.Key_Slash && !root.filtering)) {
                root.filtering = true
                event.accepted = true; return
            }
            if (event.key === Qt.Key_R && (event.modifiers & Qt.ControlModifier)) {
                picker.refreshTargets(); event.accepted = true; return
            }

            if (root.filtering) {
                // Alt keeps the actions reachable without leaving the filter,
                // which would otherwise mean retyping it to remember a choice.
                if (event.modifiers & Qt.AltModifier) {
                    switch (event.key) {
                    case Qt.Key_P: root.privateMode = !root.privateMode; event.accepted = true; return
                    case Qt.Key_R: root.cycleRemember(); event.accepted = true; return
                    case Qt.Key_C: picker.copyUrl(); event.accepted = true; return
                    }
                }
                if (event.key === Qt.Key_Backspace) {
                    picker.filter = picker.filter.slice(0, -1)
                    if (picker.filter === "") root.filtering = false
                    event.accepted = true; return
                }
                if (event.text.length > 0 && event.text.charCodeAt(0) >= 0x20) {
                    picker.filter += event.text
                    event.accepted = true; return
                }
                return
            }

            switch (event.key) {
            case Qt.Key_P:
                root.privateMode = !root.privateMode; event.accepted = true; return
            case Qt.Key_R:
                root.cycleRemember(); event.accepted = true; return
            case Qt.Key_C:
                picker.copyUrl(); event.accepted = true; return
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
                textFormat: Text.PlainText
                opacity: 0.7
                elide: Text.ElideMiddle
            }

            // The URL above is where the link goes, which is not the URL that
            // was clicked when something wrapped it. Say so rather than leaving
            // the difference to be noticed.
            QQC2.Label {
                Layout.fillWidth: true
                Layout.topMargin: -Kirigami.Units.smallSpacing
                horizontalAlignment: Text.AlignHCenter
                visible: picker.wrapperHost !== ""
                text: i18n("via %1", picker.wrapperHost)
                textFormat: Text.PlainText
                opacity: 0.5
                elide: Text.ElideMiddle
            }

            // ---- Hold: a decision already made, shown so it can be caught ----
            QQC2.Label {
                Layout.fillWidth: true
                visible: picker.errorMessage !== ""
                text: picker.errorMessage
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                visible: picker.launching
                text: i18n("Opening browser…")
            }

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

                    // Drawn rather than a QQC2.ProgressBar: under the desktop
                    // style that control is painted by QStyle from the widget
                    // palette, not Kirigami.Theme, so it renders light on this
                    // dark card no matter what the theme says.
                    Rectangle {
                        id: holdTrack

                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        implicitHeight: Math.round(Kirigami.Units.smallSpacing * 0.75)
                        radius: height / 2
                        color: Qt.rgba(Kirigami.Theme.textColor.r,
                                       Kirigami.Theme.textColor.g,
                                       Kirigami.Theme.textColor.b,
                                       0.15)

                        Rectangle {
                            height: parent.height
                            radius: parent.radius
                            width: parent.width * root.holdProgress
                            color: Kirigami.Theme.highlightColor
                        }
                    }
                }
            }

            // ---- Picker ----
            QQC2.Label {
                Layout.fillWidth: true
                visible: !picker.holding && picker.targets.count === 0
                text: picker.unfilteredCount > 0
                    ? i18n("Nothing matches “%1”.", picker.filter)
                    : i18n("No browsers found. Install a browser, then refresh the list.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            // The filter is shown as its own line rather than as a text field:
            // focus stays on the key handler, which is what keeps the digits
            // and Esc behaving the same whether or not a filter is being typed.
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                visible: root.filtering && !picker.holding
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "search"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    text: picker.filter.length > 0 ? picker.filter : i18n("type to filter")
                    opacity: picker.filter.length > 0 ? 1 : 0.6
                    font.bold: picker.filter.length > 0
                }

                QQC2.Label {
                    visible: picker.filter.length > 0
                    opacity: 0.7
                    text: i18np("%1 of %2", "%1 of %2", picker.targets.count, picker.unfilteredCount)
                }
            }
            //
            // Centred and sized to its contents rather than stretched: a Flow
            // filling the full width packs fixed-width cells from the left and
            // leaves the remainder as dead space on the right, which reads as a
            // layout bug even though the cells themselves are correct.
            Kirigami.Card {
                id: pickerCard

                // Kirigami.AbstractCard defaults Layout.fillWidth to true,
                // which silently overrides the alignment and stretches the card
                // across the whole overlay. Turning it off is what actually
                // makes the card hug its cells.
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignHCenter
                visible: !picker.holding
                enabled: !picker.launching

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
                            required property bool supportsPrivate

                            Layout.preferredWidth: grid.cellWidth
                            Layout.preferredHeight: grid.cellHeight
                            highlighted: picker.currentIndex === index
                            enabled: !root.privateMode || supportsPrivate
                            QQC2.ToolTip.text: i18n("Private browsing is unavailable for this target")
                            QQC2.ToolTip.visible: hovered && root.privateMode && !supportsPrivate
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
                                    textFormat: Text.PlainText
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
                enabled: !picker.launching

                QQC2.CheckBox {
                    visible: picker.memoryScopes.length > 0
                    checked: root.rememberIndex >= 0
                    onToggled: root.cycleRemember()
                    // Naming the scope rather than describing it: the pattern
                    // about to be written is the thing worth being sure of.
                    text: root.rememberIndex < 0
                        ? (picker.remembered
                            ? i18n("Update what I remember for %1", picker.displayHost)
                            : i18n("Remember for %1", picker.displayHost))
                        : picker.memoryScopes[root.rememberIndex].label
                }

                QQC2.Label {
                    visible: root.rememberIndex >= 0 && picker.memoryScopes.length > 1
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                    text: i18n("R for another scope")
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
                    : root.filtering
                        ? i18n("1–9 pick · Alt+P private · Alt+R remember · Alt+C copy · Esc clear filter")
                        : i18n("1–9 pick · / filter · P private · R remember · C copy · F5 refresh · Esc cancel")
            }
        }
    }
}
