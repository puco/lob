#pragma once

#include "core/Target.h"

#include <QAbstractListModel>
#include <QList>

namespace Lob
{

class TargetModel : public QAbstractListModel
{
    Q_OBJECT

    /// rowCount() is invokable but not bindable; QML needs a notifying
    /// property to lay the grid out when the target list changes.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        LabelRole = Qt::UserRole + 1,
        IconNameRole,
        ProfileNameRole,
        IsBrowserRole,
        SupportsPrivateRole,
        ShortcutRole,
    };

    explicit TargetModel(QObject *parent = nullptr);

    void setTargets(const QList<Target> &targets);
    const QList<Target> &targets() const;

    /// Narrows the rows to those matching @p text. Every whitespace-separated
    /// term must appear in the label, the profile name or the desktop id, so
    /// "fire work" finds the work profile of Firefox without knowing which
    /// field holds which word. The desktop id is searched because it is what
    /// `lob --list` prints and what rules are written against.
    void setFilter(const QString &text);
    QString filter() const;

    /// Rows are filtered rows. Everything outside this class -- the digits,
    /// the selection, choose() -- therefore counts what is on screen, which is
    /// the only numbering a user can see.
    Target at(int row) const;

    /// Visible row showing @p id, or -1. Visible, because every index outside
    /// this class is a row on screen.
    int indexOfId(const QString &id) const;
    int count() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    void rebuild();

    QList<Target> m_targets;
    QList<int> m_visible; // indices into m_targets, in order
    QString m_filter;
};

} // namespace Lob
