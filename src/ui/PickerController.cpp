#include "PickerController.h"

#include "TargetModel.h"
#include "core/RuleStore.h"
#include "core/Startup.h"
#include "core/TargetRegistry.h"

#include <LayerShellQt/Window>

#include <KLocalizedString>
#include <KWindowSystem>
#include <KSycoca>

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QPointer>
#include <QFileInfo>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(LOG_PICKER, "lob.picker")

namespace Lob
{

namespace
{
/// Holding this while clicking a link overrides whatever rule would fire.
constexpr Qt::KeyboardModifier kOverrideModifier = Qt::ShiftModifier;

/// Desktop files and profile stores change in bursts (an install, a browser
/// rewriting its profile list), so a change is a reason to look again shortly,
/// not immediately.
constexpr int kRefreshDebounceMs = 100;

/// While a link is being acted on, the list underneath must not move. The
/// refresh waits, rather than being dropped.
constexpr int kRefreshWhileBusyMs = 500;
} // namespace

PickerController::PickerController(RuleStore *store, QObject *parent, Launcher *launcher, int watchdogMs)
    : QObject(parent)
    , m_store(store)
    , m_model(new TargetModel(this))
    , m_launcher(launcher ? launcher : new Launcher(this))
{
    m_holdTimer.setSingleShot(true);
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(watchdogMs);
    connect(&m_holdTimer, &QTimer::timeout, this, &PickerController::holdCompleted);
    connect(&m_watchdog, &QTimer::timeout, this, &PickerController::watchdogExpired);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(kRefreshDebounceMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &PickerController::refreshTargets);
    const auto scheduleRefresh = [this] { m_refreshTimer.start(kRefreshDebounceMs); };
    connect(&m_targetWatcher, &QFileSystemWatcher::fileChanged, this, scheduleRefresh);
    connect(&m_targetWatcher, &QFileSystemWatcher::directoryChanged, this, scheduleRefresh);
    connect(KSycoca::self(), &KSycoca::databaseChanged, this, scheduleRefresh);
}

void PickerController::watchdogExpired()
{
    // With KeyboardInteractivityExclusive a picker that fails to hide is a
    // session-level keyboard trap, so it always gets taken down eventually.
    if (m_mode == Mode::Launching && m_operation && m_operation->dispatched && !m_launchStalled) {
        // The launch is already out of our hands: drop the input grab but keep
        // waiting for its result, which is still the honest answer to report.
        m_launchStalled = true;
        hidePicker();
        m_watchdog.start();
        return;
    }

    if (m_mode == Mode::Launching) {
        // Twice over and still nothing back. Give up on this link rather than
        // leave every later one queued behind it forever.
        qCWarning(LOG_PICKER) << "launch never reported back; abandoning it";
        m_error = i18n("The browser did not report back. The link may or may not have opened.");
        Q_EMIT errorOccurred(m_error);
        finish();
        return;
    }

    cancel();
}

QAbstractItemModel *PickerController::targetsModel() const
{
    return m_model;
}

QString PickerController::url() const
{
    return m_link.destination.toString();
}

QString PickerController::displayHost() const
{
    return m_link.destination.host();
}

QString PickerController::wrapperHost() const
{
    return m_link.wrapper;
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
        return i18n("remembered for %1", m_link.destination.host());
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
    return m_store && m_store->hasMemory(m_link.destination.host());
}

bool PickerController::hasTargets() const
{
    return m_model->rowCount() > 0;
}

Target PickerController::targetById(const QString &id, bool remembered) const
{
    return TargetRegistry::resolve(m_targets, id, remembered);
}

void PickerController::setCurrentIndex(int index)
{
    if (m_currentIndex == index) { return; }
    m_currentIndex = index;
    Q_EMIT selectionChanged();
}

void PickerController::refreshTargets()
{
    if (m_mode == Mode::Launching || m_mode == Mode::Hold) {
        m_refreshTimer.start(kRefreshWhileBusyMs);
        return;
    }

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

    watchTargetSources(targets);
    setTargets(targets);
}

void PickerController::setTargets(const QList<Target> &targets)
{
    // Rules and memories resolve against everything discovered; the list only
    // shows what is worth reading.
    m_targets = targets;

    const QList<Target> listed = TargetRegistry::withoutRedundantProfiles(targets);
    if (listed == m_model->targets()) {
        return;
    }

    // Keep the highlight on whatever it was on. A target that disappeared
    // takes the highlight back to the top rather than leaving none at all.
    const QString selected = m_model->at(m_currentIndex).id;
    m_model->setTargets(listed);

    int index = 0;
    for (int n = 0; n < listed.size(); ++n) {
        if (listed.at(n).id == selected) {
            index = n;
            break;
        }
    }
    setCurrentIndex(listed.isEmpty() ? -1 : index);
}

void PickerController::watchTargetSources(const QList<Target> &targets)
{
    // Watching the profile stores themselves is what makes a new browser
    // profile show up without a restart. Nothing is polled: a store that did
    // not exist when this ran is not watched, and the picker's own refresh is
    // the way in for that case.
    QStringList paths;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (QFileInfo::exists(config)) {
        paths << config;
    }
    for (const auto &target : targets) {
        if (target.dataDir.isEmpty()) {
            continue;
        }
        for (const auto &path : QStringList{target.dataDir, target.dataDir + QStringLiteral("/Local State"),
                                           target.dataDir + QStringLiteral("/profiles.ini")}) {
            if (QFileInfo::exists(path) && !paths.contains(path)) {
                paths << path;
            }
        }
    }

    const QStringList watched = m_targetWatcher.files() + m_targetWatcher.directories();
    for (const auto &path : watched) {
        if (!paths.contains(path)) {
            m_targetWatcher.removePath(path);
        }
    }
    for (const auto &path : std::as_const(paths)) {
        if (!watched.contains(path)) {
            m_targetWatcher.addPath(path);
        }
    }
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

    m_usingLayerShell = layer != nullptr;

    if (layer) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors({LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                           | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight});
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layer->setExclusiveZone(-1);
        layer->setScope(QStringLiteral("lob-picker"));
        layer->setCloseOnDismissed(false);
        layer->setWantsToBeOnActiveScreen(true);
    } else {
        m_window->setFlags(m_window->flags() | Qt::Dialog | Qt::WindowStaysOnTopHint);
    }

