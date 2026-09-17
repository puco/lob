#include "core/TargetRegistry.h"
#include "core/Launcher.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <KShell>

using namespace Lob;

class TargetRegistryTest : public QObject
{
    Q_OBJECT
    QTemporaryDir home;
    static void write(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    KService::Ptr desktop(const QString &id, const QString &exec, const QByteArray &extra = {})
    {
        const auto path = home.filePath(id + QStringLiteral(".desktop"));
        write(path, "[Desktop Entry]\nType=Application\nName=" + id.toUtf8()
                    + "\nCategories=WebBrowser;\nExec=" + exec.toUtf8() + "\n" + extra);
        return KService::Ptr(new KService(path));
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    }

    void profileIdsDoNotDependOnProfileCount()
    {
        const auto dataDir = home.filePath(QStringLiteral("custom data"));
        const auto state = dataDir + QStringLiteral("/Local State");
        write(state, "{\"profile\":{\"info_cache\":{\"Default\":{\"name\":\"Personal\"}}}}");
        auto service = desktop(QStringLiteral("chromium"),
            KShell::joinArgs({QStringLiteral("/usr/bin/chromium"), QStringLiteral("--user-data-dir=") + dataDir}) + QStringLiteral(" %U"));
        auto targets = TargetRegistry::fromServices({service}, {});
        QCOMPARE(targets.size(), 2);
        const auto profileId = service->storageId() + QStringLiteral("#Default");
        QVERIFY(std::any_of(targets.cbegin(), targets.cend(), [&](const Target &t) { return t.id == profileId; }));
        write(state, "{\"profile\":{\"info_cache\":{\"Default\":{},\"Profile 1\":{}}}}");
        targets = TargetRegistry::fromServices({service}, {});
        QCOMPARE(targets.size(), 3);
        QVERIFY(std::any_of(targets.cbegin(), targets.cend(), [&](const Target &t) { return t.id == profileId; }));
        write(state, "{\"profile\":{\"info_cache\":{\"Default\":{}}}}");
        targets = TargetRegistry::fromServices({service}, {});
        QCOMPARE(targets.size(), 2);
        QVERIFY(std::any_of(targets.cbegin(), targets.cend(), [&](const Target &t) { return t.id == profileId; }));
    }

    void flatpakBrowsersRemainDistinctAndUseSandboxProfiles()
    {
        write(home.filePath(QStringLiteral("config/mozilla/firefox/profiles.ini")),
              "[Profile0]\nName=Native\nPath=native\nIsRelative=1\n");
        write(home.filePath(QStringLiteral(".var/app/org.mozilla.firefox/.mozilla/firefox/profiles.ini")),
              "[Profile0]\nName=Sandbox\nPath=sandbox\nIsRelative=1\n");
        auto firefox = desktop(QStringLiteral("org.mozilla.firefox"),
            QStringLiteral("/usr/bin/flatpak run --command=firefox org.mozilla.firefox @@u %u @@"),
            "X-Flatpak=org.mozilla.firefox\n");
        auto chrome = desktop(QStringLiteral("com.google.Chrome"),
            QStringLiteral("/usr/bin/flatpak run --command=google-chrome com.google.Chrome %U"),
            "X-Flatpak=com.google.Chrome\n");
        auto targets = TargetRegistry::fromServices({firefox, chrome}, {});
        QCOMPARE(targets.size(), 3);
        QVERIFY(std::any_of(targets.cbegin(), targets.cend(), [&](const Target &t) { return t.id == chrome->storageId(); }));
        for (const auto &target : targets) {
            if (!target.profileKey.isEmpty()) {
                QCOMPARE(target.profileName, QStringLiteral("Sandbox"));
                QVERIFY(target.profileKey.contains(QLatin1String("/.var/app/")));
            }
        }
    }

    void environmentAndPrivateActionAreDiscovered()
    {
        const auto config = home.filePath(QStringLiteral("relocated"));
        write(config + QStringLiteral("/microsoft-edge/Local State"), "{\"profile\":{\"info_cache\":{\"Default\":{}}}}");
        auto service = desktop(QStringLiteral("edge"),
            QStringLiteral("/usr/bin/env XDG_CONFIG_HOME=") + config + QStringLiteral(" /usr/bin/microsoft-edge %U"),
            "Actions=InPrivate;\n[Desktop Action InPrivate]\nName=Private\nExec=/usr/bin/microsoft-edge --ozone-platform=wayland --inprivate %U\n");
        const auto targets = TargetRegistry::fromServices({service}, {});
        QCOMPARE(targets.size(), 2);
        for (const auto &target : targets) {
            QCOMPARE(target.privateFlag, QStringLiteral("--inprivate"));
            QVERIFY(target.dataDir.startsWith(config));
        }
    }

    void anUnknownPrivateFlagIsReadFromTheActionThatDeclaresIt()
    {
        // Browsers outside the two big families spell this however they like.
        // The action id is what identifies it; the flag itself is whatever the
        // entry passes, so a fork nobody has heard of still gets private mode.
        auto service = desktop(QStringLiteral("obscurebrowser"),
            QStringLiteral("/usr/bin/obscurebrowser %u"),
            "Actions=new-private-window;\n[Desktop Action new-private-window]\n"
            "Name=Secret\nExec=/usr/bin/obscurebrowser --new-window --secret-window %u\n");
        const auto targets = TargetRegistry::fromServices({service}, {});
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.first().family, EngineFamily::Unknown);
        QCOMPARE(targets.first().privateFlag, QStringLiteral("--secret-window"));
        QVERIFY(Launcher::buildArgv(targets.first(), QUrl(QStringLiteral("https://example.com/")), true)
                    .contains(QStringLiteral("--secret-window")));
    }

