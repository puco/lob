#pragma once

#include "Rule.h"

#include <QList>
#include <QUrl>

namespace Lob
{

/**
 * Decides where a URL goes. Pure: no I/O, no Qt GUI, no clock. Everything the
 * routing depends on is an argument, so the whole decision table is testable.
 */
class RuleEngine
{
public:
    /**
     * @param rules evaluated in order; explicit rules are considered before
     *        remembered ones regardless of their position in the list.
     * @param fallbackTargetId used when nothing matches. Empty means ask.
     */
    static Decision decide(const QUrl &url, const QList<Rule> &rules, const QString &fallbackTargetId = {});

    static bool matches(const Rule &rule, const QUrl &url);

    /// Human-readable description of a rule, for --explain and the settings UI.
    static QString describe(const Rule &rule);
};

} // namespace Lob