    connect(m_window, &QWindow::activeChanged, this, [this] {
        if (m_window->isActive()) {
            checkHeldModifiers();
            if (m_mode == Mode::Hold && m_decision.action == RuleAction::Copy && holdMs() == 0) {
                runDecision();
            }
        }
    });

    return true;
}

void PickerController::showPicker(const Link &link, const QString &activationToken)
{
    begin(link, activationToken);
    m_mode = Mode::Picker;
    m_decision = {};
    present(activationToken);
}

void PickerController::showHold(const Link &link, const QString &activationToken, const Decision &decision)
{
    begin(link, activationToken);
    m_decision = decision;

    // A zero hold is a deliberate "stop asking me": carry it out at once rather
    // than flashing an overlay that cannot be read, let alone reacted to.
    // runDecision() signals completion itself, so nothing is emitted here --
    // doing both would advance the queue twice for one URL.
    if (holdMs() <= 0) {
        m_mode = Mode::Hold;
        // Wayland selection ownership requires keyboard focus, even for an
        // automatic copy. Show the surface before setting the clipboard.
        if (decision.action == RuleAction::Copy && m_window && KWindowSystem::isPlatformWayland()) {
            present(activationToken);
            if (m_mode == Mode::Hold && m_window->isActive()) { runDecision(); }
            return;
        }
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
    if (m_mode == Mode::Hold) {
        m_holdTimer.start(holdMs());
    }
}

void PickerController::begin(const Link &link, const QString &activationToken)
{
    ++m_requestId;
    m_holdTimer.stop();
    m_watchdog.stop();
    if (m_operation) {
        m_operation->cancel();
    }
    m_operation.clear();
    m_error.clear();
    setCurrentIndex(0);
    m_link = link;
    m_activationToken = activationToken;
}

void PickerController::present(const QString &activationToken)
{
    m_showTimer.start();
    Q_EMIT contextChanged();
    m_watchdog.start();

    if (!m_window) {
        return;
    }

    // Under layer-shell the compositor owns both the output and the geometry:
    // the surface is anchored to all four edges and asks to be on the active
    // screen, so a geometry set here only fights the configure that follows.
    //
    // Without it -- X11, or LOB_NO_LAYERSHELL -- placement is ours, and a
    // resident daemon's window is still on whichever screen it was last shown
    // on. On one monitor that is always right and the question never comes up;
    // on two it is right only by luck. The pointer is the best available guess
    // at where the link was clicked, and on X11 it is an accurate one.
    if (!m_usingLayerShell) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = m_window->screen();
        }
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            m_window->setScreen(screen);
            m_window->setGeometry(screen->geometry());
        }
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
    m_activationToken.clear();
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
    m_holdTimer.stop();
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
        // Controller keeps even a one-shot process alive while it owns the
        // selection, so the clipboard can still serve the URL after dismissal.
        QGuiApplication::clipboard()->setText(m_link.destination.toString());
        Q_EMIT clipboardCopied();
        qCDebug(LOG_PICKER) << "copied URL to clipboard";
        finish();
        return;
    }

    const Target target = targetById(m_decision.targetId, decisionPredatesProfileIds());
    if (target.id.isEmpty()) {
        // The rule names a target that is no longer installed. Asking is the
        // only honest response; silently picking something else would be worse.
        qCWarning(LOG_PICKER) << "decision names unknown target" << m_decision.targetId << "- asking instead";
        m_mode = Mode::Picker;
        m_error = i18n("The configured browser or profile is unavailable or ambiguous. Choose a browser for this link.");
        Q_EMIT contextChanged();
        return;
    }

    // Rewriting the memory once a legacy one has resolved records the profile
    // it meant, so the same link cannot become ambiguous again later.
    startLaunch(target, m_decision.privateWindow, decisionPredatesProfileIds());
}

