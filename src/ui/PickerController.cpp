#include "PickerController.h"

#include "TargetModel.h"
#include "core/RuleStore.h"
#include "core/Startup.h"
#include "core/TargetRegistry.h"

#include <LayerShellQt/Window>

#include <KLocalizedString>
#include <KWindowSystem>

#include <QClipboard>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>

Q_LOGGING_CATEGORY(LOG_PICKER, "lob.picker")

namespace Lob
{

namespace
{
/// With KeyboardInteractivityExclusive a picker that fails to hide is a
/// session-level keyboard trap, so it always gets force-hidden eventually.
constexpr int kWatchdogMs = 60'000;

/// Holding this while clicking a link overrides whatever rule would fire.
constexpr Qt::KeyboardModifier kOverrideModifier = Qt::ShiftModifier;
} // namespace

PickerController::PickerController(RuleStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_model(new TargetModel(this))
    , m_launcher(new Launcher(this))
{
    connect(m_launcher, &Launcher::launchFailed, this, &PickerController::errorOccurred);
}

QAbstractItemModel *PickerController::targetsModel() const
{
    return m_model;
}

QString PickerController::url() const
{
    return m_url.toString();
}

QString PickerController::displayHost() const
{
    return m_url.host();
}

PickerController::Mode PickerController::mode() const
{
    return m_mode;
}

bool PickerController::isHolding() const
{
    return m_mode == Mode::Hold;
}

QString PickerController::holdTitle() const
{
    // Composed here rather than in QML: only this side knows whether the
    // decision opens something or copies it, and "Opening in Copy to
    // clipboard" is what you get if the caller guesses.
    if (m_decision.action == RuleAction::Copy) {
        return i18n("Copying to clipboard");
    }

    const Target target = targetById(m_decision.targetId);
    const QString label = target.id.isEmpty() ? m_decision.targetId : target.label;
    return m_decision.privateWindow ? i18n("Opening in %1 (private)", label) : i18n("Opening in %1", label);
}

QString PickerController::holdReason() const
{
    switch (m_decision.source) {
    case Decision::Source::Rule:
        return i18n("matched a rule");
    case Decision::Source::Memory:
        return i18n("remembered for %1", m_url.host());
    case Decision::Source::Fallback:
        return i18n("default target");
    case Decision::Source::Ask:
        return {};
    }
    return {};
}

int PickerController::holdMs() const
{
    return m_store ? m_store->holdMs() : 600;
}

bool PickerController::isRemembered() const
{
    return m_store && m_store->hasMemory(m_url.host());
}

bool PickerController::hasTargets() const
{
    return m_model->rowCount() > 0;
}

Target PickerController::targetById(const QString &id) const
{
    const auto &targets = m_model->targets();
    for (const Target &target : targets) {
        if (target.id == id) {
            return target;
        }
    }
    return {};
}

void PickerController::refreshTargets()
{
    auto targets = TargetRegistry::discover(QStringLiteral(LOB_APP_ID ".desktop"));

    // Handlers that are not browsers stay hidden until explicitly enabled, but
    // they are never filtered out of discovery -- whether routing a link to one
    // is useful is the user's call, not ours.
    if (m_store) {
        const QStringList enabled = m_store->enabledOtherHandlers();
        targets.removeIf([&enabled](const Target &target) {
            return target.kind != TargetKind::Browser && !enabled.contains(target.id);
        });
    }

    m_model->setTargets(targets);
}

bool PickerController::ensureWindow(QQmlApplicationEngine *engine)
{
    if (m_window) {
        return true;
    }

    engine->rootContext()->setContextProperty(QStringLiteral("picker"), this);
    engine->loadFromModule(QStringLiteral(LOB_APP_ID), QStringLiteral("Picker"));

    if (engine->rootObjects().isEmpty()) {
        return false;
    }

    m_window = qobject_cast<QWindow *>(engine->rootObjects().constFirst());
    if (!m_window) {
        return false;
    }

    // Exclusive keyboard is what lets the picker take input unconditionally
    // instead of losing a focus-stealing-prevention argument with the browser
    // that is about to open. It is also what makes the held-modifier check
    // below possible at all. X11 has no such protocol, so fall back.
    const bool noLayerShell = qEnvironmentVariableIsSet("LOB_NO_LAYERSHELL");
    LayerShellQt::Window *layer = noLayerShell ? nullptr : LayerShellQt::Window::get(m_window);

    if (layer) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors({LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                           | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight});
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layer->setExclusiveZone(-1);
        layer->setScope(QStringLiteral("lob-picker"));
        layer->setCloseOnDismissed(false);
    } else {
        m_window->setFlags(m_window->flags() | Qt::Dialog | Qt::WindowStaysOnTopHint);
    }

    connect(m_window, &QWindow::activeChanged, this, [this] {
        if (m_window->isActive()) {
            checkHeldModifiers();
        }
    });

    return true;
}

void PickerController::showPicker(const QUrl &url, const QString &activationToken)
{
    m_url = url;
    m_mode = Mode::Picker;
    m_decision = {};
    present(activationToken);
}

void PickerController::showHold(const QUrl &url, const QString &activationToken, const Decision &decision)
{
    m_url = url;
    m_decision = decision;

    // A zero hold is a deliberate "stop asking me": carry it out at once rather
    // than flashing an overlay that cannot be read, let alone reacted to.
    // runDecision() signals completion itself, so nothing is emitted here --
    // doing both would advance the queue twice for one URL.
    if (holdMs() <= 0) {
        m_mode = Mode::Hold;
        runDecision();
        // Unless the rule pointed at a target that no longer exists, in which
        // case runDecision() falls back to asking and the picker has to appear
        // after all.
        if (m_mode == Mode::Picker) {
            present(activationToken);
        }
        return;
    }

    m_mode = Mode::Hold;
    present(activationToken);
}

void PickerController::present(const QString &activationToken)
{
    m_showTimer.start();
    Q_EMIT contextChanged();

    if (!m_window) {
        return;
    }

    if (auto *screen = m_window->screen() ? m_window->screen() : QGuiApplication::primaryScreen()) {
        m_window->setGeometry(screen->geometry());
    }

    // Time to the first presented frame is the number that matters: it is what
    // the user experiences between clicking a link and being asked.
    if (auto *quick = qobject_cast<QQuickWindow *>(m_window)) {
        connect(
            quick,
            &QQuickWindow::frameSwapped,
            this,
            [this] {
                qCDebug(LOG_PICKER) << "painted in" << m_showTimer.elapsed() << "ms; since process start"
                                    << startupTimer().elapsed() << "ms";
            },
            Qt::SingleShotConnection);
    }

    m_window->show();

    if (!activationToken.isEmpty()) {
        KWindowSystem::setCurrentXdgActivationToken(activationToken);
    }
    KWindowSystem::activateWindow(m_window);

    QTimer::singleShot(kWatchdogMs, this, [this] {
        if (m_window && m_window->isVisible()) {
            qCWarning(LOG_PICKER) << "watchdog fired; force-hiding";
            hidePicker();
            Q_EMIT finished();
        }
    });
}

void PickerController::checkHeldModifiers()
{
    if (m_mode != Mode::Hold) {
        return;
    }

    // The modifier is held in the application that opened the link, and a
    // Wayland client cannot read global modifier state. What it can do is see
    // which keys were already down when it took keyboard focus -- which is
    // exactly this moment, and the only point where the gesture is observable.
    const Qt::KeyboardModifiers held = QGuiApplication::queryKeyboardModifiers();
    qCDebug(LOG_PICKER) << "modifiers held at focus:" << held;

    if (held.testFlag(kOverrideModifier)) {
        qCDebug(LOG_PICKER) << "override modifier held; showing the picker instead";
        interruptHold();
    }
}

void PickerController::interruptHold()
{
    if (m_mode != Mode::Hold) {
        return;
    }
    m_mode = Mode::Picker;
    Q_EMIT contextChanged();
}

void PickerController::holdCompleted()
{
    if (m_mode != Mode::Hold) {
        return;
    }
    // hidePicker() is deliberately not called here: runDecision() hides the
    // window itself once the launch has gone through. Unmapping the surface
    // first loses the activation token that is still being minted from it.
    runDecision();
}

void PickerController::runDecision()
{
    if (m_decision.action == RuleAction::Copy) {
        // On Wayland the copying client owns the selection and has to stay
        // alive to serve it. That holds for the daemon, but a one-shot run
        // would drop the content the moment it exits unless a clipboard
        // manager (Plasma runs one) has taken a copy first.
        QGuiApplication::clipboard()->setText(m_url.toString());
        qCDebug(LOG_PICKER) << "copied to clipboard; read back:" << QGuiApplication::clipboard()->text();
        hidePicker();
        Q_EMIT finished();
        return;
    }

    const Target target = targetById(m_decision.targetId);
    if (target.id.isEmpty()) {
        // The rule names a target that is no longer installed. Asking is the
        // only honest response; silently picking something else would be worse.
        qCWarning(LOG_PICKER) << "decision names unknown target" << m_decision.targetId << "- asking instead";
        m_mode = Mode::Picker;
        Q_EMIT contextChanged();
        return;
    }

    m_launcher->launch(target, m_url, m_decision.privateWindow, m_window, [this](LaunchResult) {
        hidePicker();
        Q_EMIT finished();
    });
}

bool PickerController::launchFallback(const QUrl &url)
{
    const auto &targets = m_model->targets();
    for (const Target &target : targets) {
        if (target.kind == TargetKind::Browser) {
            m_launcher->launch(target, url, false, m_window);
            return true;
        }
    }
    return false;
}

void PickerController::choose(int index, bool privateWindow, bool remember)
{
    const Target target = m_model->at(index);
    if (target.id.isEmpty()) {
        return;
    }

    if (remember && m_store) {
        m_store->remember(m_url.host(), target.id, privateWindow);
    }

    // The window stays mapped until the launch has gone through: the
    // activation token is minted from it asynchronously, and hiding it first
    // loses the token and with it the browser's claim to the foreground.
    m_launcher->launch(target, m_url, privateWindow, m_window, [this](LaunchResult) {
        hidePicker();
        Q_EMIT finished();
    });
}

void PickerController::copyUrl()
{
    QGuiApplication::clipboard()->setText(m_url.toString());
    hidePicker();
    Q_EMIT finished();
}

void PickerController::cancel()
{
    hidePicker();
    Q_EMIT finished();
}

void PickerController::hidePicker()
{
    if (m_window) {
        m_window->hide();
    }
}

} // namespace Lob
