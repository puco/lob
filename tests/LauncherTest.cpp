#include "core/Launcher.h"
#include "core/UrlSanitizer.h"

#include <QTest>

using namespace Lob;

namespace
{

Target chromiumTarget(const QString &profileKey = {})
{
    Target t;
    t.id = QStringLiteral("edge");
    t.storageId = QStringLiteral("microsoft-edge.desktop");
    t.execPath = QStringLiteral("/usr/bin/microsoft-edge-stable");
    t.family = EngineFamily::Chromium;
    t.privateFlag = QStringLiteral("--inprivate");
    t.profileKey = profileKey;
    return t;
}

Target geckoTarget(const QString &profileKey = {})
{
    Target t;
    t.id = QStringLiteral("firefox");
    t.storageId = QStringLiteral("firefox.desktop");
    t.execPath = QStringLiteral("/usr/lib/firefox/firefox");
    t.family = EngineFamily::Gecko;
    t.privateFlag = QStringLiteral("--private-window");
    t.profileKey = profileKey;
    return t;
}

const QUrl kUrl{QStringLiteral("https://example.com/p?a=1")};

} // namespace

class LauncherTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void chromiumProfileStaysOneArgumentEvenWithASpace()
    {
        // Chromium truncates --profile-directory at a space and parses the
        // remainder as a URL, so this must never be split or shell-quoted.
        const QStringList argv = Launcher::buildArgv(chromiumTarget(QStringLiteral("Profile 1")), kUrl, false);
        QVERIFY(argv.contains(QStringLiteral("--profile-directory=Profile 1")));
        QCOMPARE(argv.last(), kUrl.toString());
    }

    void chromiumProfileFlagPrecedesTheUrl()
    {
        const QStringList argv = Launcher::buildArgv(chromiumTarget(QStringLiteral("Default")), kUrl, false);
        QVERIFY(argv.indexOf(QStringLiteral("--profile-directory=Default")) < argv.indexOf(kUrl.toString()));
    }

    void privateFlagIsTheVendorsOwn()
    {
        QVERIFY(Launcher::buildArgv(chromiumTarget(), kUrl, true).contains(QStringLiteral("--inprivate")));
        QVERIFY(Launcher::buildArgv(geckoTarget(), kUrl, true).contains(QStringLiteral("--private-window")));
    }

    void geckoUsesProfilePathAndNeverNoRemote()
    {
        const QStringList argv = Launcher::buildArgv(geckoTarget(QStringLiteral("/home/u/.config/mozilla/firefox/x")), kUrl, false);
        QCOMPARE(argv.at(1), QStringLiteral("--profile"));
        QCOMPARE(argv.at(2), QStringLiteral("/home/u/.config/mozilla/firefox/x"));
        // --no-remote would fork a second instance and produce a profile lock
        // error instead of handing the URL to the running one.
        QVERIFY(!argv.contains(QStringLiteral("--no-remote")));
    }

    void targetWithoutPrivateSupportIgnoresTheRequest()
    {
        Target plain = chromiumTarget();
        plain.privateFlag.clear();
        const QStringList argv = Launcher::buildArgv(plain, kUrl, true);
        QCOMPARE(argv.size(), 2); // exec + url only
    }

    void shellsAndInterpretersAreRefusedAsExecutables()
    {
        QVERIFY(!Launcher::execIsSafe(QStringLiteral("/bin/sh")));
        QVERIFY(!Launcher::execIsSafe(QStringLiteral("/usr/bin/python3")));
        QVERIFY(!Launcher::execIsSafe(QStringLiteral("/usr/bin/env")));
        QVERIFY(!Launcher::execIsSafe(QString()));
        QVERIFY(Launcher::execIsSafe(QStringLiteral("/usr/bin/microsoft-edge-stable")));
    }

    void onlyHttpUrlsWithoutCredentialsAreRoutable()
    {
        QVERIFY(UrlSanitizer::isRoutable(QUrl(QStringLiteral("https://example.com/"))));
        QVERIFY(UrlSanitizer::isRoutable(QUrl(QStringLiteral("http://example.com/"))));
        QVERIFY(!UrlSanitizer::isRoutable(QUrl(QStringLiteral("file:///etc/passwd"))));
        QVERIFY(!UrlSanitizer::isRoutable(QUrl(QStringLiteral("javascript:alert(1)"))));
        QVERIFY(!UrlSanitizer::isRoutable(QUrl(QStringLiteral("https://user:pw@example.com/"))));
        QVERIFY(!UrlSanitizer::isRoutable(QUrl(QStringLiteral("https:///nohost"))));
    }
};

QTEST_GUILESS_MAIN(LauncherTest)
#include "LauncherTest.moc"