bool PickerController::decisionPredatesProfileIds() const
{
    return m_decision.source == Decision::Source::Memory && m_decision.legacyTarget;
}

bool PickerController::launchFallback(const Link &link, const QString &activationToken, const QString &targetId)
{
    const Target target = targetById(targetId);
    if (target.id.isEmpty() || target.kind != TargetKind::Browser) { return false; }
    begin(link, activationToken);
    startLaunch(target, false, false);
    return true;
}

void PickerController::choose(int index, bool privateWindow, bool remember)
{
    if (m_mode != Mode::Picker) {
        return;
    }
    const Target target = m_model->at(index);
    if (target.id.isEmpty()) {
        return;
    }

    startLaunch(target, privateWindow, remember);
}

void PickerController::startLaunch(const Target &target, bool privateWindow, bool remember)
{
    m_holdTimer.stop();
    m_launchStalled = false;
    m_watchdog.start();
    m_mode = Mode::Launching;
    Q_EMIT contextChanged();
    const auto id = ++m_requestId;
    const QString host = m_link.destination.host();
    m_operation = QSharedPointer<LaunchOperation>::create();
    QPointer<PickerController> guard(this);
    // The browser is handed toOpen: the same URL, unless a link scanner was
    // read through, in which case it is the scanner's own URL that must be
    // visited even though the decision was made about where it leads.
    m_launcher->launch(target, m_link.toOpen, privateWindow, m_window,
        [guard, id, target, host, privateWindow, remember](LaunchResult result) {
            if (!guard || guard->m_requestId != id || guard->m_mode != Mode::Launching) {
                return;
            }
            if (result.outcome == LaunchResult::Failed) {
                guard->m_error = result.error;
                guard->m_mode = Mode::Picker;
                guard->present(guard->m_activationToken);
                Q_EMIT guard->errorOccurred(result.error);
                return;
            }
            if (result.outcome == LaunchResult::Started && remember && guard->m_store) {
                guard->m_store->remember(host, target.id, privateWindow);
            }
            guard->finish();
        }, m_activationToken, m_operation);
}

void PickerController::finish()
{
    if (m_mode == Mode::Idle) { return; }
    ++m_requestId;
    m_mode = Mode::Idle;
    m_holdTimer.stop();
    m_watchdog.stop();
    m_operation.clear();
    hidePicker();
    Q_EMIT contextChanged();
    Q_EMIT finished();
}

void PickerController::copyUrl()
{
    if (m_mode != Mode::Picker) { return; }
    QGuiApplication::clipboard()->setText(m_link.destination.toString());
    Q_EMIT clipboardCopied();
    finish();
}

void PickerController::cancel()
{
    if (m_mode == Mode::Launching && m_operation && !m_operation->cancel()) {
        return; // dispatch is already in progress; wait for its result
    }
    finish();
}

void PickerController::hidePicker()
{
    if (m_window) {
        m_window->hide();
    }
}

} // namespace Lob
