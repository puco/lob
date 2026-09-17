#include "app/Controller.h"
#include "core/DefaultBrowserManager.h"
#include "core/Launcher.h"
#include "core/RedirectUnwrapper.h"
#include "core/RuleEngine.h"
#include "core/RuleStore.h"
#include "core/Startup.h"
#include "core/Target.h"
#include "core/TargetRegistry.h"
#include "core/UrlSanitizer.h"

#include <QApplication>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>
#include <QTimer>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

namespace
{

void setupIdentity()
{
    // These must stay in sync: KDBusService derives its bus name from the
    // reversed organizationDomain plus applicationName, and that name has to
    // equal the .desktop basename or D-Bus activation silently falls back to
    // the Exec= line.
    QCoreApplication::setOrganizationDomain(QStringLiteral("puco.github.io"));
    QCoreApplication::setApplicationName(QStringLiteral("lob"));
    QGuiApplication::setDesktopFileName(QStringLiteral(LOB_APP_ID));
    QCoreApplication::setApplicationVersion(QStringLiteral(LOB_VERSION));
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("lob"));
}

QString familyName(Lob::EngineFamily family)
{
    switch (family) {
    case Lob::EngineFamily::Gecko:
        return QStringLiteral("gecko");
    case Lob::EngineFamily::Chromium:
        return QStringLiteral("chromium");
    case Lob::EngineFamily::Unknown:
        return QStringLiteral("-");
    }
    return QStringLiteral("-");
}

int runList()
{
    QTextStream out(stdout);

    const auto targets = Lob::TargetRegistry::discover(QStringLiteral(LOB_APP_ID ".desktop"));
    if (targets.isEmpty()) {
        out << "No https handlers found.\n";
        return 0;
    }

    // Everything discovered is routable by id; the picker leaves out rows that
    // would say the same thing twice, and saying so here keeps the two views
    // from looking like a disagreement.
    const auto listed = Lob::TargetRegistry::withoutRedundantProfiles(targets);
    const auto standsInFor = [&listed](const Lob::Target &hidden) {
        for (const Lob::Target &target : listed) {
            if (target.storageId != hidden.storageId) {
                continue;
            }
            // A hidden browser entry is covered by its default profile; a
            // hidden profile by the browser entry that opens it.
            if (hidden.profileKey.isEmpty() ? target.isDefaultProfile : target.profileKey.isEmpty()) {
                return target.id;
            }
        }
        return QString();
    };

    for (const Lob::Target &target : targets) {
        const bool browser = target.kind == Lob::TargetKind::Browser;
        out << (browser ? "[browser] " : "[other]   ") << target.label << '\n';
        out << "    id:      " << target.id << '\n';
        if (!listed.contains(target)) {
            out << "    listed:  no -- same as " << standsInFor(target) << '\n';
        }
        out << "    exec:    " << target.execPath << '\n';
        out << "    engine:  " << familyName(target.family) << '\n';
        if (!target.profileKey.isEmpty()) {
            out << "    profile: " << target.profileName << (target.isDefaultProfile ? " [default]" : "")
                << "  (" << target.profileKey << ")\n";
        }

        // KIO owns unmodified desktop launches; modified launches have argv.
        const QUrl sample(QStringLiteral("https://example.com/path?a=1"));
        out << "    open:    " << (target.profileKey.isEmpty()
            ? QStringLiteral("KIO desktop launch: ") + target.storageId
            : Lob::Launcher::buildArgv(target, sample, false).join(QLatin1Char(' '))) << '\n';
        if (target.supportsPrivate()) {
            out << "    private: " << Lob::Launcher::buildArgv(target, sample, true).join(QLatin1Char(' ')) << '\n';
        }
        if (!Lob::Launcher::commandIsSafe(target.command)) {
            out << "    BLOCKED: exec is a shell or interpreter\n";
        }
        out << '\n';
    }

    return 0;
}

