#include "core/RuleStore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Lob;

class RuleStoreTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

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
};

QTEST_GUILESS_MAIN(RuleStoreTest)
#include "RuleStoreTest.moc"
