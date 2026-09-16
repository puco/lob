#pragma once

#include "Target.h"

#include <QObject>
#include <QSharedPointer>
#include <QStringList>
#include <QUrl>

#include <functional>

class QWindow;

namespace Lob
{

struct LaunchOperation {
    bool cancelled = false;
    bool dispatched = false;
    bool cancel()
    {
        if (dispatched) {
            return false;
        }
        cancelled = true;
        return true;
    }
};

struct LaunchResult {
    enum Outcome { Started, Failed, Cancelled } outcome = Started;
    QString error;
};

class Launcher : public QObject
{
    Q_OBJECT

public:
    explicit Launcher(QObject *parent = nullptr);

    /**
     * Opens @p url in @p target. If @p window is a live Wayland window, a fresh
     * activation token is minted from it first so the browser comes to the
     * front instead of being demoted by focus-stealing prevention.
     *
     * Completion reports dispatch success, failure, or cancellation. It does
     * not report page-load success. Keep the window mapped until completion.
     */
    using Completion = std::function<void(LaunchResult)>;
    virtual void launch(const Target &target,
                        const QUrl &url,
                        bool privateWindow,
                        QWindow *window,
                        Completion completion = {},
                        const QString &activationToken = {},
                        QSharedPointer<LaunchOperation> operation = {});

    /// Command line this target would run. Public for testing.
    static QStringList buildArgv(const Target &target, const QUrl &url, bool privateWindow);

    /**
     * Rejects shells and re-exec wrappers as a launch target. Walks every hop
     * of the symlink chain by basename rather than resolving it outright --
     * canonicalising first would collapse python3 -> python3.14 and launder a
     * blocked name past the check.
     */
    static bool execIsSafe(const QString &execPath);

    /// Whether a whole desktop-entry command line is safe to run, including
    /// what an `env` wrapper or `flatpak run` would end up executing.
    static bool commandIsSafe(const QStringList &command);

    /// Index of the program inside a command line, past any `env` wrapper.
    /// -1 when the wrapper cannot be read with confidence.
    static int programIndex(const QStringList &command);

Q_SIGNALS:
    void launchFailed(const QString &message);

private:
    void doLaunch(const Target &target, const QUrl &url, bool privateWindow, const QString &activationToken, Completion completion);
};

} // namespace Lob