int runExplain(const QString &rawUrl)
{
    QTextStream out(stdout);

    const QUrl url = QUrl::fromUserInput(rawUrl);
    QString reason;
    if (!Lob::UrlSanitizer::isRoutable(url, &reason)) {
        out << "refused: " << reason << '\n';
        return 2;
    }

    Lob::RuleStore store;
    const auto rules = store.rules();

    // Routing reads redirectors and strips tracking parameters before the rules
    // ever see the URL, so explaining the raw one would answer a question
    // nobody asked -- and would differ from what actually happens.
    const Lob::Link link = store.unwrapRedirects()
        ? Lob::RedirectUnwrapper::unwrap(url, store.redirectWrappers())
        : Lob::Link::plain(url);
    const QUrl routed = store.stripTracking()
        ? Lob::UrlSanitizer::strip(link.destination, store.trackingParameters())
        : link.destination;

    const Lob::Decision decision = Lob::RuleEngine::decide(routed, rules, store.fallbackTargetId());

    out << "url:   " << url.toString() << '\n';
    if (link.wasWrapped()) {
        out << "       -> " << link.destination.toString() << " (unwrapped from " << link.wrapper << ")\n";
        if (link.toOpen != link.destination) {
            out << "       the browser still receives the " << link.toOpen.host() << " URL, which is there to be visited\n";
        }
    }
    if (routed != link.destination) {
        out << "       -> " << routed.toString() << " (tracking parameters stripped)\n";
    }
    out << "host:  " << routed.host() << '\n';
    out << "rules: " << rules.size() << " from " << Lob::RuleStore::filePath() << '\n';
    out << '\n';

    switch (decision.source) {
    case Lob::Decision::Source::Rule:
        out << "-> rule #" << decision.ruleIndex << ": " << Lob::RuleEngine::describe(rules.at(decision.ruleIndex))
            << '\n';
        break;
    case Lob::Decision::Source::Memory:
        out << "-> remembered #" << decision.ruleIndex << ": "
            << Lob::RuleEngine::describe(rules.at(decision.ruleIndex)) << '\n';
        // A memory is the one kind of rule nobody chose to write down, so it
        // is the one worth saying how to undo. The pattern itself is not
        // always a valid argument -- --forget takes a host or a URL, and a
        // path memory's pattern is neither -- so the hint names the URL that
        // reaches this memory, which is the thing that was asked about anyway.
        out << "   undo with: lob --forget "
            << (rules.at(decision.ruleIndex).matchKind == Lob::MatchKind::PathPrefix
                    ? routed.toString()
                    : rules.at(decision.ruleIndex).pattern)
            << '\n';
        break;
    case Lob::Decision::Source::Fallback:
        out << "-> no match; fallback target " << decision.targetId << '\n';
        break;
    case Lob::Decision::Source::Ask:
        out << "-> no match; show the picker\n";
        break;
    }

    if (decision.opensWithoutAsking()) {
        if (decision.action == Lob::RuleAction::Copy) {
            out << "   copies to the clipboard";
        } else {
            out << "   opens in " << decision.targetId << (decision.privateWindow ? " (private)" : "");
        }
        out << ", after a " << store.holdMs() << "ms hold bar\n";
    }

    // Rules that would have matched but were beaten: the usual reason a rule
    // "does not work" is that an earlier one already claimed the URL.
    bool shadowedHeader = false;
    for (int i = 0; i < rules.size(); ++i) {
        if (i == decision.ruleIndex || !Lob::RuleEngine::matches(rules.at(i), routed)) {
            continue;
        }
        if (!shadowedHeader) {
            out << "\nalso matched, but lost:\n";
            shadowedHeader = true;
        }
        out << "   #" << i << ": " << Lob::RuleEngine::describe(rules.at(i)) << '\n';
    }

    return 0;
}

