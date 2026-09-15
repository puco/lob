#pragma once

#include "core/Target.h"

#include <QAbstractListModel>
#include <QList>

namespace Lob
{

class TargetModel : public QAbstractListModel
{
    Q_OBJECT

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
    Target at(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<Target> m_targets;
};

} // namespace Lob
