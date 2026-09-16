#include "Controller.h"

#include "TrayController.h"
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

    m_picker = new PickerController(this);
    m_picker->refreshTargets();

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

void Controller::enqueue(const QUrl &url, const QString &token, bool forcePicker)
{
    // While paused, links still open -- they just skip the question. Queueing
    // them up to ask later would be worse than picking a sensible browser now.
    if (m_tray && m_tray->isPaused() && !forcePicker) {
        if (!m_picker->launchFallback(url)) {
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
    m_picker->showFor(pending.url, pending.token);
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
