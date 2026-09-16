#include "core/RuleEngine.h"

#include <QTest>

using namespace Lob;

namespace
{

Rule rule(MatchKind kind, const QString &pattern, const QString &target, bool remembered = false)
{
    Rule r;
    r.matchKind = kind;
    r.pattern = pattern;
    r.action = RuleAction::Open;
    r.targetId = target;
    r.remembered = remembered;
    r.compile();
    return r;
}

} // namespace

class RuleEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void hostMatchesExactlyAndNotSubdomains()
    {
        const Rule r = rule(MatchKind::Host, QStringLiteral("example.com"), QStringLiteral("a"));
        QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/x"))));
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://sub.example.com/x"))));
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://notexample.com/x"))));
    }

    void hostSuffixCoversTheBareDomain()
    {
        // A rule for a company domain that failed to cover the domain itself
        // would surprise everyone, so both spellings include it.
        for (const auto &pattern : {"corp.example", "*.corp.example"}) {
            const Rule r = rule(MatchKind::HostSuffix, QString::fromLatin1(pattern), QStringLiteral("a"));
            QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://corp.example/"))));
            QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://mail.corp.example/"))));
            QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://evilcorp.example/"))));
        }
    }

    void pathPrefixNeedsHostAndPath()
    {
        const Rule r = rule(MatchKind::PathPrefix, QStringLiteral("github.com/anthropics"), QStringLiteral("a"));
        QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://github.com/anthropics/claude-code"))));
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://github.com/other/repo"))));
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://gitlab.com/anthropics/x"))));
    }

    void invalidRegexNeverMatches()
    {
        Rule r = rule(MatchKind::Regex, QStringLiteral("([unclosed"), QStringLiteral("a"));
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/"))));
    }

    void disabledRuleIsSkipped()
    {
        Rule r = rule(MatchKind::Host, QStringLiteral("example.com"), QStringLiteral("a"));
        r.enabled = false;
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/"))));
    }

    void explicitRulesBeatRememberedOnesRegardlessOfOrder()
    {
        // The remembered rule sits first in the list and still loses: a choice
        // made in passing must not shadow one written deliberately.
        const QList<Rule> rules = {
            rule(MatchKind::Host, QStringLiteral("example.com"), QStringLiteral("remembered"), true),
            rule(MatchKind::Host, QStringLiteral("example.com"), QStringLiteral("explicit")),
        };

        const Decision d = RuleEngine::decide(QUrl(QStringLiteral("https://example.com/")), rules);
        QCOMPARE(d.source, Decision::Source::Rule);
        QCOMPARE(d.targetId, QStringLiteral("explicit"));
    }

    void firstMatchingRuleWins()
    {
        const QList<Rule> rules = {
            rule(MatchKind::Host, QStringLiteral("example.com"), QStringLiteral("first")),
            rule(MatchKind::Host, QStringLiteral("example.com"), QStringLiteral("second")),
        };
        QCOMPARE(RuleEngine::decide(QUrl(QStringLiteral("https://example.com/")), rules).targetId,
                 QStringLiteral("first"));
    }

    void askActionSuppressesAutomaticOpening()
    {
        QList<Rule> rules;
        Rule ask = rule(MatchKind::Host, QStringLiteral("bank.example"), QString());
        ask.action = RuleAction::Ask;
        rules << ask;

        const Decision d = RuleEngine::decide(QUrl(QStringLiteral("https://bank.example/")), rules,
                                              QStringLiteral("fallback"));
        QCOMPARE(d.source, Decision::Source::Rule);
        QCOMPARE(d.ruleIndex, 0);
        QVERIFY(!d.opensWithoutAsking());
    }

    void fallbackOnlyAppliesWhenNothingMatched()
    {
        const Decision d = RuleEngine::decide(QUrl(QStringLiteral("https://nowhere.example/")), {},
                                              QStringLiteral("fallback"));
        QCOMPARE(d.source, Decision::Source::Fallback);
        QCOMPARE(d.targetId, QStringLiteral("fallback"));

        const Decision none = RuleEngine::decide(QUrl(QStringLiteral("https://nowhere.example/")), {});
        QCOMPARE(none.source, Decision::Source::Ask);
    }

    void caseSensitivityIsExplicitAndPreservesV1Default()
    {
        auto r = rule(MatchKind::PathPrefix, QStringLiteral("example.com/Work"), QStringLiteral("a"));
        QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/work"))));
        r.caseSensitive = true;
        r.compile();
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/work"))));
        QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://EXAMPLE.com/Work/page"))));
        r.matchKind = MatchKind::Regex;
        r.pattern = QStringLiteral("/Work$");
        r.compile();
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/work"))));
        QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/Work"))));
    }

    void anUncompiledRegexRuleMatchesNothing()
    {
        // Matching reads the compiled expression only, and an empty one would
        // otherwise match every URL there is.
        Rule r;
        r.matchKind = MatchKind::Regex;
        r.pattern = QStringLiteral("^https://example\\.com/");
        QVERIFY(!RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/x"))));
        r.compile();
        QVERIFY(RuleEngine::matches(r, QUrl(QStringLiteral("https://example.com/x"))));
    }
};

QTEST_GUILESS_MAIN(RuleEngineTest)
#include "RuleEngineTest.moc"
