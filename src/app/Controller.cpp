#include "Controller.h"

#include "TrayController.h"
#include "core/RuleEngine.h"
#include "core/RuleStore.h"
#include "core/UrlSanitizer.h"
#include "core/DefaultBrowserManager.h"
#include "ui/PickerController.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <KNotification>

#include <QClipboard>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QTimer>

Q_LOGGING_CATEGORY(LOG_CONTROLLER, "lob.controller")

namespace Lob
{

namespace
{
/// How long a one-shot run keeps serving a URL it copied to the clipboard,
/// when no clipboard manager takes it over.
constexpr int kClipboardHoldMs = 5 * 60 * 1000;
} // namespace

Controller::Controller(QObject *parent)
    : QObject(parent)
{
}

void Controller::setDaemonMode(bool daemon)
{
    m_daemon = daemon;
}

bool Controller::isDaemonMode() const
{
    return m_daemon;
}

bool Controller::isBusy() const
{
    return m_busy;
}

bool Controller::initialize()
{
    m_engine = new QQmlApplicationEngine(this);
    KLocalization::setupLocalizedContext(m_engine);

    m_store = new RuleStore(this);
    m_picker = new PickerController(m_store, this);
    connect(m_store, &RuleStore::errorOccurred, m_picker, &PickerController::errorOccurred);
    m_picker->refreshTargets();

    // An edit to rules.json outside the app takes effect without a restart.
    connect(m_store, &RuleStore::changed, m_picker, &PickerController::refreshTargets);

    // Building the window now rather than on first use is the whole point of
    // running as a daemon: otherwise the first link after login still pays the
    // full QML engine and scenegraph cost.
    if (!m_picker->ensureWindow(m_engine)) {
        return false;
    }

    connect(m_picker, &PickerController::finished, this, &Controller::onPickerFinished);
    connect(m_picker, &PickerController::clipboardCopied, this, [this] {
        m_servingClipboard = QGuiApplication::clipboard()->ownsClipboard();
        if (m_servingClipboard && !m_daemon) {
            // Nothing is obliged to take the selection from us, and on Wayland
            // it dies with this process. Outliving the copy by a few minutes
            // covers a paste; outliving the session does not, so a one-shot run
            // gives it up rather than becoming a process nobody knows about.
            QTimer::singleShot(kClipboardHoldMs, this, [this] { releaseClipboard(); });
        }
    });
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (m_servingClipboard && !QGuiApplication::clipboard()->ownsClipboard()) {
            releaseClipboard();
        }
    });
    connect(m_picker, &PickerController::errorOccurred, this, [](const QString &message) {
        qCWarning(LOG_CONTROLLER) << message;
        KNotification::event(KNotification::Error, i18n("Lob"), message, QStringLiteral("internet-web-browser"));
    });
    if (!m_store->lastError().isEmpty()) {
        QTimer::singleShot(0, m_picker, [this] { Q_EMIT m_picker->errorOccurred(m_store->lastError()); });
    }

    if (m_daemon) {
        m_tray = new TrayController(this);

        connect(m_tray, &TrayController::quitRequested, qGuiApp, &QGuiApplication::quit);
        connect(m_tray, &TrayController::routeClipboardRequested, this, [this] {
            const QUrl url = QUrl::fromUserInput(QGuiApplication::clipboard()->text().trimmed());
            QString reason;
            if (UrlSanitizer::isRoutable(url, &reason)) {
                enqueue(url, QString(), true);
            } else {
                KNotification::event(KNotification::Error, i18n("Lob"), reason, QStringLiteral("internet-web-browser"));
            }
        });
    }

    return true;
}

void Controller::handleArgs(const QStringList &args, const QString &activationToken)
{
    const bool forcePicker = args.contains(QStringLiteral("--pick"));
    QString token = activationToken;

    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith(QLatin1String("--"))) {
            continue;
        }

        const QUrl url = QUrl::fromUserInput(arg);
        QString reason;
        if (!UrlSanitizer::isRoutable(url, &reason)) {
            qCWarning(LOG_CONTROLLER) << "refusing URL:" << reason;
            continue;
        }
        enqueue(url, token, forcePicker);
        token.clear(); // activation tokens are single-use, including multi-URL requests
    }
}

