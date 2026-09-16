#pragma once

#include "Target.h"

#include <QList>
#include <KService>
#include <QString>

namespace Lob
{

class TargetRegistry
{
public:
    /**
     * Discovers every application registered for x-scheme-handler/https,
     * classifies each one, and expands browsers into one target per profile.
     *
     * @param ownStorageId our own desktop id, excluded unconditionally --
     *        listing ourselves would be an infinite launch loop.
     */
    static QList<Target> discover(const QString &ownStorageId);
    // Also used by fixture-based discovery tests without the live KSycoca database.
    static QList<Target> fromServices(const KService::List &services, const QString &ownStorageId);

    /**
     * The target for @p id, or an empty target.
     *
     * @param remembered treats a bare desktop id as a memory written before
     *        profile ids existed: it resolves to that browser's only profile,
     *        and to nothing at all when several exist, because a memory from
     *        back then cannot say which one was meant.
     */
    static Target resolve(const QList<Target> &targets, const QString &id, bool remembered = false);

    /// The same targets minus rows that duplicate another one, for display.
    static QList<Target> withoutRedundantProfiles(const QList<Target> &targets);
};

} // namespace Lob
