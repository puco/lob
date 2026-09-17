#include "ui/RuleModel.h"
#include "core/RuleStore.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Lob;

class RuleModelTest : public QObject
{
    Q_OBJECT
    QTemporaryDir config;

    static Rule written(const QString &pattern, const QString &target)
    {
        Rule rule;
        rule.matchKind = MatchKind::Host;
        rule.pattern = pattern;
        rule.action = RuleAction::Open;
        rule.targetId = target;
        rule.targetVersion = 2;
        return rule;
    }

    static Rule memory(const QString &pattern, const QString &target)
    {
        Rule rule = written(pattern, target);
        rule.remembered = true;
        return rule;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(config.isValid()); qputenv("XDG_CONFIG_HOME", config.path().toUtf8()); }
    void init() { QFile::remove(RuleStore::filePath()); }

    void eachSectionShowsOnlyItsOwnKind()
    {
        RuleStore store;
        QVERIFY(store.setRules({written(QStringLiteral("a.example"), QStringLiteral("a.desktop")),
                                memory(QStringLiteral("b.example"), QStringLiteral("b.desktop")),
                                written(QStringLiteral("c.example"), QStringLiteral("c.desktop"))}));

        RuleModel rules(&store, RuleModel::Written);
        RuleModel memories(&store, RuleModel::Remembered);

        QCOMPARE(rules.rowCount(), 2);
        QCOMPARE(memories.rowCount(), 1);
        QCOMPARE(rules.data(rules.index(0, 0), RuleModel::PatternRole).toString(), QStringLiteral("a.example"));
        QCOMPARE(rules.data(rules.index(1, 0), RuleModel::PatternRole).toString(), QStringLiteral("c.example"));
        QCOMPARE(memories.data(memories.index(0, 0), RuleModel::PatternRole).toString(), QStringLiteral("b.example"));
    }

    void movingAWrittenRuleLeavesTheMemoriesBetweenThemAlone()
    {
        RuleStore store;
        QVERIFY(store.setRules({written(QStringLiteral("a.example"), QStringLiteral("a.desktop")),
                                memory(QStringLiteral("m.example"), QStringLiteral("m.desktop")),
                                written(QStringLiteral("c.example"), QStringLiteral("c.desktop"))}));

        RuleModel rules(&store, RuleModel::Written);
        QVERIFY(rules.move(0, 1));

        // The two written rules swapped; the memory is still a memory and is
        // still there. Order is semantics for the rules and nothing at all for
        // where the memory happens to sit.
        QCOMPARE(rules.data(rules.index(0, 0), RuleModel::PatternRole).toString(), QStringLiteral("c.example"));
        QCOMPARE(rules.data(rules.index(1, 0), RuleModel::PatternRole).toString(), QStringLiteral("a.example"));

        RuleModel memories(&store, RuleModel::Remembered);
        QCOMPARE(memories.rowCount(), 1);
        QCOMPARE(memories.data(memories.index(0, 0), RuleModel::PatternRole).toString(), QStringLiteral("m.example"));
    }

    void aMemoryCannotBeReorderedOrForged()
    {
        RuleStore store;
        QVERIFY(store.setRules({memory(QStringLiteral("a.example"), QStringLiteral("a.desktop")),
                                memory(QStringLiteral("b.example"), QStringLiteral("b.desktop"))}));

        RuleModel memories(&store, RuleModel::Remembered);
        // Memories are kept narrowest-first by whatever wrote them; dragging
        // one would be reordering something nobody wrote into an order that
        // means something else.
        QVERIFY(!memories.move(0, 1));
        QCOMPARE(memories.data(memories.index(0, 0), RuleModel::PatternRole).toString(), QStringLiteral("a.example"));

        // And the window must not be able to manufacture one: a memory says
        // where it came from, and that has to stay true.
        QVERIFY(!memories.appendRule(QStringLiteral("c.example"), int(MatchKind::Host),
                                     int(RuleAction::Open), QStringLiteral("c.desktop"), false));
        QCOMPARE(memories.rowCount(), 2);
    }

    void moveAndRemoveRefuseRowsThatAreNotThere()
    {
        RuleStore store;
        QVERIFY(store.setRules({written(QStringLiteral("a.example"), QStringLiteral("a.desktop"))}));
        RuleModel rules(&store, RuleModel::Written);

        QVERIFY(!rules.move(0, 1));  // nothing below it
        QVERIFY(!rules.move(0, -1)); // nothing above it
        QVERIFY(!rules.remove(5));
        QVERIFY(!rules.remove(-1));
        QCOMPARE(rules.rowCount(), 1);
    }

