#include "core/BrowserProfiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Lob;

namespace
{

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
{
    const QString path = dir.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        qFatal("Could not write browser profile fixture");
    }
    file.close();
    return path;
}

} // namespace

class BrowserProfilesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void geckoPrefersTheInstallSectionOverTheLegacyDefaultFlag()
    {
        // Taken from a real profiles.ini: Profile1 carries Default=1, which
        // only records "last selected", while the [Install...] section names
        // what modern Firefox actually launches.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("profiles.ini"), R"([General]
StartWithLastProfile=1
Version=2

[Profile0]
Name=default-release
IsRelative=1
Path=b2of27ls.default-release

[Install4F96D1932A9F858E]
Default=b2of27ls.default-release
Locked=1

[Profile1]
Name=default
IsRelative=1
Path=oka48whq.default
Default=1
)");

        const auto profiles = scanGeckoProfiles(dir.path());
        QCOMPARE(profiles.size(), 2);

        const auto isDefault = [&profiles](const QString &name) {
            for (const auto &p : profiles) {
                if (p.name == name) {
                    return p.isDefault;
                }
            }
            return false;
        };
        QVERIFY(isDefault(QStringLiteral("default-release")));
        QVERIFY(!isDefault(QStringLiteral("default")));
    }

    void geckoResolvesRelativeAndAbsolutePaths()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("profiles.ini"), R"([Profile0]
Name=relative
IsRelative=1
Path=abc.relative

[Profile1]
Name=absolute
IsRelative=0
Path=/data/ff/absolute
)");

        const auto profiles = scanGeckoProfiles(dir.path());
        QCOMPARE(profiles.size(), 2);
        QCOMPARE(profiles.at(0).key, QDir::cleanPath(dir.path() + QStringLiteral("/abc.relative")));
        QCOMPARE(profiles.at(1).key, QStringLiteral("/data/ff/absolute"));
    }

    void geckoIgnoresANonExistentDirectory()
    {
        QVERIFY(scanGeckoProfiles(QStringLiteral("/nonexistent/path")).isEmpty());
        QVERIFY(!isGeckoDataDir(QStringLiteral("/nonexistent/path")));
    }

    void chromiumReadsProfileNamesInPrecedenceOrder()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("Local State"), R"({
  "profile": {
    "last_used": "Profile 1",
    "info_cache": {
      "Default":   { "gaia_name": "Gaia Only", "user_name": "a@example.com" },
      "Profile 1": { "name": "Personal", "user_name": "b@example.com" },
      "Profile 2": { "user_name": "c@example.com" }
    }
  }
})");

        auto profiles = scanChromiumProfiles(dir.path());
        QCOMPARE(profiles.size(), 3);

        std::sort(profiles.begin(), profiles.end(), [](const auto &a, const auto &b) {
            return a.key < b.key;
        });
        QCOMPARE(profiles.at(0).name, QStringLiteral("Gaia Only"));   // name absent -> gaia_name
        QCOMPARE(profiles.at(1).name, QStringLiteral("Personal"));    // name wins
        QCOMPARE(profiles.at(2).name, QStringLiteral("c@example.com")); // falls through to user_name

        QVERIFY(profiles.at(1).isDefault); // last_used, not the "Default" key
    }

    void chromiumKeysWithSpacesSurviveIntact()
    {
        // "Profile 1" is the common case and a space here is what trips the
        // Chromium --profile-directory truncation bug.
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("Local State"),
                  R"({"profile":{"info_cache":{"Profile 1":{"name":"Work"}}}})");

        const auto profiles = scanChromiumProfiles(dir.path());
        QCOMPARE(profiles.size(), 1);
        QCOMPARE(profiles.first().key, QStringLiteral("Profile 1"));
    }

    void malformedLocalStateYieldsNothing()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("Local State"), "not json at all");
        QVERIFY(scanChromiumProfiles(dir.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(BrowserProfilesTest)
#include "BrowserProfilesTest.moc"
