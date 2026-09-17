#include "core/RuleStore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSaveFile>
#include <QLockFile>

using namespace Lob;

class RuleStoreTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    static void writeConfig(const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(RuleStore::filePath()).absolutePath());
        QSaveFile file(RuleStore::filePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        QVERIFY(file.commit());
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
        QStandardPaths::setTestModeEnabled(false);
    }

    void init()
    {
        QFile::remove(RuleStore::filePath());
    }

    void missingFileIsNotAnError()
    {
        RuleStore store;
        QVERIFY(store.rules().isEmpty());
        QCOMPARE(store.holdMs(), 600);
    }

    void rulesSurviveARoundTrip()
    {
        Rule r;
        r.matchKind = MatchKind::PathPrefix;
        r.pattern = QStringLiteral("github.com/anthropics");
        r.action = RuleAction::Open;
        r.targetId = QStringLiteral("firefox.desktop");
        r.privateWindow = true;

        {
            RuleStore store;
            store.setRules({r});
            store.setHoldMs(1200);
        }

        RuleStore reloaded;
        QCOMPARE(reloaded.rules().size(), 1);
        QCOMPARE(reloaded.rules().first().matchKind, MatchKind::PathPrefix);
        QCOMPARE(reloaded.rules().first().pattern, r.pattern);
        QCOMPARE(reloaded.rules().first().targetId, r.targetId);
        QVERIFY(reloaded.rules().first().privateWindow);
        QCOMPARE(reloaded.holdMs(), 1200);
    }

    void rememberingTheSameHostTwiceReplacesRatherThanAccumulates()
    {
        RuleStore store;
        store.remember(QStringLiteral("example.com"), QStringLiteral("a.desktop"), false);
        store.remember(QStringLiteral("example.com"), QStringLiteral("b.desktop"), true);

        QCOMPARE(store.rules().size(), 1);
        QCOMPARE(store.rules().first().targetId, QStringLiteral("b.desktop"));
        QVERIFY(store.rules().first().privateWindow);
        QVERIFY(store.rules().first().remembered);
        QCOMPARE(store.rules().first().targetVersion, 2);
        QVERIFY(store.hasMemory(QStringLiteral("example.com")));
    }

    void rememberingDoesNotTouchExplicitRulesForTheSameHost()
    {
        Rule explicitRule;
        explicitRule.matchKind = MatchKind::Host;
        explicitRule.pattern = QStringLiteral("example.com");
        explicitRule.targetId = QStringLiteral("explicit.desktop");

        RuleStore store;
        store.setRules({explicitRule});
        store.remember(QStringLiteral("example.com"), QStringLiteral("remembered.desktop"), false);

        QCOMPARE(store.rules().size(), 2);
        QVERIFY(!store.rules().first().remembered);
        QCOMPARE(store.rules().first().targetId, QStringLiteral("explicit.desktop"));
    }

    void forgettingRemovesOnlyTheMemory()
    {
        RuleStore store;
        store.remember(QStringLiteral("example.com"), QStringLiteral("a.desktop"), false);
        store.remember(QStringLiteral("other.example"), QStringLiteral("b.desktop"), false);

        store.forget(QStringLiteral("example.com"));
        QCOMPARE(store.rules().size(), 1);
        QVERIFY(!store.hasMemory(QStringLiteral("example.com")));
        QVERIFY(store.hasMemory(QStringLiteral("other.example")));
    }

    void malformedJsonIsIgnoredRatherThanOverwritten()
    {
        // Losing a file someone was hand-editing because it is momentarily
        // invalid would be far worse than ignoring it for one run.
        const QString path = RuleStore::filePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ this is not json");
        file.close();

        RuleStore store;
        QVERIFY(store.rules().isEmpty());

        QFile check(path);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("{ this is not json"));
    }

    void invalidEditCannotLoseRulesOrBeOverwritten()
    {
        RuleStore store;
        QVERIFY(store.remember(QStringLiteral("example.com"), QStringLiteral("a.desktop"), false));
        writeConfig("{ invalid");
        QVERIFY(!store.load());
        QCOMPARE(store.rules().size(), 1);
        QVERIFY(!store.remember(QStringLiteral("other.com"), QStringLiteral("b.desktop"), false));
        QCOMPARE(store.rules().size(), 1);
        QFile file(RuleStore::filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{ invalid"));
    }

    void externalChangeBeforeWatcherCannotBeOverwritten()
    {
        RuleStore store;
        writeConfig("{\"version\":1,\"holdMs\":1234,\"rules\":[]}");
        QVERIFY(!store.setHoldMs(100));
        QCOMPARE(store.holdMs(), 1234);
        QVERIFY(store.setHoldMs(200));
        QCOMPARE(store.holdMs(), 200);
    }

    void emptyTrackingAndExtensionFieldsSurviveSave()
    {
        writeConfig("{\"version\":1,\"trackingParameters\":[],\"custom\":{\"keep\":true},\"rules\":[]}");
        RuleStore store;
        QVERIFY(store.trackingParameters().isEmpty());
        QVERIFY(store.setHoldMs(100));
        RuleStore reloaded;
        QVERIFY(reloaded.trackingParameters().isEmpty());
        QFile file(RuleStore::filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("custom")).toObject()
                    .value(QStringLiteral("keep")).toBool());
    }

    void schemaErrorsKeepLastGoodState()
    {
        RuleStore store;
        QVERIFY(store.setHoldMs(1200));
        for (const auto &bytes : {QByteArray("{\"version\":2}"), QByteArray("{\"holdMs\":-1}"),
                                  QByteArray("{\"rules\":[{\"match\":\"oops\",\"pattern\":\"x\",\"target\":\"a\"}]}"),
                                  QByteArray("{\"rules\":[{\"match\":\"regex\",\"pattern\":\"[\",\"target\":\"a\"}]}")}) {
            writeConfig(bytes);
            QVERIFY(!store.load());
            QCOMPARE(store.holdMs(), 1200);
        }
    }

    void pathPrefixWithoutAPathIsRejectedRatherThanSilentlyDead()
    {
        RuleStore store;
        QVERIFY(store.setHoldMs(1200));

        // The engine splits a pathPrefix pattern at the first slash, so one
        // without a host or without a path matches nothing at all. Refusing it
        // is the only way the author ever finds out.
        for (const auto &pattern : {QByteArray("github.com"), QByteArray("/anthropics"), QByteArray("   ")}) {
            writeConfig("{\"rules\":[{\"match\":\"pathPrefix\",\"pattern\":\"" + pattern
                        + "\",\"target\":\"a.desktop\"}]}");
            QVERIFY2(!store.load(), pattern.constData());
            QCOMPARE(store.holdMs(), 1200);
            QVERIFY(store.lastError().contains(QLatin1String("pathPrefix"))
                    || store.lastError().contains(QLatin1String("pattern")));
        }

        // "host/" is a host rule written the long way round, not a mistake:
        // every path starts with "/", which is exactly what it asks for.
        for (const auto &pattern : {QByteArray("github.com/"), QByteArray("github.com/anthropics")}) {
            writeConfig("{\"rules\":[{\"match\":\"pathPrefix\",\"pattern\":\"" + pattern
                        + "\",\"target\":\"a.desktop\"}]}");
            QVERIFY2(store.load(), pattern.constData());
            QCOMPARE(store.rules().size(), 1);
        }
    }

    void watchesCreationReplacementAndRecreation()
    {
        QDir(QFileInfo(RuleStore::filePath()).absolutePath()).removeRecursively();
        RuleStore store;
        writeConfig("{\"holdMs\":123}");
        QTRY_COMPARE(store.holdMs(), 123);
        writeConfig("{\"holdMs\":456}");
        QTRY_COMPARE(store.holdMs(), 456);
        QVERIFY(QFile::remove(RuleStore::filePath()));
        QTRY_COMPARE(store.holdMs(), 600);
        writeConfig("{\"holdMs\":789}");
        QTRY_COMPARE(store.holdMs(), 789);
    }

    void redirectSettingsAreReadAndValidated()
    {
        RuleStore store;
        QVERIFY(store.unwrapRedirects()); // on unless it is turned off
        QVERIFY(store.redirectWrappers().isEmpty());

        writeConfig("{\"unwrapRedirects\":false,\"redirectWrappers\":{\"go.corp.example/out\":\"to\"}}");
        QTRY_VERIFY(!store.unwrapRedirects());
        QCOMPARE(store.redirectWrappers().value(QStringLiteral("go.corp.example/out")), QStringLiteral("to"));

        writeConfig("{\"redirectWrappers\":{\"go.corp.example\":true}}");
        QTRY_VERIFY(store.lastError().contains(QLatin1String("redirectWrappers")));
        // The last valid configuration is still the one in force.
        QCOMPARE(store.redirectWrappers().value(QStringLiteral("go.corp.example/out")), QStringLiteral("to"));
    }

    void deletingTheFileIsAResetRatherThanAnError()
    {
        // Removing rules.json is how someone starts over. Treating it as a
        // read failure would keep the old contents and refuse every later
        // save, so "remember this choice" would quietly stop working.
        RuleStore store;
        QVERIFY(store.remember(QStringLiteral("a.example"), QStringLiteral("firefox.desktop"), false));
        QVERIFY(QFile::remove(RuleStore::filePath()));

        QVERIFY(store.load());
        QVERIFY(store.lastError().isEmpty());
        QVERIFY(store.rules().isEmpty());

        QVERIFY(store.remember(QStringLiteral("b.example"), QStringLiteral("firefox.desktop"), false));
        QCOMPARE(store.rules().size(), 1);
    }

    void failedSaveDoesNotCreateAnInMemoryRule()
    {
        RuleStore store;
        QDir().mkpath(QFileInfo(RuleStore::filePath()).absolutePath());
        QLockFile lock(RuleStore::filePath() + QStringLiteral(".lock"));
        QVERIFY(lock.tryLock(0));
        QSignalSpy errors(&store, &RuleStore::errorOccurred);
        QVERIFY(!store.remember(QStringLiteral("example.com"), QStringLiteral("a.desktop"), false));
        QVERIFY(store.rules().isEmpty());
        QCOMPARE(errors.count(), 1);
        lock.unlock();
        QVERIFY(store.remember(QStringLiteral("example.com"), QStringLiteral("a.desktop"), false));
        QCOMPARE(store.rules().size(), 1);
    }
};

QTEST_GUILESS_MAIN(RuleStoreTest)
#include "RuleStoreTest.moc"