    void anIncompleteRuleIsRefusedWithSomethingToSayAboutIt()
    {
        RuleStore store;
        RuleModel rules(&store, RuleModel::Written);

        // The store already refuses these; the point is that the refusal
        // reaches the form rather than half-landing.
        QVERIFY(!rules.appendRule(QString(), int(MatchKind::Host), int(RuleAction::Open),
                                  QStringLiteral("a.desktop"), false));
        QVERIFY(!store.lastError().isEmpty());

        // A pathPrefix with no path matches nothing and says nothing about it.
        QVERIFY(!rules.appendRule(QStringLiteral("github.com"), int(MatchKind::PathPrefix),
                                  int(RuleAction::Open), QStringLiteral("a.desktop"), false));

        // Open with no target is not a rule either.
        QVERIFY(!rules.appendRule(QStringLiteral("a.example"), int(MatchKind::Host),
                                  int(RuleAction::Open), QString(), false));

        QCOMPARE(rules.rowCount(), 0);

        // The same rule, complete, goes in.
        QVERIFY(rules.appendRule(QStringLiteral("github.com/anthropics"), int(MatchKind::PathPrefix),
                                 int(RuleAction::Open), QStringLiteral("a.desktop"), false));
        QCOMPARE(rules.rowCount(), 1);
    }

    void aRejectedEditLeavesTheRuleAsItWas()
    {
        RuleStore store;
        QVERIFY(store.setRules({written(QStringLiteral("github.com"), QStringLiteral("a.desktop"))}));
        RuleModel rules(&store, RuleModel::Written);

        // Host -> pathPrefix without a path cannot be saved, and the rule must
        // not be left half-changed by the attempt.
        QVERIFY(!rules.setMatchKind(0, int(MatchKind::PathPrefix)));
        QCOMPARE(rules.data(rules.index(0, 0), RuleModel::MatchKindRole).toInt(), int(MatchKind::Host));
        QCOMPARE(rules.data(rules.index(0, 0), RuleModel::PatternRole).toString(), QStringLiteral("github.com"));
    }

    void aChangeOnDiskArrivesAsAReloadRatherThanAConflict()
    {
        RuleStore store;
        QVERIFY(store.setRules({written(QStringLiteral("a.example"), QStringLiteral("a.desktop"))}));
        RuleModel rules(&store, RuleModel::Written);
        QSignalSpy counted(&rules, &RuleModel::countChanged);

        // Stands in for the file being edited elsewhere, or a picker in this
        // same process remembering a choice: one store, so the model has no
        // copy of its own to reconcile.
        QVERIFY(store.setRules({written(QStringLiteral("a.example"), QStringLiteral("a.desktop")),
                                written(QStringLiteral("b.example"), QStringLiteral("b.desktop"))}));

        QCOMPARE(rules.rowCount(), 2);
        QVERIFY(counted.count() > 0);
    }

    void disablingARuleKeepsItInTheFile()
    {
        RuleStore store;
        QVERIFY(store.setRules({written(QStringLiteral("a.example"), QStringLiteral("a.desktop"))}));
        RuleModel rules(&store, RuleModel::Written);

        QVERIFY(rules.setEnabled(0, false));
        QCOMPARE(rules.rowCount(), 1);
        QVERIFY(!rules.data(rules.index(0, 0), RuleModel::EnabledRole).toBool());

        RuleStore reloaded;
        QCOMPARE(reloaded.rules().size(), 1);
        QVERIFY(!reloaded.rules().first().enabled);
    }

    void editingARegexRuleRecompilesIt()
    {
        Rule rule;
        rule.matchKind = MatchKind::Regex;
        rule.pattern = QStringLiteral("^https://a\\.example/");
        rule.action = RuleAction::Ask;
        rule.compile();

        RuleStore store;
        QVERIFY(store.setRules({rule}));
        RuleModel rules(&store, RuleModel::Written);

        QVERIFY(rules.setPattern(0, QStringLiteral("^https://b\\.example/")));

        // A stale compiled expression would keep matching the old pattern,
        // which is the kind of bug that looks like the edit never saved.
        const Rule &saved = store.rules().first();
        QCOMPARE(saved.pattern, QStringLiteral("^https://b\\.example/"));
        QVERIFY(saved.expression.match(QStringLiteral("https://b.example/x")).hasMatch());
        QVERIFY(!saved.expression.match(QStringLiteral("https://a.example/x")).hasMatch());
    }
};

// Guiless: this exercises the store and the models, so it must run where CI runs
// -- with no display at all. QTEST_MAIN would build a QGuiApplication, which
// aborts there and passes on a developer machine, which is the worst of both.
QTEST_GUILESS_MAIN(RuleModelTest)
#include "RuleModelTest.moc"
