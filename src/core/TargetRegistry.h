#pragma once

#include "Target.h"

#include <QList>
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
};

} // namespace Lob
