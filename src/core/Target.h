#pragma once

#include <QString>
#include <QStringList>

namespace Lob
{

enum class EngineFamily {
    Unknown,
    Gecko,
    Chromium,
};

/**
 * Browser means "we fingerprinted the engine and confirmed it by finding its
 * profile store". Everything else that registers for x-scheme-handler/https is
 * an OtherHandler: still routable, just not enabled by default. chatgpt.desktop
 * is the motivating example -- it claims https for OAuth callbacks, and whether
 * sending a link there is useful is the user's call, not ours.
 */
enum class TargetKind {
    Browser,
    OtherHandler,
};

struct Target {
    QString id; // stable identity; "<storageId>" or "<storageId>#<profileKey>"
    QString label;
    QString iconName;

    QString storageId; // desktop file id, e.g. "microsoft-edge.desktop"
    QString execPath;  // resolved absolute binary

    EngineFamily family = EngineFamily::Unknown;
    TargetKind kind = TargetKind::OtherHandler;

    QString dataDir; // profile store root, empty if none found

    // Empty for the browser's own default profile.
    QString profileKey;  // Chromium: directory name. Gecko: absolute profile path.
    QString profileName; // display name

    // Taken from the entry's own new-private-window action where it has one,
    // because the flag is vendor-specific: Edge says --inprivate, Chrome says
    // --incognito, Firefox says --private-window.
    QString privateFlag;

    bool supportsPrivate() const
    {
        return !privateFlag.isEmpty();
    }
};

} // namespace Lob