void Controller::handleUrls(const QList<QUrl> &urls, const QString &activationToken)
{
    QString token = activationToken;
    for (const QUrl &url : urls) {
        QString reason;
        if (!UrlSanitizer::isRoutable(url, &reason)) {
            qCWarning(LOG_CONTROLLER) << "refusing URL:" << reason;
            continue;
        }
        enqueue(url, token, false);
        token.clear();
    }
}

void Controller::enqueue(const QUrl &rawUrl, const QString &token, bool forcePicker)
{
    m_acceptedAnyUrl = true;

    // A link clicked in a chat client or a search result arrives wrapped in a
    // redirector, so read it through to where it actually goes first: asking
    // about the redirector, and remembering it, would answer a question about
    // a host that every other link goes through too.
    Link link = m_store->unwrapRedirects() ? RedirectUnwrapper::unwrap(rawUrl, m_store->redirectWrappers())
                                           : Link::plain(rawUrl);
    if (link.wasWrapped()) {
        qCDebug(LOG_CONTROLLER) << "unwrapped a link from" << link.wrapper << "to host" << link.destination.host();
    }

    // Strip before the rules see it, so a rule matching on query parameters
    // matches what will actually be opened rather than what arrived.
    if (m_store->stripTracking()) {
        const QStringList patterns = m_store->trackingParameters();
        const QUrl destination = UrlSanitizer::strip(link.destination, patterns);
        if (destination != link.destination) {
            qCDebug(LOG_CONTROLLER) << "stripped tracking parameters for host" << destination.host();
        }
        // The two are the same URL unless a link scanner is being left intact,
        // and that one has to keep the parameters it was given.
        link.toOpen = link.toOpen == link.destination ? destination : link.toOpen;
        link.destination = destination;
    }

    m_queue.enqueue({link, token, forcePicker});
    processQueue();
}

void Controller::processQueue()
{
    if (m_busy || m_queue.isEmpty()) {
        return;
    }

    const PendingUrl pending = m_queue.dequeue();
    m_busy = true;

    // While paused, links still open -- they just skip the question. Queueing
    // them up to ask later would be worse than picking a sensible browser now:
    // the configured fallback, else whatever handled links before we did. Only
    // if neither exists is asking better than dropping the link.
    if (m_tray && m_tray->isPaused() && !pending.forcePicker) {
        const QString previous = DefaultBrowserManager::previousHandler(pending.link.destination.scheme());
        if (m_picker->launchFallback(pending.link, pending.token, m_store->fallbackTargetId())
            || m_picker->launchFallback(pending.link, pending.token, previous)) {
            return;
        }
        m_picker->showPicker(pending.link, pending.token);
        return;
    }

    if (pending.forcePicker) {
        m_picker->showPicker(pending.link, pending.token);
        return;
    }

    const Decision decision = RuleEngine::decide(pending.link.destination, m_store->rules(), m_store->fallbackTargetId());
    if (decision.opensWithoutAsking()) {
        m_picker->showHold(pending.link, pending.token, decision);
    } else {
        m_picker->showPicker(pending.link, pending.token);
    }
}

void Controller::onPickerFinished()
{
    m_busy = false;

    if (!m_queue.isEmpty()) {
        // Let the compositor settle between overlays rather than swapping the
        // surface contents underneath a keypress that is already in flight.
        QTimer::singleShot(0, this, &Controller::processQueue);
        return;
    }

    if (!m_daemon && !m_servingClipboard) {
        quitWhenIdle();
    }
}

void Controller::releaseClipboard()
{
    if (!m_servingClipboard) {
        return;
    }
    m_servingClipboard = false;
    if (!m_daemon) {
        quitWhenIdle();
    }
}

void Controller::quitWhenIdle()
{
    // Deferred: a link may still be queued behind the one that just finished,
    // and exiting from inside its own completion would strand it.
    QTimer::singleShot(0, this, [this] {
        if (!m_busy && m_queue.isEmpty() && !m_servingClipboard) {
            QCoreApplication::exit(0);
        }
    });
}

} // namespace Lob
