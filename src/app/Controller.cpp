#include "Controller.h"

#include "TrayController.h"
#include "core/RuleEngine.h"
#include "core/RuleStore.h"
#include "core/UrlSanitizer.h"
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
    connect(m_picker, &PickerController::errorOccurred, this, [](const QString &message) {
        qCWarning(LOG_CONTROLLER) << message;
        KNotification::event(KNotification::Error, i18n("Lob"), message, QStringLiteral("internet-web-browser"));
    });

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

    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith(QLatin1String("--"))) {
            continue;
        }

        const QUrl url = QUrl::fromUserInput(arg);
        QString reason;
        if (!UrlSanitizer::isRoutable(url, &reason)) {
            qCWarning(LOG_CONTROLLER) << "refusing" << arg << ":" << reason;
            continue;
        }
        enqueue(url, activationToken, forcePicker);
    }
}

void Controller::handleUrls(const QList<QUrl> &urls, const QString &activationToken)
{
    for (const QUrl &url : urls) {
        QString reason;
        if (!UrlSanitizer::isRoutable(url, &reason)) {
            qCWarning(LOG_CONTROLLER) << "refusing" << url.toString() << ":" << reason;
            continue;
        }
        enqueue(url, activationToken, false);
    }
}

void Controller::enqueue(const QUrl &rawUrl, const QString &token, bool forcePicker)
{
    // Strip before the rules see it, so a rule matching on query parameters
    // matches what will actually be opened rather than what arrived.
    const QUrl url = m_store->stripTracking()
        ? UrlSanitizer::strip(rawUrl, m_store->trackingParameters())
        : rawUrl;
    if (url != rawUrl) {
        qCDebug(LOG_CONTROLLER) << "stripped tracking parameters:" << rawUrl.toString() << "->" << url.toString();
    }

    // While paused, links still open -- they just skip the question. Queueing
    // them up to ask later would be worse than picking a sensible browser now.
    if (m_tray && m_tray->isPaused() && !forcePicker) {
        if (!m_picker->launchFallback(url, token, m_store->fallbackTargetId())) {
            qCWarning(LOG_CONTROLLER) << "paused, but no browser to fall back to";
        }
        return;
    }

    m_queue.enqueue({url, token, forcePicker});
    processQueue();
}

void Controller::processQueue()
{
    if (m_busy || m_queue.isEmpty()) {
        return;
    }

    const PendingUrl pending = m_queue.dequeue();
    m_busy = true;

    if (pending.forcePicker) {
        m_picker->showPicker(pending.url, pending.token);
        return;
    }

    const Decision decision = RuleEngine::decide(pending.url, m_store->rules(), m_store->fallbackTargetId());
    if (decision.opensWithoutAsking()) {
        m_picker->showHold(pending.url, pending.token, decision);
    } else {
        m_picker->showPicker(pending.url, pending.token);
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

    if (!m_daemon) {
        QGuiApplication::quit();
    }
}

} // namespace Lob
