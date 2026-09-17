#include "TargetModel.h"

#include <algorithm>

namespace Lob
{

TargetModel::TargetModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

namespace
{

bool matches(const Target &target, const QStringList &terms)
{
    return std::all_of(terms.cbegin(), terms.cend(), [&target](const QString &term) {
        return target.label.contains(term, Qt::CaseInsensitive)
            || target.profileName.contains(term, Qt::CaseInsensitive)
            || target.storageId.contains(term, Qt::CaseInsensitive);
    });
}

} // namespace

void TargetModel::setTargets(const QList<Target> &targets)
{
    beginResetModel();
    m_targets = targets;
    rebuild();
    endResetModel();
    Q_EMIT countChanged();
}

void TargetModel::setFilter(const QString &text)
{
    if (m_filter == text) {
        return;
    }
    beginResetModel();
    m_filter = text;
    rebuild();
    endResetModel();
    Q_EMIT countChanged();
}

QString TargetModel::filter() const
{
    return m_filter;
}

void TargetModel::rebuild()
{
    m_visible.clear();
    const QStringList terms = m_filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < m_targets.size(); ++i) {
        if (terms.isEmpty() || matches(m_targets.at(i), terms)) {
            m_visible.append(i);
        }
    }
}

const QList<Target> &TargetModel::targets() const
{
    return m_targets;
}

Target TargetModel::at(int row) const
{
    if (row < 0 || row >= m_visible.size()) {
        return {};
    }
    return m_targets.at(m_visible.at(row));
}

int TargetModel::indexOfId(const QString &id) const
{
    for (int row = 0; row < m_visible.size(); ++row) {
        if (m_targets.at(m_visible.at(row)).id == id) {
            return row;
        }
    }
    return -1;
}

int TargetModel::count() const
{
    return m_visible.size();
}

int TargetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant TargetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size()) {
        return {};
    }

    const Target &target = m_targets.at(m_visible.at(index.row()));
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
        // Only the first nine get a digit; later targets use arrows, a click,
        // or the filter -- which is the point of the filter. The numbering is
        // of visible rows, so it renumbers as the filter narrows and a digit
        // always means the cell showing it.
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