/// Removes the "remember for this host" the picker wrote. This is the only
/// undo for that checkbox short of hand-editing rules.json, which is why it
/// exists: a choice made in passing should not be a one-way door.
int runForget(const QString &argument)
{
    QTextStream out(stdout);

    // People reach for this after clicking something, so the thing on the
    // clipboard is a URL far more often than a bare host. Both work: a bare
    // host becomes the URL it would have been, which is what the memories for
    // it were written against.
    QUrl url = QUrl::fromUserInput(argument);
    if (url.host().isEmpty()) {
        url = QUrl(QStringLiteral("https://") + argument.trimmed().toLower());
    }
    if (url.host().isEmpty()) {
        QTextStream(stderr) << "usage: lob --forget <host|url>\n";
        return 2;
    }

    Lob::RuleStore store;
    if (!store.lastError().isEmpty()) {
        // Saving is blocked while the file is unreadable, so say why rather
        // than reporting a forget that never reached the disk.
        out << store.lastError() << '\n';
        return 1;
    }

    // Whichever memory the engine would actually reach, not whichever one
    // happens to be named after the host: a memory covering a domain or a path
    // is the thing deciding these links, and is what has to go.
    const int index = store.memoryIndexFor(url);
    if (index < 0) {
        out << "nothing remembered for " << url.host() << '\n';
        return 0;
    }

    const QString pattern = store.rules().at(index).pattern;
    if (!store.forgetAt(index)) {
        out << (store.lastError().isEmpty() ? QStringLiteral("could not write the rules file") : store.lastError())
            << '\n';
        return 1;
    }

    out << "forgot " << pattern << "; it will ask again\n";

    // One command drops one memory, so a URL covered by several says what is
    // still there rather than leaving the next click unexplained.
    if (store.memoryIndexFor(url) >= 0) {
        out << "still remembered: " << store.rules().at(store.memoryIndexFor(url)).pattern
            << " (run --forget again to drop it too)\n";
    }
    return 0;
}

int runDefaultBrowser(const QString &mode)
{
    QTextStream out(stdout);

    if (mode == QLatin1String("--status")) {
        out << "default browser: " << (Lob::DefaultBrowserManager::isDefault() ? "lob" : "not lob") << '\n';
        const QStringList handlers = Lob::DefaultBrowserManager::currentHandlers();
        const QStringList types = Lob::DefaultBrowserManager::handledMimeTypes();
        for (int i = 0; i < types.size(); ++i) {
            out << "  " << types.at(i) << " -> " << (handlers.at(i).isEmpty() ? QStringLiteral("(unset)") : handlers.at(i))
                << '\n';
        }
        const QString shadow = Lob::DefaultBrowserManager::shadowingConfig();
        if (!shadow.isEmpty()) {
            out << "warning: " << shadow << " sets a default that takes precedence over ours\n";
        }
        return 0;
    }

    QString error;
    const bool claiming = mode == QLatin1String("--set-default");
    const bool ok = claiming ? Lob::DefaultBrowserManager::claim(&error) : Lob::DefaultBrowserManager::restore(&error);

    if (!error.isEmpty()) {
        out << error << '\n';
    }
    if (!ok) {
        return 1;
    }

    out << (claiming ? "lob now handles http and https\n" : "previous browser restored\n");
    return 0;
}

/// KDBusService hands us the inbound activation token via the environment and
/// clears it once the signal returns, so it has to be taken synchronously --
/// Written once and printed either to stdout on request or to stderr when the
/// command line made no sense, so the two can never describe different programs.
void printUsage(QTextStream &out)
{
    out << QStringLiteral("lob " LOB_VERSION " -- choose which browser opens each link\n"
                          "\n"
                          "usage: lob [--daemon] [--pick] <url>\n"
                          "       lob --list | --explain <url> | --forget <host> | --status\n"
                          "       lob --set-default | --restore-default\n"
                          "\n"
                          "  <url>               route one URL: a rule decides, or the picker asks\n"
                          "  --pick <url>        route one URL, ignoring the rules\n"
                          "  --daemon            stay resident, so the picker opens without a delay\n"
                          "  --list              discovered browsers, profiles and launch commands\n"
                          "  --explain <url>     which rule decides this URL, and which ones lose\n"
                          "  --forget <host>     drop the choice remembered for a host, so it asks again\n"
                          "  --status            who currently handles http and https\n"
                          "  --set-default       claim the handler, recording what was there first\n"
                          "  --restore-default   put the previous handler back\n"
                          "  --version           print the version\n"
                          "  --help              print this\n");
}

/// any queued call or nested event loop would lose it.
QString takeActivationToken()
{
    const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (!token.isEmpty()) {
        qunsetenv("XDG_ACTIVATION_TOKEN");
    }
    return token;
}

} // namespace

