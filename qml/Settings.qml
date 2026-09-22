import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    title: i18nc("@title:window", "Lob Settings")
    width: Kirigami.Units.gridUnit * 44
    height: Kirigami.Units.gridUnit * 34
    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 24

    // Closing settings must not take the daemon down with it: the window is a
    // view onto a process whose job is to be there when a link is clicked.
    onClosing: (close) => { close.accepted = true }

    readonly property var matchNames: [
        i18n("Host"), i18n("Domain"), i18n("URL prefix"), i18n("Regular expression")
    ]
    readonly property var actionNames: [i18n("Open"), i18n("Always ask"), i18n("Copy")]

    function targetLabel(id) {
        if (id === "") return i18n("(none)")
        for (let i = 0; i < settings.targets.length; ++i) {
            const t = settings.targets[i]
            if (t.id === id) {
                return t.profileName === "" ? t.label : i18nc("browser, profile", "%1 — %2", t.label, t.profileName)
            }
        }
        // A rule can name a browser that is not installed here. Saying so beats
        // showing an empty cell that looks like the rule lost its target.
        return i18nc("a target id that is not installed", "%1 (not installed)", id)
    }

    pageStack.initialPage: Kirigami.ScrollablePage {
        title: i18nc("@title", "Lob")

        header: QQC2.ToolBar {
            visible: settings.lastError !== ""
            contentItem: RowLayout {
                Kirigami.Icon { source: "dialog-error" }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: settings.lastError
                }
            }
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            // ---- Rules ----
            Kirigami.Heading {
                level: 2
                text: i18n("Rules")
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                // Order is semantics, not decoration: someone reordering this
                // list is changing which rule decides a link.
                text: i18n("The first rule that matches decides, so order matters. Rules always beat remembered choices.")
            }

            Repeater {
                model: settings.rules
                delegate: Kirigami.AbstractCard {
                    id: ruleCard
                    // ComboBox.activated(int index) shadows the delegate's own
                    // index inside its handler, so the row is read from here.
                    readonly property int row: index
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        RowLayout {
                            Layout.fillWidth: true

                            QQC2.CheckBox {
                                checked: model.enabled
                                onToggled: settings.rules.setEnabled(index, checked)
                                QQC2.ToolTip.text: i18n("A disabled rule stays in the file and decides nothing")
                                QQC2.ToolTip.visible: hovered
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                opacity: model.enabled ? 1 : 0.5
                                text: model.description
                            }

                            QQC2.ToolButton {
                                icon.name: "go-up"
                                enabled: index > 0
                                onClicked: settings.rules.move(index, -1)
                                QQC2.ToolTip.text: i18n("Match this earlier")
                                QQC2.ToolTip.visible: hovered
                            }
                            QQC2.ToolButton {
                                icon.name: "go-down"
                                enabled: index < settings.rules.count - 1
                                onClicked: settings.rules.move(index, 1)
                                QQC2.ToolTip.text: i18n("Match this later")
                                QQC2.ToolTip.visible: hovered
                            }
                            QQC2.ToolButton {
                                icon.name: "edit-delete"
                                onClicked: settings.rules.remove(index)
                                QQC2.ToolTip.text: i18n("Delete this rule")
                                QQC2.ToolTip.visible: hovered
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.ComboBox {
                                model: root.matchNames
                                currentIndex: matchKind
                                onActivated: settings.rules.setMatchKind(ruleCard.row, currentIndex)
                            }
                            QQC2.TextField {
                                Layout.fillWidth: true
                                text: pattern
                                onEditingFinished: if (text !== pattern) settings.rules.setPattern(index, text)
                            }
                            QQC2.ComboBox {
                                model: root.actionNames
                                currentIndex: action
                                onActivated: settings.rules.setAction(ruleCard.row, currentIndex)
                            }
                            QQC2.Label {
                                visible: action === 0
                                text: root.targetLabel(targetId)
                                elide: Text.ElideRight
                                Layout.maximumWidth: Kirigami.Units.gridUnit * 12
                            }
                        }
                    }
                }
            }

            QQC2.Button {
                icon.name: "list-add"
                text: i18n("Add Rule…")
                onClicked: addSheet.open()
            }

            // ---- Remembered ----
            Kirigami.Heading {
                level: 2
                text: i18n("Remembered choices")
                visible: settings.memories.count > 0
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                visible: settings.memories.count > 0
                // Not reorderable, and the reason is worth saying: these are
                // kept narrowest-first by whatever wrote them.
                text: i18n("Written by “remember” in the picker, narrowest first. Delete one to be asked again.")
            }

            Repeater {
                model: settings.memories
                delegate: Kirigami.AbstractCard {
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        QQC2.CheckBox {
                            checked: model.enabled
                            onToggled: settings.memories.setEnabled(index, checked)
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            opacity: model.enabled ? 1 : 0.5
                            text: model.description
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-delete"
                            onClicked: settings.memories.remove(index)
                            QQC2.ToolTip.text: i18n("Forget this")
                            QQC2.ToolTip.visible: hovered
                        }
                    }
                }
            }

            // ---- Other handlers ----
            Kirigami.Heading {
                level: 2
                text: i18n("Other applications")
                visible: settings.otherHandlers.length > 0
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                visible: settings.otherHandlers.length > 0
                // The discoverability gap this window exists to close: these
                // were found all along and hidden, and nothing said so.
                text: i18n("These register for links without being browsers. They are hidden from the picker until enabled here.")
            }

            Repeater {
                model: settings.otherHandlers
                delegate: QQC2.CheckBox {
                    text: modelData.label
                    checked: modelData.enabled
                    onToggled: settings.setOtherHandlerEnabled(modelData.id, checked)
                }
            }

            // ---- Behaviour ----
            Kirigami.Heading {
                level: 2
                text: i18n("Behaviour")
            }

            RowLayout {
                QQC2.Label { text: i18n("Hold bar:") }
                QQC2.SpinBox {
                    from: 0
                    to: 60000
                    stepSize: 100
                    value: settings.holdMs
                    onValueModified: settings.holdMs = value
                }
                QQC2.Label {
                    opacity: 0.7
                    text: settings.holdMs === 0
                        ? i18n("ms — zero opens immediately, with nothing to interrupt")
                        : i18n("ms before a decided link opens")
                }
            }

            QQC2.CheckBox {
                text: i18n("Strip tracking parameters from links")
                checked: settings.stripTracking
                onToggled: settings.stripTracking = checked
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.6
                font: Kirigami.Theme.smallFont
                text: i18n("Everything here is %1, which stays hand-editable.", settings.configPath())
            }
        }

        // Adding a rule is a form rather than a blank row, because the store
        // refuses an incomplete rule -- and it is right to: a rule with no
        // pattern matches nothing and says nothing about it.
        Kirigami.OverlaySheet {
            id: addSheet
            title: i18nc("@title:window", "Add Rule")

            property int kind: 0
            property int act: 0

            ColumnLayout {
                QQC2.ComboBox {
                    Layout.fillWidth: true
                    model: root.matchNames
                    currentIndex: addSheet.kind
                    onActivated: addSheet.kind = currentIndex
                }
                QQC2.TextField {
                    id: patternField
                    Layout.fillWidth: true
                    placeholderText: addSheet.kind === 2 ? "github.com/anthropics" : "example.com"
                }
                QQC2.ComboBox {
                    Layout.fillWidth: true
                    model: root.actionNames
                    currentIndex: addSheet.act
                    onActivated: addSheet.act = currentIndex
                }
                QQC2.ComboBox {
                    id: targetField
                    Layout.fillWidth: true
                    visible: addSheet.act === 0
                    model: settings.targets
                    textRole: "label"
                    valueRole: "id"
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    visible: settings.lastError !== ""
                    color: Kirigami.Theme.negativeTextColor
                    text: settings.lastError
                }
                QQC2.Button {
                    text: i18n("Add")
                    onClicked: {
                        const ok = settings.rules.appendRule(
                            patternField.text, addSheet.kind, addSheet.act,
                            addSheet.act === 0 ? targetField.currentValue : "", false)
                        // Only close on success: a refusal has something to say
                        // about the field, and closing would throw it away
                        // along with what was typed.
                        if (ok) {
                            patternField.text = ""
                            addSheet.close()
                        }
                    }
                }
            }
        }
    }
}
