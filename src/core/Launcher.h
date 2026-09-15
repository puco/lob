#pragma once

#include "Target.h"

#include <QObject>
#include <QStringList>
#include <QUrl>

class QWindow;

namespace Lob
{

class Launcher : public QObject
{
    Q_OBJECT

public:
    explicit Launcher(QObject *parent = nullptr);

    /**
     * Opens @p url in @p target. If @p window is a live Wayland window, a fresh
     * activation token is minted from it first so the browser comes to the
     * front instead of being demoted by focus-stealing prevention.
     */
    void launch(const Target &target, const QUrl &url, bool privateWindow, QWindow *window);

    /// Command line this target would run. Public for testing.
    static QStringList buildArgv(const Target &target, const QUrl &url, bool privateWindow);

    /**
     * Rejects shells and re-exec wrappers as a launch target. Walks every hop
     * of the symlink chain by basename rather than resolving it outright --
     * canonicalising first would collapse python3 -> python3.14 and launder a
     * blocked name past the check.
     */
    static bool execIsSafe(const QString &execPath);

Q_SIGNALS:
    void launchFailed(const QString &message);

private:
    void doLaunch(const Target &target, const QUrl &url, bool privateWindow, const QString &activationToken);
};

} // namespace Lob
