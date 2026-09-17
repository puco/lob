#pragma once

#include <QString>
#include <QJsonObject>
#include <QRegularExpression>

namespace Lob
{

enum class MatchKind {
    Host,       ///< exact host match
    HostSuffix, ///< host is, or ends in, the pattern ("*.corp.example")
    PathPrefix, ///< host matches and the path starts with the pattern
    Regex,      ///< regular expression against the URL; add anchors to require a whole-URL match
};

/**
 * What "remember this" was asked to cover. Maps onto the match kinds above:
 * a memory is an ordinary rule with the remembered flag, not a separate store.
 */
enum class MemoryScope {
    None,   ///< do not remember
    Host,   ///< exactly this host
    Domain, ///< this host and everything under its registrable domain
    Path,   ///< this host and everything under the first path segment
};

enum class RuleAction {
    Open, ///< open in targetId
    Ask,  ///< always show the picker, even if a later rule would match
    Copy, ///< copy to the clipboard instead of opening
};

struct Rule {
    MatchKind matchKind = MatchKind::Host;
    QString pattern;
    RuleAction action = RuleAction::Open;
    QString targetId;
    bool privateWindow = false;
    bool enabled = true;

    /// Written by the picker's "remember for this host" rather than by hand.
    /// Explicit rules always win over these, so a deliberate rule is never
    /// shadowed by a choice made in passing.
    bool remembered = false;
    bool caseSensitive = false; // v1 keeps its historical default
    int targetVersion = 1; // 2 distinguishes an intentional browser-default memory from a legacy profile ID
    QJsonObject extensions; // retain fields written by external editors

    /// Compiled form of a Regex pattern. Matching uses this and nothing else,
    /// so whoever builds a Rule compiles it: RuleStore does that when it reads
    /// the file, and anything constructing a Rule by hand must call compile().
    QRegularExpression expression;

    void compile()
    {
        if (matchKind != MatchKind::Regex) {
            expression = {};
            return;
        }
        expression = QRegularExpression(pattern,
            caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        expression.optimize();
    }
};

struct Decision {
    enum class Source {
        Rule,     ///< an explicit rule matched
        Memory,   ///< a remembered choice matched
        Fallback, ///< no match, but a default target is configured
        Ask,      ///< show the picker
    };

    Source source = Source::Ask;
    QString targetId;
    bool privateWindow = false;
    RuleAction action = RuleAction::Ask;
    bool legacyTarget = false;

    /// Index into the rule list that produced this, or -1. For --explain.
    int ruleIndex = -1;

    bool opensWithoutAsking() const
    {
        return action != RuleAction::Ask && (source == Source::Rule || source == Source::Memory || source == Source::Fallback);
    }
};

} // namespace Lob