int main(int argc, char *argv[])
{
    Lob::startupTimer().start();

    const QStringList rawArgs = [argc, argv] {
        QStringList args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            args << QString::fromLocal8Bit(argv[i]);
        }
        return args;
    }();

    // Answering these needs no application object at all, let alone a display,
    // and answering them first means they work even where nothing else would.
    if (rawArgs.contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << QStringLiteral("lob " LOB_VERSION "\n");
        return 0;
    }

    if (rawArgs.contains(QStringLiteral("--help")) || rawArgs.contains(QStringLiteral("-h"))) {
        QTextStream out(stdout);
        printUsage(out);
        return 0;
    }

    const bool daemonMode = rawArgs.contains(QStringLiteral("--daemon"));

    // Nothing to route and nothing to stay running for. Decided here rather
    // than after registering on the bus, where a running daemon would take the
    // call and this would exit silently having said nothing.
    if (!daemonMode && rawArgs.size() < 2) {
        QTextStream out(stderr);
        printUsage(out);
        return 2;
    }

    // --list is diagnostic and must work without a display, so it runs before
    // any QApplication exists.
    if (rawArgs.contains(QStringLiteral("--list"))) {
        QCoreApplication app(argc, argv);
        setupIdentity();
        return runList();
    }

    for (const auto &mode : {"--set-default", "--restore-default", "--status"}) {
        if (rawArgs.contains(QLatin1String(mode))) {
            QCoreApplication app(argc, argv);
            setupIdentity();
            return runDefaultBrowser(QLatin1String(mode));
        }
    }

    for (const auto &mode : {"--explain", "--forget"}) {
        const QString flag = QLatin1String(mode);
        if (!rawArgs.contains(flag)) {
            continue;
        }
        QCoreApplication app(argc, argv);
        setupIdentity();
        const int index = rawArgs.indexOf(flag);
        if (index + 1 >= rawArgs.size()) {
            QTextStream(stderr) << "usage: lob " << flag << (flag == QLatin1String("--forget") ? " <host|url>\n" : " <url>\n");
            return 2;
        }
        return flag == QLatin1String("--forget") ? runForget(rawArgs.at(index + 1)) : runExplain(rawArgs.at(index + 1));
    }

    const QString inboundToken = takeActivationToken();

    QApplication app(argc, argv);
    setupIdentity();
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    KAboutData about(QStringLiteral("lob"),
                     i18n("Lob"),
                     QStringLiteral(LOB_VERSION),
                     i18n("Routes links to the right browser"),
                     KAboutLicense::MIT);
    // Both of these must be set on the KAboutData itself, because
    // setApplicationData overwrites what setupIdentity() put on
    // QCoreApplication. KAboutData defaults organizationDomain to "kde.org",
    // which silently yields the bus name org.kde.lob -- and a bus name that
    // does not match the .desktop basename means D-Bus activation never
    // reaches us and every link falls back to the Exec= line instead.
    about.setOrganizationDomain(QByteArrayLiteral("puco.github.io"));
    about.setDesktopFileName(QStringLiteral(LOB_APP_ID));
    KAboutData::setApplicationData(about);
    KCrash::initialize();

    QTextStream err(stderr);

    // Registering before doing any real work means a second `lob <url>` hands
    // its arguments to the running daemon and exits without ever building a
    // QML engine.
    KDBusService service(KDBusService::Unique);
    if (!service.isRegistered()) {
        return 0;
    }

    Lob::Controller controller;
    controller.setDaemonMode(daemonMode);

    if (!controller.initialize()) {
        err << "Failed to build the picker window.\n";
        return 1;
    }

    QObject::connect(&service, &KDBusService::activateRequested, &controller,
                     [&controller](const QStringList &args, const QString &) {
                         controller.handleArgs(args, takeActivationToken());
                     });
    QObject::connect(&service, &KDBusService::openRequested, &controller, [&controller](const QList<QUrl> &urls) {
        controller.handleUrls(urls, takeActivationToken());
    });

    QTimer::singleShot(0, &controller, [&controller, rawArgs, inboundToken, daemonMode] {
        controller.handleArgs(rawArgs, inboundToken);
        if (!daemonMode && !controller.isBusy() && !controller.servingClipboard()) {
            QCoreApplication::exit(controller.acceptedAnyUrl() ? 0 : 2);
        }
    });

    return app.exec();
}
