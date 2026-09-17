#include "RuleModel.h"

#include "core/RuleEngine.h"
#include "core/RuleStore.h"

#include <functional>

namespace Lob
{

RuleModel::RuleModel(RuleStore *store, Section section, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
    , m_section(section)
{
    reload();
    // The store is the single copy. A file edited outside the window, a rule
    // remembered by a picker in this same process, an undo from the CLI: all
    // of them arrive here as a reload rather than as a conflict, because this
    // model never holds an edit of its own.
    connect(m_store, &RuleStore::changed, this, [this] {
        beginResetModel();
        reload();
        endResetModel();
        Q_EMIT countChanged();
    });
}

void RuleModel::reload()
{
    m_rows.clear();
    const auto &rules = m_store->rules();
    for (int i = 0; i < rules.size(); ++i) {
        if (rules.at(i).remembered == (m_section == Remembered)) {
            m_rows.append(i);
        }
    }
}

int RuleModel::storeIndex(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : -1;
}

int RuleModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int RuleModel::count() const
{
    return m_rows.size();
}

QVariant RuleModel::data(const QModelIndex &index, int role) const
{
    const int at = storeIndex(index.row());
    if (at < 0) {
        return {};
    }
    const Rule &rule = m_store->rules().at(at);
    switch (role) {
    case PatternRole:
        return rule.pattern;
    case MatchKindRole:
        return int(rule.matchKind);
    case ActionRole:
        return int(rule.action);
    case TargetIdRole:
        return rule.targetId;
    case PrivateWindowRole:
        return rule.privateWindow;
    case EnabledRole:
        return rule.enabled;
    case CaseSensitiveRole:
        return rule.caseSensitive;
    case DescriptionRole:
        return RuleEngine::describe(rule);
    default:
        return {};
    }
}

QHash<int, QByteArray> RuleModel::roleNames() const
{
    return {
        {PatternRole, QByteArrayLiteral("pattern")},
        {MatchKindRole, QByteArrayLiteral("matchKind")},
        {ActionRole, QByteArrayLiteral("action")},
        {TargetIdRole, QByteArrayLiteral("targetId")},
        {PrivateWindowRole, QByteArrayLiteral("privateWindow")},
        {EnabledRole, QByteArrayLiteral("enabled")},
        {CaseSensitiveRole, QByteArrayLiteral("caseSensitive")},
        {DescriptionRole, QByteArrayLiteral("description")},
    };
}

bool RuleModel::commit(const QList<Rule> &rules)
{
    // setRules validates and reports; a refusal leaves the store untouched, so
    // there is nothing to roll back here. The reset arrives via changed() when
    // it succeeds.
    return m_store->setRules(rules);
}

bool RuleModel::mutateAt(int row, const std::function<void(Rule &)> &change)
{
    const int at = storeIndex(row);
    if (at < 0) {
        return false;
    }
    auto rules = m_store->rules();
    change(rules[at]);
    rules[at].compile(); // a changed pattern or kind means a stale expression
    return commit(rules);
}

bool RuleModel::setEnabled(int row, bool enabled)
{
    return mutateAt(row, [enabled](Rule &rule) { rule.enabled = enabled; });
}

bool RuleModel::setPattern(int row, const QString &pattern)
{
    return mutateAt(row, [&pattern](Rule &rule) { rule.pattern = pattern; });
}

bool RuleModel::setMatchKind(int row, int kind)
{
    if (kind < int(MatchKind::Host) || kind > int(MatchKind::Regex)) {
        return false;
    }
    return mutateAt(row, [kind](Rule &rule) { rule.matchKind = MatchKind(kind); });
}

bool RuleModel::setAction(int row, int action)
{
    if (action < int(RuleAction::Open) || action > int(RuleAction::Copy)) {
        return false;
    }
    return mutateAt(row, [action](Rule &rule) { rule.action = RuleAction(action); });
}

bool RuleModel::setTargetId(int row, const QString &targetId)
{
    return mutateAt(row, [&targetId](Rule &rule) {
        rule.targetId = targetId;
        // Anything chosen here names a profile, so it is a current-generation
        // id whatever the rule carried before.
        rule.targetVersion = 2;
    });
}

bool RuleModel::setPrivateWindow(int row, bool privateWindow)
{
    return mutateAt(row, [privateWindow](Rule &rule) { rule.privateWindow = privateWindow; });
}

bool RuleModel::setCaseSensitive(int row, bool caseSensitive)
{
    return mutateAt(row, [caseSensitive](Rule &rule) { rule.caseSensitive = caseSensitive; });
}

bool RuleModel::remove(int row)
{
    const int at = storeIndex(row);
    if (at < 0) {
        return false;
    }
    auto rules = m_store->rules();
    rules.removeAt(at);
    return commit(rules);
}

bool RuleModel::move(int row, int delta)
{
    if (m_section == Remembered) {
        // Memories are ordered narrowest-first by the code that writes them,
        // and a user dragging one out of that order would be reordering
        // something they did not write into an order that means something
        // else. The section is a list to read and delete from, not to sort.
        return false;
    }
    const int target = row + delta;
    if (storeIndex(row) < 0 || storeIndex(target) < 0) {
        return false;
    }
    auto rules = m_store->rules();
    // Moving within the section means swapping the store positions the two
    // rows occupy, which is what keeps the memories between them where they
    // are.
    rules.move(storeIndex(row), storeIndex(target));
    return commit(rules);
}

bool RuleModel::appendRule(const QString &pattern, int matchKind, int action,
                           const QString &targetId, bool privateWindow)
{
    if (m_section == Remembered) {
        // A memory is written by choosing one in the picker. Letting the
        // window forge one would make a deliberate rule that lies about where
        // it came from, and the two sections mean different things precisely
        // because of that.
        return false;
    }
    if (matchKind < int(MatchKind::Host) || matchKind > int(MatchKind::Regex)
        || action < int(RuleAction::Open) || action > int(RuleAction::Copy)) {
        return false;
    }

    Rule rule;
    rule.matchKind = MatchKind(matchKind);
    rule.action = RuleAction(action);
    rule.pattern = pattern;
    rule.targetId = targetId;
    rule.privateWindow = privateWindow;
    rule.targetVersion = 2;
    rule.compile();

    auto rules = m_store->rules();
    rules.append(rule);
    return commit(rules);
}

} // namespace Lob
