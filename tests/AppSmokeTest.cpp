#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class AppSmokeTest : public QObject
{
    Q_OBJECT
    QTemporaryDir home;
    QProcessEnvironment environment() const
    {
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HOME"), home.path());
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), home.filePath(QStringLiteral("config")));
        env.insert(QStringLiteral("XDG_CACHE_HOME"), home.filePath(QStringLiteral("cache")));
        env.insert(QStringLiteral("XDG_DATA_HOME"), home.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("QT_QPA_PLATFORMTHEME"), QStringLiteral("generic"));
        env.insert(QStringLiteral("QT_NO_ACCESSIBILITY"), QStringLiteral("1"));
        env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
        env.insert(QStringLiteral("LOB_NO_LAYERSHELL"), QStringLiteral("1"));
        env.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));
        env.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
        env.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("lob.*=true"));
        return env;
    }
private Q_SLOTS:
    void pickerLoadsAndPaints()
    {
        QVERIFY(home.isValid());
        QProcess process;
        process.setProcessEnvironment(environment());
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(LOB_TEST_BINARY), {QStringLiteral("--pick"), QStringLiteral("https://example.com/")});
        QVERIFY(process.waitForStarted());
        QByteArray output;
        QTRY_VERIFY2_WITH_TIMEOUT((output += process.readAll()).contains("painted in"), output.constData(), 10000);
        QVERIFY2(!output.contains("ReferenceError") && !output.contains("TypeError")
                 && !output.contains("failed to load component"), output.constData());
        process.terminate();
        if (!process.waitForFinished(2000)) { process.kill(); process.waitForFinished(); }
    }

    void rejectedCredentialsAreNotLogged()
    {
        QProcess process;
        process.setProcessEnvironment(environment());
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(LOB_TEST_BINARY), {QStringLiteral("https://user:secret-password@example.com/?token=secret-query")});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 2);
        const auto output = process.readAll();
        QVERIFY2(!output.contains("secret-password") && !output.contains("secret-query"), output.constData());
    }

    void zeroHoldOneShotWaitsForBothQueuedLaunches()
    {
        const QString apps = home.filePath(QStringLiteral("data/applications"));
        const QString config = home.filePath(QStringLiteral("config/lob"));
        QVERIFY(QDir().mkpath(apps));
        QVERIFY(QDir().mkpath(config));
        QFile desktop(apps + QStringLiteral("/lob-test-browser.desktop"));
        QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write("[Desktop Entry]\nType=Application\nName=Lob test browser\nCategories=WebBrowser;\nExec=\"");
        desktop.write((QCoreApplication::applicationDirPath() + QStringLiteral("/LaunchRecorder")).toUtf8());
        desktop.write("\" %U\nMimeType=x-scheme-handler/http;x-scheme-handler/https;\n");
        desktop.close();
        QFile rules(config + QStringLiteral("/rules.json"));
        QVERIFY(rules.open(QIODevice::WriteOnly));
        rules.write("{\"version\":1,\"holdMs\":0,\"fallbackTarget\":\"lob-test-browser.desktop\"}");
        rules.close();
        auto env = environment();
        const QString outputPath = home.filePath(QStringLiteral("launches.jsonl"));
        env.insert(QStringLiteral("LOB_TEST_OUTPUT"), outputPath);
        QProcess process;
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(LOB_TEST_BINARY), {QStringLiteral("https://first.example/"), QStringLiteral("https://second.example/")});
        const bool ended = process.waitForFinished(10000);
        const auto logs = process.readAll();
        QVERIFY2(ended, logs.constData());
        QCOMPARE(process.exitCode(), 0);
        QStringList urls;
        const auto collect = [&] {
            QFile records(outputPath);
            if (!records.open(QIODevice::ReadOnly)) { return false; }
            urls.clear();
            while (!records.atEnd()) {
                const auto record = QJsonDocument::fromJson(records.readLine()).object();
                urls << record.value(QStringLiteral("arguments")).toArray().last().toString();
            }
            return urls.size() >= 2;
        };
        QTRY_VERIFY(collect());
        QCOMPARE(urls.size(), 2);
        QVERIFY(urls.contains(QStringLiteral("https://first.example/")));
        QVERIFY(urls.contains(QStringLiteral("https://second.example/")));
    }
};
QTEST_GUILESS_MAIN(AppSmokeTest)
#include "AppSmokeTest.moc"
