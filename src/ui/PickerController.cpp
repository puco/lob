#include "PickerController.h"

#include "TargetModel.h"
#include "core/Startup.h"
#include "core/TargetRegistry.h"

#include <LayerShellQt/Window>

#include <KWindowSystem>

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>
#include <QQuickWindow>
#include <QWindow>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(LOG_PICKER, "lob.picker")

namespace Lob
{

namespace
{
/// With KeyboardInteractivityExclusive a picker that fails to hide is a
/// session-level keyboard trap, so it always gets force-hidden eventually.
constexpr int kWatchdogMs = 60'000;
} // namespace

PickerController::PickerController(QObject *parent)
    : QObject(parent)
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

void PickerController::refreshTargets()
{
    m_model->setTargets(TargetRegistry::discover(QStringLiteral(LOB_APP_ID ".desktop")));
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

    // Layer-shell is what lets the picker take the keyboard unconditionally
    // instead of losing a focus-stealing-prevention argument with the browser
    // that is about to open. On X11 there is no such protocol, so we fall back
    // to an ordinary always-on-top window.
    // Exclusive keyboard is unforgiving while iterating on the UI: a QML error
    // that leaves the overlay up takes the session's keyboard with it. This
    // escape hatch runs the picker as an ordinary window instead.
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

    return true;
}

void PickerController::showFor(const QUrl &url, const QString &activationToken)
{
    m_showTimer.start();
    m_url = url;
    Q_EMIT urlChanged();

    if (!m_window) {
        return;
    }

    if (auto *screen = m_window->screen() ? m_window->screen() : QGuiApplication::primaryScreen()) {
        m_window->setGeometry(screen->geometry());
    }

    qCDebug(LOG_PICKER) << "showFor" << url.toString() << "window" << m_window;
    // Time to the first presented frame is the number that matters: it is what
    // the user experiences between clicking a link and being asked.
    if (auto *quick = qobject_cast<QQuickWindow *>(m_window)) {
        connect(
            quick,
            &QQuickWindow::frameSwapped,
            this,
            [this] {
                qCDebug(LOG_PICKER) << "picker painted in" << m_showTimer.elapsed()
                                      << "ms; since process start" << startupTimer().elapsed() << "ms";
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
            qCWarning(LOG_PICKER) << "watchdog fired";
            hidePicker();
            Q_EMIT finished();
        }
    });
}

bool PickerController::hasTargets() const
{
    return m_model->rowCount() > 0;
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

void PickerController::choose(int index, bool privateWindow)
{
    qCDebug(LOG_PICKER) << "choose" << index << "private" << privateWindow;
    const Target target = m_model->at(index);
    if (target.id.isEmpty()) {
        return;
    }

    // Mint the outbound token while the picker still holds focus, then hide it.
    m_launcher->launch(target, m_url, privateWindow, m_window);
    hidePicker();
    Q_EMIT finished();
}

void PickerController::copyUrl()
{
    qCDebug(LOG_PICKER) << "copyUrl";
    QGuiApplication::clipboard()->setText(m_url.toString());
    hidePicker();
    Q_EMIT finished();
}

void PickerController::cancel()
{
    qCDebug(LOG_PICKER) << "cancel";
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
