#include "app/Controller.h"
#include "core/Launcher.h"
#include "core/Startup.h"
#include "core/Target.h"
#include "core/TargetRegistry.h"

#include <QApplication>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

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

    for (const Lob::Target &target : targets) {
        const bool browser = target.kind == Lob::TargetKind::Browser;
        out << (browser ? "[browser] " : "[other]   ") << target.label << '\n';
        out << "    id:      " << target.id << '\n';
        out << "    exec:    " << target.execPath << '\n';
        out << "    engine:  " << familyName(target.family) << '\n';
        if (!target.profileKey.isEmpty()) {
            out << "    profile: " << target.profileName << "  (" << target.profileKey << ")\n";
        }

        // The exact command line, so the launch path is inspectable without
        // actually opening a browser.
        const QUrl sample(QStringLiteral("https://example.com/path?a=1"));
        out << "    open:    " << Lob::Launcher::buildArgv(target, sample, false).join(QLatin1Char(' ')) << '\n';
        if (target.supportsPrivate()) {
            out << "    private: " << Lob::Launcher::buildArgv(target, sample, true).join(QLatin1Char(' ')) << '\n';
        }
        if (!Lob::Launcher::execIsSafe(target.execPath)) {
            out << "    BLOCKED: exec is a shell or interpreter\n";
        }
        out << '\n';
    }

    return 0;
}

/// KDBusService hands us the inbound activation token via the environment and
/// clears it once the signal returns, so it has to be taken synchronously --
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

    // --list is diagnostic and must work without a display, so it runs before
    // any QApplication exists.
    if (rawArgs.contains(QStringLiteral("--list"))) {
        QCoreApplication app(argc, argv);
        setupIdentity();
        return runList();
    }

    const QString inboundToken = takeActivationToken();

    QApplication app(argc, argv);
    setupIdentity();
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    KAboutData about(QStringLiteral("lob"),
                     i18n("Lob"),
                     QStringLiteral(LOB_VERSION),
                     i18n("Routes links to the right browser"),
                     KAboutLicense::GPL_V3);
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

    const bool daemonMode = rawArgs.contains(QStringLiteral("--daemon"));

    if (!daemonMode && rawArgs.size() < 2) {
        err << "usage: lob [--daemon] [--pick] [--list] <url>\n";
        return 2;
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

    controller.handleArgs(rawArgs, inboundToken);

    // A one-shot invocation with nothing routable left has no reason to linger.
    if (!daemonMode && !controller.isBusy()) {
        return 2;
    }

    return app.exec();
}
