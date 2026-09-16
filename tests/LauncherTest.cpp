#include "core/Launcher.h"
#include "core/UrlSanitizer.h"

#include <QTest>
#include <QUrlQuery>

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

    void targetWithoutPrivateSupportRejectsTheRequest()
    {
        Target plain = chromiumTarget();
        plain.privateFlag.clear();
        const QStringList argv = Launcher::buildArgv(plain, kUrl, true);
        QVERIFY(argv.isEmpty());
    }

    void shellsAndInterpretersAreRefusedAsExecutables()
    {
        QVERIFY(!Launcher::execIsSafe(QStringLiteral("/bin/sh")));
        QVERIFY(!Launcher::execIsSafe(QStringLiteral("/usr/bin/python3")));
        QVERIFY(!Launcher::execIsSafe(QStringLiteral("/usr/bin/env")));
        QVERIFY(!Launcher::execIsSafe(QString()));
        QVERIFY(Launcher::execIsSafe(QStringLiteral("/usr/bin/microsoft-edge-stable")));
    }

    void launchReportsFailureAndCancellation()
    {
        Launcher launcher;
        Target target = chromiumTarget(QStringLiteral("Default"));
        target.execPath = QStringLiteral("/nonexistent/lob-test-browser");
        int completed = 0;
        launcher.launch(target, kUrl, false, nullptr, [&completed](LaunchResult result) {
            QCOMPARE(result.outcome, LaunchResult::Failed);
            QVERIFY(!result.error.isEmpty());
            ++completed;
        });
        QCOMPARE(completed, 1);
        auto operation = QSharedPointer<LaunchOperation>::create();
        operation->cancel();
        launcher.launch(target, kUrl, false, nullptr, [&completed](LaunchResult result) {
            QCOMPARE(result.outcome, LaunchResult::Cancelled);
            ++completed;
        }, {}, operation);
        QCOMPARE(completed, 2);
    }

    void wrappedProfileLaunchPreservesArgumentsAndEncodesUrl()
    {
        auto target = chromiumTarget(QStringLiteral("Profile 1"));
        target.command = {QStringLiteral("/usr/bin/env"), QStringLiteral("XDG_CONFIG_HOME=/custom config"),
            QStringLiteral("--"), QStringLiteral("/usr/bin/chromium"), QStringLiteral("--user-data-dir=/browser data"),
            QStringLiteral("--profile-directory=old"), QStringLiteral("--"), QStringLiteral("%U")};
        target.privateCommand = {QStringLiteral("/usr/bin/chromium"), QStringLiteral("--incognito"), QStringLiteral("%U")};
        const QUrl url(QStringLiteral("https://example.com/a b?q=%26"));
        auto argv = Launcher::buildArgv(target, url, true);
        QCOMPARE(argv.at(1), QStringLiteral("XDG_CONFIG_HOME=/custom config"));
        QCOMPARE(argv.at(2), QStringLiteral("--"));
        QCOMPARE(argv.at(3), QStringLiteral("/usr/bin/chromium"));
        QVERIFY(argv.contains(QStringLiteral("--user-data-dir=/browser data")));
        QVERIFY(!argv.contains(QStringLiteral("--profile-directory=old")));
        QVERIFY(argv.indexOf(QStringLiteral("--profile-directory=Profile 1")) < argv.lastIndexOf(QStringLiteral("--")));
        QCOMPARE(argv.last(), url.toString(QUrl::FullyEncoded));
    }

    void flatpakOptionsStayOutsideFileForwarding()
    {
        auto target = geckoTarget(QStringLiteral("/profiles/work"));
        target.flatpakId = QStringLiteral("org.mozilla.firefox");
        target.command = {QStringLiteral("/usr/bin/flatpak"), QStringLiteral("run"), QStringLiteral("--command=firefox"),
                          QStringLiteral("--file-forwarding"), target.flatpakId, QStringLiteral("@@u"),
                          QStringLiteral("%u"), QStringLiteral("@@")};
        const auto argv = Launcher::buildArgv(target, kUrl, true);
        QVERIFY(argv.indexOf(QStringLiteral("--profile")) > argv.indexOf(target.flatpakId));
        QVERIFY(argv.indexOf(QStringLiteral("--private-window")) < argv.indexOf(QStringLiteral("@@u")));
        QCOMPARE(argv.at(argv.indexOf(QStringLiteral("@@u")) + 1), kUrl.toString());
    }

    void privateActionDoesNotDuplicateUrlConsumingFlag()
    {
        auto target = geckoTarget();
        target.privateCommand = {target.execPath, target.privateFlag, QStringLiteral("%u")};
        const auto argv = Launcher::buildArgv(target, kUrl, true);
        QCOMPARE(argv.count(target.privateFlag), 1);
        QCOMPARE(argv.last(), kUrl.toString());
    }

    void environmentWrapperCannotHideInterpreter()
    {
        QVERIFY(Launcher::commandIsSafe({QStringLiteral("env"), QStringLiteral("A=B"), QStringLiteral("firefox")}));
        QVERIFY(!Launcher::commandIsSafe({QStringLiteral("env"), QStringLiteral("A=B"), QStringLiteral("sh"), QStringLiteral("-c")}));
        QVERIFY(!Launcher::commandIsSafe({QStringLiteral("flatpak"), QStringLiteral("run"), QStringLiteral("--command=sh")}));

        // env's own options can replace the environment or re-split the command
        // line, which would move the program somewhere the check never looked.
        QVERIFY(!Launcher::commandIsSafe({QStringLiteral("env"), QStringLiteral("-u"), QStringLiteral("PATH"),
                                          QStringLiteral("sh")}));
        QVERIFY(!Launcher::commandIsSafe({QStringLiteral("env"), QStringLiteral("-S"), QStringLiteral("sh -c id")}));

        auto target = geckoTarget();
        target.command = {QStringLiteral("/usr/bin/env"), QStringLiteral("-i"), target.execPath, QStringLiteral("%u")};
        QVERIFY(Launcher::buildArgv(target, kUrl, false).isEmpty());
    }

    void fieldCodesInsideAnArgumentAreSubstituted()
    {
        // Not what the spec says, but --app=%u exists in real desktop files,
        // and refusing to launch is a worse answer than reading it.
        auto target = chromiumTarget();
        target.command = {target.execPath, QStringLiteral("--app=%u")};
        const auto argv = Launcher::buildArgv(target, kUrl, false);
        QCOMPARE(argv.size(), 2);
        QCOMPARE(argv.last(), QStringLiteral("--app=") + kUrl.toString(QUrl::FullyEncoded));

        // An unknown code still means the entry cannot be run directly: it
        // would otherwise reach the browser as a literal argument.
        target.command = {target.execPath, QStringLiteral("--tag=%z"), QStringLiteral("%u")};
        QVERIFY(Launcher::buildArgv(target, kUrl, false).isEmpty());

        // An escaped percent sign is just a percent sign.
        target.command = {target.execPath, QStringLiteral("--label=100%%"), QStringLiteral("%u")};
        QCOMPARE(Launcher::buildArgv(target, kUrl, false).at(1), QStringLiteral("--label=100%"));
    }

    void trackingParametersAreStrippedIncludingPrefixPatterns()
    {
        const QStringList patterns = UrlSanitizer::defaultTrackingParameters();
        const QUrl cleaned = UrlSanitizer::strip(
            QUrl(QStringLiteral("https://shop.example/item?id=42&utm_source=news&utm_campaign=x&fbclid=abc&ref=friend")),
            patterns);

        QUrlQuery query(cleaned);
        QVERIFY(!query.hasQueryItem(QStringLiteral("utm_source")));
        QVERIFY(!query.hasQueryItem(QStringLiteral("utm_campaign")));
        QVERIFY(!query.hasQueryItem(QStringLiteral("fbclid")));
        QCOMPARE(query.queryItemValue(QStringLiteral("id")), QStringLiteral("42"));
        QCOMPARE(query.queryItemValue(QStringLiteral("ref")), QStringLiteral("friend"));
    }

    void aQueryOfNothingButTrackingIsEmptiedEntirely()
    {
        // The common case for this feature: a newsletter link that is just the
        // page plus its campaign tags. Leaving it alone would exempt precisely
        // the URLs most worth cleaning.
        const QUrl cleaned = UrlSanitizer::strip(
            QUrl(QStringLiteral("https://example.com/watch?utm_source=news&fbclid=x")),
            UrlSanitizer::defaultTrackingParameters());
        QCOMPARE(cleaned.toString(), QStringLiteral("https://example.com/watch"));
        QVERIFY(!cleaned.hasQuery());
    }

    void urlsWithoutTrackingAreReturnedUnchanged()
    {
        const QUrl url(QStringLiteral("https://example.com/a?b=c"));
        QCOMPARE(UrlSanitizer::strip(url, UrlSanitizer::defaultTrackingParameters()), url);
        const QUrl noQuery(QStringLiteral("https://example.com/a"));
        QCOMPARE(UrlSanitizer::strip(noQuery, UrlSanitizer::defaultTrackingParameters()), noQuery);
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
