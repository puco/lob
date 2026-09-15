#include "Launcher.h"

#include <KIO/ApplicationLauncherJob>
#include <KService>
#include <KServiceAction>
#include <KWaylandExtras>
#include <KWindowSystem>

#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QSharedPointer>
#include <QTimer>
#include <QWindow>

namespace Lob
{

namespace
{

/// Anything that would re-interpret its arguments instead of being a browser.
/// Lob is a network-facing URL handler whose target list is user-editable, so
/// this check is load-bearing rather than defensive decoration.
const QStringList kBlockedExecNames = {
    QStringLiteral("sh"),      QStringLiteral("bash"),         QStringLiteral("zsh"),
    QStringLiteral("dash"),    QStringLiteral("fish"),         QStringLiteral("ksh"),
    QStringLiteral("csh"),     QStringLiteral("tcsh"),         QStringLiteral("env"),
    QStringLiteral("xargs"),   QStringLiteral("sudo"),         QStringLiteral("doas"),
    QStringLiteral("pkexec"),  QStringLiteral("flatpak-spawn"),QStringLiteral("ssh"),
    QStringLiteral("python"),  QStringLiteral("python2"),      QStringLiteral("python3"),
    QStringLiteral("perl"),    QStringLiteral("ruby"),         QStringLiteral("node"),
    QStringLiteral("awk"),     QStringLiteral("gawk"),         QStringLiteral("sed"),
    QStringLiteral("eval"),    QStringLiteral("nohup"),        QStringLiteral("setsid"),
};

constexpr int kMaxSymlinkHops = 16;

/// ~300ms is long enough for a healthy compositor roundtrip and short enough
/// that a click never appears to do nothing. Launching without a token (browser
/// opens, possibly unfocused) beats not launching at all.
constexpr int kActivationTokenTimeoutMs = 300;

} // namespace

Launcher::Launcher(QObject *parent)
    : QObject(parent)
{
}

bool Launcher::execIsSafe(const QString &execPath)
{
    if (execPath.isEmpty()) {
        return false;
    }

    QString current = execPath;
    for (int hop = 0; hop < kMaxSymlinkHops; ++hop) {
        const QFileInfo info(current);
        if (kBlockedExecNames.contains(info.fileName())) {
            return false;
        }
        if (!info.isSymLink()) {
            return true;
        }
        const QString next = info.symLinkTarget();
        if (next.isEmpty() || next == current) {
            return true;
        }
        current = next;
    }
    return false; // symlink loop or absurd chain; refuse rather than guess
}

QStringList Launcher::buildArgv(const Target &target, const QUrl &url, bool privateWindow)
{
    QStringList argv{target.execPath};

    const bool wantPrivate = privateWindow && target.supportsPrivate();

    switch (target.family) {
    case EngineFamily::Gecko:
        if (!target.profileKey.isEmpty()) {
            // --profile takes a path, which is what the scanner hands us; names
            // are not unique across forks. Deliberately no --no-remote: without
            // it the URL reaches the already-running instance for that profile,
            // which is what we want.
            argv << QStringLiteral("--profile") << target.profileKey;
        }
        argv << (wantPrivate ? target.privateFlag : QStringLiteral("--new-tab"));
        break;

    case EngineFamily::Chromium:
        if (!target.profileKey.isEmpty()) {
            // Must precede the URL, and must stay a single argv element: a
            // space in the profile directory name otherwise truncates the value
            // and the remainder is parsed as a URL.
            argv << QStringLiteral("--profile-directory=") + target.profileKey;
        }
        if (wantPrivate) {
            argv << target.privateFlag;
        }
        break;

    case EngineFamily::Unknown:
        if (wantPrivate) {
            argv << target.privateFlag;
        }
        break;
    }

    argv << url.toString();
    return argv;
}

void Launcher::launch(const Target &target, const QUrl &url, bool privateWindow, QWindow *window)
{
    if (!execIsSafe(target.execPath)) {
        Q_EMIT launchFailed(tr("Refusing to launch %1: not a browser executable.").arg(target.execPath));
        return;
    }

    if (!window || !KWindowSystem::isPlatformWayland()) {
        doLaunch(target, url, privateWindow, QString());
        return;
    }

    // The inbound token that raised the picker is single-use and already spent,
    // so mint a fresh one for the browser. Pass a real app id -- an empty one
    // makes KWin more likely to reject the token.
    const QString appId = QFileInfo(target.storageId).completeBaseName();
    const quint32 serial = KWaylandExtras::lastInputSerial(window);

    auto resolved = QSharedPointer<bool>::create(false);
    auto finish = [this, target, url, privateWindow, resolved](const QString &token) {
        if (*resolved) {
            return;
        }
        *resolved = true;
        doLaunch(target, url, privateWindow, token);
    };

    KWaylandExtras::xdgActivationToken(window, serial, appId).then(this, finish);
    QTimer::singleShot(kActivationTokenTimeoutMs, this, [finish] {
        finish(QString());
    });
}

void Launcher::doLaunch(const Target &target, const QUrl &url, bool privateWindow, const QString &activationToken)
{
    const bool needsArgv = !target.profileKey.isEmpty();

    // Without a profile to select, KIO knows better than we do: it expands
    // field codes, honours Terminal=, and -- crucially -- reaches
    // DBusActivatable browsers, where the environment variable below would
    // never arrive because dbus-daemon, not us, spawns the process.
    if (!needsArgv) {
        const KService::Ptr service = KService::serviceByStorageId(target.storageId);
        if (service) {
            KIO::ApplicationLauncherJob *job = nullptr;

            if (privateWindow && target.supportsPrivate()) {
                const auto actions = service->actions();
                for (const KServiceAction &action : actions) {
                    if (action.name() == QLatin1String("new-private-window")) {
                        job = new KIO::ApplicationLauncherJob(action, this);
                        break;
                    }
                }
            }
            if (!job) {
                job = new KIO::ApplicationLauncherJob(service, this);
            }

            job->setUrls({url});
            if (!activationToken.isEmpty()) {
                job->setStartupId(activationToken.toUtf8());
            }
            job->start();
            return;
        }
    }

    const QStringList argv = buildArgv(target, url, privateWindow);
    const QString program = argv.constFirst();
    const QStringList arguments = argv.mid(1);

    // The static startDetached overload forks from our own live environment and
    // takes no QProcessEnvironment, so the token has to be set around the call
    // and restored immediately after. Safe only because this runs on the single
    // main thread -- if this daemon ever grows a worker thread, this becomes a
    // data race and must move to a QProcess instance with setProcessEnvironment.
    const bool hasToken = !activationToken.isEmpty();
    const bool hadPrevious = qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN");
    const QByteArray previous = hadPrevious ? qgetenv("XDG_ACTIVATION_TOKEN") : QByteArray();

    if (hasToken) {
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    }

    const bool started = QProcess::startDetached(program, arguments);

    if (hasToken) {
        if (hadPrevious) {
            qputenv("XDG_ACTIVATION_TOKEN", previous);
        } else {
            qunsetenv("XDG_ACTIVATION_TOKEN");
        }
    }

    if (!started) {
        Q_EMIT launchFailed(tr("Could not start %1.").arg(program));
    }
}

} // namespace Lob
