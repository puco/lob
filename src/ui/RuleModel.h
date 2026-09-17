#pragma once

#include "core/Rule.h"

#include <QAbstractListModel>
#include <QList>

namespace Lob
{

class RuleStore;

/**
 * One section of rules.json as a list: either the rules someone wrote or the
 * choices the picker remembered. They are the same kind of object in the file
 * and deliberately not the same kind of thing on screen -- a rule is a decision
 * someone made, a memory is one they made in passing -- so each section gets
 * its own model over the same store.
 *
 * Every edit goes through RuleStore::setRules, which validates, detects a
 * concurrent write and reports what went wrong. Nothing is held back here: the
 * model has no pending state of its own, so a file changed underneath simply
 * reloads and the view follows.
 */
class RuleModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Section {
        Written,    ///< rules someone wrote; order decides which matches first
        Remembered, ///< what "remember this" recorded
    };

    enum Roles {
        PatternRole = Qt::UserRole + 1,
        MatchKindRole,
        ActionRole,
        TargetIdRole,
        PrivateWindowRole,
        EnabledRole,
        CaseSensitiveRole,
        DescriptionRole, ///< the same sentence --explain prints
    };

    RuleModel(RuleStore *store, Section section, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    /// Edits. Each returns false and leaves everything alone when the store
    /// refuses the result, so a rejected edit never half-lands.
    Q_INVOKABLE bool setEnabled(int row, bool enabled);
    Q_INVOKABLE bool remove(int row);

    /// Order is semantics for written rules: the first match wins. Moving one
    /// is therefore an edit, not a view preference, and it is refused outright
    /// for memories, whose order is maintained by specificity.
    Q_INVOKABLE bool move(int row, int delta);

    Q_INVOKABLE bool setPattern(int row, const QString &pattern);
    Q_INVOKABLE bool setMatchKind(int row, int kind);
    Q_INVOKABLE bool setAction(int row, int action);
    Q_INVOKABLE bool setTargetId(int row, const QString &targetId);
    Q_INVOKABLE bool setPrivateWindow(int row, bool privateWindow);
    Q_INVOKABLE bool setCaseSensitive(int row, bool caseSensitive);

    /// Appends a complete rule. The store refuses an incomplete one -- an
    /// empty pattern, a pathPrefix with no path, an open action with no target
    /// -- so a rule is added only once it is one, and a refusal comes back as
    /// false with RuleStore::lastError() saying which field was wrong. That is
    /// what lets the form report on the field rather than on the save.
    /// Written rules only.
    Q_INVOKABLE bool appendRule(const QString &pattern, int matchKind, int action,
                                const QString &targetId, bool privateWindow);

Q_SIGNALS:
    void countChanged();

private:
    void reload();
    /// Index into the store's full list for @p row of this section.
    int storeIndex(int row) const;
    bool commit(const QList<Rule> &rules);
    bool mutateAt(int row, const std::function<void(Rule &)> &change);

    RuleStore *m_store;
    Section m_section;
    QList<int> m_rows; // indices into the store's list, in file order
};

} // namespace Lob