    void aBrowsersOnlyProfileIsNotListedBesideTheBrowser()
    {
        const auto dataDir = home.filePath(QStringLiteral("listed"));
        const auto state = dataDir + QStringLiteral("/Local State");
        write(state, "{\"profile\":{\"info_cache\":{\"Default\":{\"name\":\"Personal\"}}}}");
        auto service = desktop(QStringLiteral("chromium-listed"),
            KShell::joinArgs({QStringLiteral("/usr/bin/chromium"), QStringLiteral("--user-data-dir=") + dataDir}) + QStringLiteral(" %U"));

        auto targets = TargetRegistry::fromServices({service}, {});
        auto listed = TargetRegistry::withoutRedundantProfiles(targets);
        QCOMPARE(targets.size(), 2);
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().id, service->storageId());
        // Still resolvable, so a rule naming the profile keeps working.
        QVERIFY(!TargetRegistry::resolve(targets, service->storageId() + QStringLiteral("#Default")).id.isEmpty());

        // With more than one profile the browser's own row is the vague one:
        // it opens the default profile, which now has a row that says so.
        write(state, "{\"profile\":{\"info_cache\":{\"Default\":{},\"Profile 1\":{}}}}");
        targets = TargetRegistry::fromServices({service}, {});
        listed = TargetRegistry::withoutRedundantProfiles(targets);
        QCOMPARE(targets.size(), 3);
        QCOMPARE(listed.size(), 2);
        QVERIFY(std::none_of(listed.cbegin(), listed.cend(), [&](const Target &t) { return t.profileKey.isEmpty(); }));
        QVERIFY(!TargetRegistry::resolve(targets, service->storageId()).id.isEmpty());
    }

    void severalProfilesWithNoDefaultKeepTheBrowsersOwnEntry()
    {
        // Nothing here says which profile the bare entry would open, so leaving
        // it out would drop a destination rather than a duplicate name.
        write(home.filePath(QStringLiteral("config/mozilla/firefox/profiles.ini")),
              "[Profile0]\nName=one\nPath=one\nIsRelative=1\n"
              "[Profile1]\nName=two\nPath=two\nIsRelative=1\n");
        auto service = desktop(QStringLiteral("firefox"), QStringLiteral("/usr/bin/firefox %u"));
        const auto targets = TargetRegistry::fromServices({service}, {});
        const auto listed = TargetRegistry::withoutRedundantProfiles(targets);
        QCOMPARE(targets.size(), 3);
        QCOMPARE(listed.size(), 3);
    }

    void legacyMemoriesAreResolvedOnlyWhenUnambiguous()
    {
        Target base;
        base.id = base.storageId = QStringLiteral("firefox.desktop");
        Target first = base;
        first.profileKey = QStringLiteral("one");
        first.id += QStringLiteral("#one");
        Target second = base;
        second.profileKey = QStringLiteral("two");
        second.id += QStringLiteral("#two");
        QCOMPARE(TargetRegistry::resolve({base, first}, base.id, true).id, first.id);
        QVERIFY(TargetRegistry::resolve({base, first, second}, base.id, true).id.isEmpty());
        QCOMPARE(TargetRegistry::resolve({base, first, second}, base.id, false).id, base.id);
        QCOMPARE(TargetRegistry::resolve({base, first, second}, first.id, true).id, first.id);
    }
};
QTEST_GUILESS_MAIN(TargetRegistryTest)
#include "TargetRegistryTest.moc"
