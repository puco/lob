#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QUrl>

class QQmlApplicationEngine;

namespace Lob
{

class PickerController;
class TrayController;

/**
 * Owns the process: the QML engine, the picker, the tray, and the queue of
 * URLs waiting to be routed.
 *
 * Runs either as a resident daemon (the normal case) or as a one-shot that
 * quits once the URL it was started for has been dealt with.
 */
class Controller : public QObject
{
    Q_OBJECT

public:
    explicit Controller(QObject *parent = nullptr);

    void setDaemonMode(bool daemon);
    bool isDaemonMode() const;

    /// Builds the QML engine and picker window. In daemon mode this is done up
    /// front so the first link after login is as fast as every later one.
    bool initialize();

    /// From KDBusService::activateRequested -- argv of a second `lob <url>`.
    void handleArgs(const QStringList &args, const QString &activationToken);

    /// From KDBusService::openRequested -- URLs from KIO/GIO.
    void handleUrls(const QList<QUrl> &urls, const QString &activationToken);

    /// True while a picker is on screen.
    bool isBusy() const;

private:
    struct PendingUrl {
        QUrl url;
        QString token;
        bool forcePicker = false;
    };

    void enqueue(const QUrl &url, const QString &token, bool forcePicker);
    void processQueue();
    void onPickerFinished();

    QQmlApplicationEngine *m_engine = nullptr;
    PickerController *m_picker = nullptr;
    TrayController *m_tray = nullptr;

    // Each URL carries its own activation token: tokens are single-use, so two
    // links clicked in quick succession must not share one.
    QQueue<PendingUrl> m_queue;
    bool m_busy = false;
    bool m_daemon = false;
};

} // namespace Lob
