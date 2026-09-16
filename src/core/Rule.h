#pragma once

#include <QString>

namespace Lob
{

enum class MatchKind {
    Host,       ///< exact host match
    HostSuffix, ///< host is, or ends in, the pattern ("*.corp.example")
    PathPrefix, ///< host matches and the path starts with the pattern
    Regex,      ///< anchored match against the whole URL
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

    /// Index into the rule list that produced this, or -1. For --explain.
    int ruleIndex = -1;

    bool opensWithoutAsking() const
    {
        return source == Source::Rule || source == Source::Memory || source == Source::Fallback;
    }
};

} // namespace Lob
