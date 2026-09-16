#include "TargetModel.h"

namespace Lob
{

TargetModel::TargetModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void TargetModel::setTargets(const QList<Target> &targets)
{
    beginResetModel();
    m_targets = targets;
    endResetModel();
    Q_EMIT countChanged();
}

const QList<Target> &TargetModel::targets() const
{
    return m_targets;
}

Target TargetModel::at(int row) const
{
    if (row < 0 || row >= m_targets.size()) {
        return {};
    }
    return m_targets.at(row);
}

int TargetModel::count() const
{
    return m_targets.size();
}

int TargetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_targets.size();
}

QVariant TargetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_targets.size()) {
        return {};
    }

    const Target &target = m_targets.at(index.row());
    switch (role) {
    case LabelRole:
        return target.label;
    case IconNameRole:
        return target.iconName.isEmpty() ? QStringLiteral("internet-web-browser") : target.iconName;
    case ProfileNameRole:
        return target.profileName;
    case IsBrowserRole:
        return target.kind == TargetKind::Browser;
    case SupportsPrivateRole:
        return target.supportsPrivate();
    case ShortcutRole:
        // Only the first nine get a digit; later targets use arrows or a click.
        return index.row() < 9 ? QString::number(index.row() + 1) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> TargetModel::roleNames() const
{
    return {
        {LabelRole, QByteArrayLiteral("label")},
        {IconNameRole, QByteArrayLiteral("iconName")},
        {ProfileNameRole, QByteArrayLiteral("profileName")},
        {IsBrowserRole, QByteArrayLiteral("isBrowser")},
        {SupportsPrivateRole, QByteArrayLiteral("supportsPrivate")},
        {ShortcutRole, QByteArrayLiteral("shortcut")},
    };
}

} // namespace Lob
