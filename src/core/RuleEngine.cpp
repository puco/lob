#include "RuleEngine.h"

#include <KLocalizedString>

#include <QRegularExpression>

namespace Lob
{

namespace
{

Decision fromRule(const Rule &rule, int index, Decision::Source source)
{
    Decision decision;
    decision.source = source;
    decision.action = rule.action;
    decision.targetId = rule.targetId;
    decision.privateWindow = rule.privateWindow;
    decision.legacyTarget = rule.targetVersion == 1;
    decision.ruleIndex = index;
    return decision;
}

} // namespace

bool RuleEngine::matches(const Rule &rule, const QUrl &url)
{
    if (!rule.enabled || rule.pattern.isEmpty()) {
        return false;
    }

    const QString host = url.host().toLower();
    const QString pattern = rule.pattern.trimmed().toLower();

    switch (rule.matchKind) {
    case MatchKind::Host:
        return host == pattern;

    case MatchKind::HostSuffix: {
        // "*.corp.example" and "corp.example" mean the same thing, and both
        // include the bare domain -- a rule for a company's domain that failed
        // to cover the domain itself would surprise everyone.
        QString suffix = pattern;
        if (suffix.startsWith(QLatin1String("*."))) {
            suffix = suffix.mid(2);
        }
        return host == suffix || host.endsWith(QLatin1Char('.') + suffix);
    }

    case MatchKind::PathPrefix: {
        // "github.com/anthropics" -- host and path in one pattern, since that
        // is how people write the thing they want to match.
        const int slash = pattern.indexOf(QLatin1Char('/'));
        if (slash < 0) {
            return false;
        }
        const QString patternHost = pattern.left(slash);
        const QString patternPath = rule.pattern.trimmed().mid(slash);
        const QString path = url.path();
        if (host != patternHost || !path.startsWith(patternPath, rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)) {
            return false;
        }
        // A remembered path is a whole first segment -- "github.com/kde" was
        // said about KDE, not about kdeconnect or kdenlive. A hand-written
        // prefix stays a plain prefix, because that is what it was sold as.
        return !rule.remembered || path.size() == patternPath.size() || patternPath.endsWith(QLatin1Char('/'))
            || path.at(patternPath.size()) == QLatin1Char('/');
    }

    case MatchKind::Regex:
        // Compiled once by whoever built the rule; an invalid pattern never
        // gets past RuleStore's validation and matches nothing here. An
        // uncompiled expression is empty, and an empty one would match every
        // URL, so it has to be refused rather than run.
        return !rule.expression.pattern().isEmpty() && rule.expression.isValid()
            && rule.expression.match(url.toString()).hasMatch();
    }

    return false;
}

Decision RuleEngine::decide(const QUrl &url, const QList<Rule> &rules, const QString &fallbackTargetId)
{
    // Explicit rules first, in order. A rule someone wrote deliberately should
    // never be shadowed by a "remember this" chosen in passing, whatever order
    // they happen to sit in the file.
    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        if (!rule.remembered && matches(rule, url)) {
            return fromRule(rule, i, Decision::Source::Rule);
        }
    }

    for (int i = 0; i < rules.size(); ++i) {
        const Rule &rule = rules.at(i);
        if (rule.remembered && matches(rule, url)) {
            return fromRule(rule, i, Decision::Source::Memory);
        }
    }

    if (!fallbackTargetId.isEmpty()) {
        Decision decision;
        decision.source = Decision::Source::Fallback;
        decision.action = RuleAction::Open;
        decision.targetId = fallbackTargetId;
        return decision;
    }

    return {};
}

QString RuleEngine::describe(const Rule &rule)
{
    // Built in two halves -- what it matches, then what it does -- because
    // that is how the rule itself is written. Translators get the halves with
    // context saying how they are joined, since word order across the join is
    // not something a fragment can express on its own.
    QString match;
    switch (rule.matchKind) {
    case MatchKind::Host:
        match = i18nc("what a rule matches; becomes %1 of a \"<match> -> <action>\" line", "host is %1", rule.pattern);
        break;
    case MatchKind::HostSuffix:
        match = i18nc("what a rule matches; becomes %1 of a \"<match> -> <action>\" line", "host is or ends in %1", rule.pattern);
        break;
    case MatchKind::PathPrefix:
        match = i18nc("what a rule matches; becomes %1 of a \"<match> -> <action>\" line", "URL starts with %1", rule.pattern);
        break;
    case MatchKind::Regex:
        match = i18nc("what a rule matches; becomes %1 of a \"<match> -> <action>\" line", "URL matches /%1/", rule.pattern);
        break;
    }

    switch (rule.action) {
    case RuleAction::Ask:
        return i18nc("%1 is what the rule matches", "%1 → always ask", match);
    case RuleAction::Copy:
        return i18nc("%1 is what the rule matches", "%1 → copy instead of opening", match);
    case RuleAction::Open:
        break;
    }

    return rule.privateWindow
        ? i18nc("%1 is what the rule matches, %2 a browser or profile id", "%1 → open in %2 (private)", match, rule.targetId)
        : i18nc("%1 is what the rule matches, %2 a browser or profile id", "%1 → open in %2", match, rule.targetId);
}

} // namespace Lob
