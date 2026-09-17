#include "Launcher.h"
#include "UrlSanitizer.h"

#include <KLocalizedString>

#include <KIO/ApplicationLauncherJob>
#include <KService>
#include <KServiceAction>
#include <KWaylandExtras>
#include <KWindowSystem>
#include <KShell>

#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSharedPointer>
#include <QTimer>
#include <QWindow>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Lob
{

namespace
{

/// Anything that would re-interpret its arguments instead of being a browser.
/// This is not a sandbox for untrusted desktop files -- launch arguments never
/// go through a shell -- but the target list is user-editable and a mistake in
/// it should not turn a URL into a command.
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

constexpr auto kCommandOption = QLatin1String("--command=");

} // namespace

int Launcher::programIndex(const QStringList &command)
{
    // `env VAR=value -- program ...` is a common desktop-entry wrapper. Where
    // the program actually starts decides both what the safety check inspects
    // and where our own arguments may go, so both ask this one question.
    if (command.isEmpty()) {
        return -1;
    }
    if (QFileInfo(command.constFirst()).fileName() != QLatin1String("env")) {
        return 0;
    }

    static const QRegularExpression assignment(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
    int program = 1;
    while (program < command.size() && assignment.match(command.at(program)).hasMatch()) {
        ++program;
    }
    if (command.value(program) == QLatin1String("--")) {
        ++program;
    }
    // env's own options (-i, -u NAME, -S "...") can replace the environment or
    // re-split the command line. Rather than reimplement env, refuse to read a
    // wrapper we do not fully understand.
    if (program >= command.size() || command.at(program).startsWith(QLatin1Char('-'))) {
        return -1;
    }
    return program;
}

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
    QStringList argv;
    QStringList command = target.launchCommand(privateWindow);
    if (command.isEmpty()) { command.append(target.execPath); }
    if (privateWindow && !target.supportsPrivate()) { return {}; }
    const QString profileKey = target.profileKey;

    const bool wantPrivate = privateWindow && target.supportsPrivate();

    QStringList flags;
    switch (target.family) {
    case EngineFamily::Gecko:
        if (!target.profileKey.isEmpty()) {
            // --profile takes a path, which is what the scanner hands us; names
            // are not unique across forks. Deliberately no --no-remote: without
            // it the URL reaches the already-running instance for that profile,
            // which is what we want.
            flags << QStringLiteral("--profile") << profileKey;
        }
        if (!command.contains(wantPrivate ? target.privateFlag : QStringLiteral("--new-tab"))) {
            flags << (wantPrivate ? target.privateFlag : QStringLiteral("--new-tab"));
        }
        break;

    case EngineFamily::Chromium:
        if (!target.profileKey.isEmpty()) {
            // Must precede the URL, and must stay a single argv element: a
            // space in the profile directory name otherwise truncates the value
            // and the remainder is parsed as a URL.
            flags << QStringLiteral("--profile-directory=") + profileKey;
        }
        if (wantPrivate && !command.contains(target.privateFlag)) {
            flags << target.privateFlag;
        }
        break;

    case EngineFamily::Unknown:
        if (wantPrivate && !command.contains(target.privateFlag)) {
            flags << target.privateFlag;
        }
        break;
    }

    // Options belong before the URL, and outside Flatpak's @@u ... @@ wrapper.
    bool inserted = false;
    bool hasUrl = false;
    int application = programIndex(command);
    if (application < 0) { return {}; }
    if (!target.flatpakId.isEmpty()) {
        const int app = command == target.command && target.applicationIndex >= 0
            ? target.applicationIndex : command.indexOf(target.flatpakId, application);
        if (app < 0) { return {}; }
        application = app;
    }
    for (int i = 0; i < command.size(); ++i) {
        const QString arg = command.at(i);
        if (i > application && wantPrivate && target.family == EngineFamily::Gecko
            && (arg == QLatin1String("--new-tab") || arg == QLatin1String("--new-window"))) { continue; }
        if (i > application && !target.profileKey.isEmpty()) {
            if (arg == QLatin1String("--profile") || arg == QLatin1String("-profile") || arg == QLatin1String("-P")
                || arg == QLatin1String("--profile-directory")) { ++i; continue; }
            if (arg.startsWith(QLatin1String("--profile=")) || arg.startsWith(QLatin1String("--profile-directory="))) { continue; }
        }
        if (i > application && !inserted && (arg == QLatin1String("@@u") || arg == QLatin1String("@@")
                          || arg == QLatin1String("--") || arg.contains(QLatin1String("%u")) || arg.contains(QLatin1String("%U")))) {
            argv << flags;
            inserted = true;
        }
        if (arg == QLatin1String("%u") || arg == QLatin1String("%U") || arg == QLatin1String("%f") || arg == QLatin1String("%F")) {
            if (!inserted) { argv << flags; inserted = true; }
            argv << url.toString(QUrl::FullyEncoded);
            hasUrl = true;
        } else if (arg == QLatin1String("%i")) {
            if (!target.iconName.isEmpty()) { argv << QStringLiteral("--icon") << target.iconName; }
        } else if (arg == QLatin1String("%c")) {
            argv << target.applicationName;
        } else if (arg == QLatin1String("%k")) {
            argv << target.desktopFilePath;
        } else {
            // The spec defines field codes as whole arguments, but entries in
            // the wild embed them (--app=%u). Substitute those and refuse the
            // rest: an unknown code would change what the command means, and
            // field codes are not shell substitutions to be passed through.
            QString expanded;
            for (int n = 0; n < arg.size(); ++n) {
                if (arg.at(n) != QLatin1Char('%')) {
                    expanded += arg.at(n);
                    continue;
                }
                if (++n >= arg.size()) { return {}; }
                const QChar code = arg.at(n);
                if (code == QLatin1Char('%')) {
                    expanded += code;
                    continue;
                }
                if (code != QLatin1Char('u') && code != QLatin1Char('U')
                    && code != QLatin1Char('f') && code != QLatin1Char('F')) { return {}; }
                if (!inserted) { argv << flags; inserted = true; }
                expanded += url.toString(QUrl::FullyEncoded);
                hasUrl = true;
            }
            argv << expanded;
        }
    }
    if (!inserted) { argv << flags; }
    if (!hasUrl) { argv << url.toString(QUrl::FullyEncoded); }
    return argv;
}

bool Launcher::commandIsSafe(const QStringList &command)
{
    const int program = programIndex(command);
    if (program < 0) { return false; }

    const QString resolved = QStandardPaths::findExecutable(command.at(program));
    if (!execIsSafe(resolved.isEmpty() ? command.at(program) : resolved)) { return false; }

    // Flatpak is a launcher of its own: what it will run has to be checked too.
    if (QFileInfo(command.at(program)).fileName() == QLatin1String("flatpak")) {
        if (command.value(program + 1) != QLatin1String("run")) { return false; }
        for (int n = program + 2; n < command.size(); ++n) {
            const auto arg = command.at(n);
            if (arg.startsWith(kCommandOption) && !execIsSafe(arg.mid(kCommandOption.size()))) { return false; }
            if (arg == QLatin1String("--command") && !execIsSafe(command.value(n + 1))) { return false; }
        }
    }
    return true;
}

void Launcher::launch(const Target &target,
                      const QUrl &url,
                      bool privateWindow,
                      QWindow *window,
                      Completion completion,
                      const QString &activationToken,
                      QSharedPointer<LaunchOperation> operation)
{
    const auto done = [this, completion = std::move(completion)](LaunchResult result) {
        if (result.outcome == LaunchResult::Failed) {
            Q_EMIT launchFailed(result.error);
        }
        if (completion) {
            completion(result);
        }
    };
    if (!operation) {
        operation = QSharedPointer<LaunchOperation>::create();
    }
    QString reason;
    if (!UrlSanitizer::isRoutable(url, &reason)) {
        done({LaunchResult::Failed, reason});
        return;
    }

    const auto command = target.launchCommand(privateWindow);
    if (!commandIsSafe(command.isEmpty() ? QStringList{target.execPath} : command)) {
        done({LaunchResult::Failed, i18n("Refusing to launch %1: not a browser executable.", target.execPath)});
        return;
    }

    if (privateWindow && !target.supportsPrivate()) {
        done({LaunchResult::Failed, i18n("%1 does not support private browsing.", target.label)});
        return;
    }

    if (!window || !window->isVisible() || !KWindowSystem::isPlatformWayland()) {
        if (operation->cancelled) {
            done({LaunchResult::Cancelled, {}});
        } else {
            operation->dispatched = true;
            doLaunch(target, url, privateWindow, activationToken, done);
        }
        return;
    }

    // The inbound token that raised the picker is single-use and already spent,
    // so mint a fresh one for the browser. Pass a real app id -- an empty one
    // makes KWin more likely to reject the token.
    const QString appId = QFileInfo(target.storageId).completeBaseName();

    auto resolved = QSharedPointer<bool>::create(false);
    auto finish = [this, target, url, privateWindow, resolved, done, operation](const QString &token) {
        if (*resolved) {
            return;
        }
        *resolved = true;
        if (operation->cancelled) {
            done({LaunchResult::Cancelled, {}});
            return;
        }
        operation->dispatched = true;
        doLaunch(target, url, privateWindow, token, done);
    };

    // This overload picks up the last input serial itself. That matters for the
    // hold bar, which nothing was pressed in: passing a serial we looked up per
    // window would be zero there, and a request carrying a zero or stale serial
    // is refused, leaving the browser behind everything. Keep the surface mapped
    // for the roundtrip; the compositor decides whether to grant activation.
    KWaylandExtras::xdgActivationToken(window, appId).then(this, finish);

    QTimer::singleShot(kActivationTokenTimeoutMs, this, [finish] {
        finish(QString());
    });
}

void Launcher::doLaunch(const Target &target, const QUrl &url, bool privateWindow, const QString &activationToken, Completion completion)
{
    const bool needsArgv = !target.profileKey.isEmpty() || privateWindow;
    const auto startJob = [this, target, activationToken, completion](const KService::Ptr &service, const QList<QUrl> &urls) {
        auto *job = new KIO::ApplicationLauncherJob(service, this);
        job->setUrls(urls);
        job->setStartupId(activationToken.toUtf8());
        connect(job, &KJob::result, this, [completion, target](KJob *finished) {
            // KIO error text can contain the URL. Keep diagnostics free of its credentials/query.
            completion(finished->error() ? LaunchResult{LaunchResult::Failed,
                i18n("Could not launch %1 (error %2).", target.label, static_cast<int>(finished->error()))} : LaunchResult{});
        });
        job->start();
    };

    // Without a profile to select, KIO knows better than we do: it expands
    // field codes, honours Terminal=, and -- crucially -- reaches
    // DBusActivatable browsers, where the environment variable below would
    // never arrive because dbus-daemon, not us, spawns the process.
    if (!needsArgv) {
        const KService::Ptr service = KService::serviceByStorageId(target.storageId);
        if (service) {
            startJob(service, {url});
            return;
        }
    }

    const QStringList argv = buildArgv(target, url, privateWindow);
    if (argv.isEmpty()) {
        completion({LaunchResult::Failed, i18n("Unsupported desktop launch command for %1.", target.label)});
        return;
    }
    const QString program = argv.constFirst();
    const QStringList arguments = argv.mid(1);

    if (target.terminal) {
        // The command is already expanded; escape literal percent signs before
        // handing it to KIO's desktop-entry parser a second time.
        auto escaped = argv;
        for (auto &arg : escaped) { arg.replace(QLatin1Char('%'), QStringLiteral("%%")); }
        KService::Ptr service(new KService(target.applicationName, KShell::joinArgs(escaped), target.iconName));
        service->setTerminal(true);
        service->setTerminalOptions(target.terminalOptions);
        service->setWorkingDirectory(target.workingDirectory);
        startJob(service, {});
        return;
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setWorkingDirectory(target.workingDirectory);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    if (!activationToken.isEmpty()) {
        environment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), activationToken);
    }
    process.setProcessEnvironment(environment);
    const bool started = process.startDetached();
    completion(started ? LaunchResult{} : LaunchResult{LaunchResult::Failed, i18n("Could not start %1.", program)});
}

} // namespace Lob
