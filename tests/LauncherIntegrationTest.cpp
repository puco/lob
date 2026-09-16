#include "core/Launcher.h"
#include <KService>
#include <KShell>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Lob;

class LauncherIntegrationTest : public QObject
{
    Q_OBJECT
    QTemporaryDir files;
    const QUrl url{QStringLiteral("https://example.com/a b?q=%26")};
    QString helper() const { return QCoreApplication::applicationDirPath() + QStringLiteral("/LaunchRecorder"); }
    QJsonObject record()
    {
        QFile file(files.filePath(QStringLiteral("record.json")));
        if (!file.open(QIODevice::ReadOnly)) { return {}; }
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    Target desktop(const QString &program)
    {
        Target target;
        target.execPath = program;
        target.label = QStringLiteral("Test browser");
        target.storageId = files.filePath(QStringLiteral("browser.desktop"));
        QFile file(target.storageId);
        if (!file.open(QIODevice::WriteOnly)) { qFatal("Cannot create test desktop file"); }
        file.write("[Desktop Entry]\nType=Application\nName=Test browser\nExec="
            + KShell::quoteArg(program).toUtf8() + " %U\nMimeType=x-scheme-handler/https;\n");
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return target;
    }
private Q_SLOTS:
    void init()
    {
        QVERIFY(files.isValid());
        QFile::remove(files.filePath(QStringLiteral("record.json")));
        qputenv("LOB_TEST_OUTPUT", files.filePath(QStringLiteral("record.json")).toUtf8());
    }
    void cleanup() { qunsetenv("XDG_ACTIVATION_TOKEN"); }

    void kioCompletionMeansTheProcessWasStarted()
    {
        Launcher launcher;
        const auto target = desktop(helper());
        QVERIFY(KService::serviceByStorageId(target.storageId));
        int completed = 0;
        LaunchResult result;
        launcher.launch(target, url, false, nullptr, [&](LaunchResult value) { result = value; ++completed; });
        QTRY_COMPARE(completed, 1);
        QCOMPARE(result.outcome, LaunchResult::Started);
        QTRY_VERIFY(!record().isEmpty());
        // KIO supplies a single pretty-decoded argv element; URL semantics must survive.
        QCOMPARE(QUrl(record().value(QStringLiteral("arguments")).toArray().last().toString()), url);
        QTest::qWait(30);
        QCOMPARE(completed, 1);
    }

    void kioFailureIsReported()
    {
        Launcher launcher;
        const auto target = desktop(QStringLiteral("/nonexistent/lob-browser"));
        int completed = 0;
        LaunchResult result;
        launcher.launch(target, url, false, nullptr, [&](LaunchResult value) { result = value; ++completed; });
        QTRY_COMPARE(completed, 1);
        QCOMPARE(result.outcome, LaunchResult::Failed);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(record().isEmpty());
    }

    void directPrivateLaunchPreservesArgumentsAndEnvironment()
    {
        Launcher launcher;
        Target target;
        target.execPath = helper();
        target.command = {helper(), QStringLiteral("--user-data-dir=/profile root"), QStringLiteral("%U")};
        target.family = EngineFamily::Chromium;
        target.profileKey = QStringLiteral("Profile 1");
        target.privateFlag = QStringLiteral("--incognito");
        qputenv("XDG_ACTIVATION_TOKEN", "original");
        int completed = 0;
        launcher.launch(target, url, true, nullptr, [&](LaunchResult value) {
            QCOMPARE(value.outcome, LaunchResult::Started);
            ++completed;
        }, QStringLiteral("outbound"));
        QCOMPARE(completed, 1);
        QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray("original"));
        QTRY_VERIFY(!record().isEmpty());
        const auto args = record().value(QStringLiteral("arguments")).toArray();
        QVERIFY(args.contains(QStringLiteral("--incognito")));
        QVERIFY(args.contains(QStringLiteral("--profile-directory=Profile 1")));
        QVERIFY(args.contains(QStringLiteral("--user-data-dir=/profile root")));
        QCOMPARE(record().value(QStringLiteral("token")).toString(), QStringLiteral("outbound"));
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    if (!home.isValid()) { return 1; }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", home.filePath(QStringLiteral("cache")).toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    LauncherIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "LauncherIntegrationTest.moc"
