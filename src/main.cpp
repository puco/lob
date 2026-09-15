#include "core/Target.h"
#include "core/Launcher.h"
#include "core/TargetRegistry.h"
#include "core/UrlSanitizer.h"
#include "ui/PickerController.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include <KAboutData>
#include <KLocalizedQmlContext>
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

QUrl urlFromArgs(const QStringList &args)
{
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith(QLatin1String("--"))) {
            continue;
        }
        return QUrl::fromUserInput(arg);
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
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

    // Snapshot the inbound activation token before anything else can consume
    // or clobber it; it is single-use and belongs to this URL.
    const QString inboundToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    qunsetenv("XDG_ACTIVATION_TOKEN");

    QApplication app(argc, argv);
    setupIdentity();
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    KAboutData about(QStringLiteral("lob"),
                     i18n("Lob"),
                     QStringLiteral(LOB_VERSION),
                     i18n("Routes links to the right browser"),
                     KAboutLicense::GPL_V3);
    // Must be set on the KAboutData itself: setApplicationData overwrites
    // QGuiApplication's desktopFileName with "org.kde.<component>" otherwise,
    // which breaks the portal registration and the derived D-Bus name.
    about.setDesktopFileName(QStringLiteral(LOB_APP_ID));
    KAboutData::setApplicationData(about);

    QTextStream err(stderr);

    const QUrl url = urlFromArgs(rawArgs);
    if (url.isEmpty()) {
        err << "usage: lob [--list] <url>\n";
        return 2;
    }

    QString reason;
    if (!Lob::UrlSanitizer::isRoutable(url, &reason)) {
        err << reason << '\n';
        return 2;
    }

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);

    Lob::PickerController controller;
    controller.refreshTargets();

    if (!controller.ensureWindow(&engine)) {
        err << "Failed to build the picker window.\n";
        return 1;
    }

    QObject::connect(&controller, &Lob::PickerController::errorOccurred, &app, [&err](const QString &message) {
        err << message << '\n';
    });
    QObject::connect(&controller, &Lob::PickerController::finished, &app, &QCoreApplication::quit);

    controller.showFor(url, inboundToken);

    return app.exec();
}
