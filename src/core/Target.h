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
 * Browser means a recognized engine or a WebBrowser desktop category.
 * Everything else that registers for x-scheme-handler/https is
 * an OtherHandler: still routable, just not enabled by default. chatgpt.desktop
 * is the motivating example -- it claims https for OAuth callbacks, and whether
 * sending a link there is useful is the user's call, not ours.
 */
enum class TargetKind {
    Browser,
    OtherHandler,
};

struct Target {
    bool operator==(const Target &) const = default;
    QString id; // stable identity; "<storageId>" or "<storageId>#<profileKey>"
    QString label;
    QString iconName;

    QString storageId; // desktop file id, e.g. "microsoft-edge.desktop"
    QString execPath;  // resolved absolute binary
    QStringList command; // desktop Exec tokens, including wrappers and field codes
    QStringList privateCommand;
    QString desktopFilePath;
    QString applicationName;
    QString flatpakId;
    QString browserExecutable;
    QString workingDirectory;
    bool terminal = false;
    QString terminalOptions;
    int applicationIndex = -1; // actual application/ref after env or flatpak options

    EngineFamily family = EngineFamily::Unknown;
    TargetKind kind = TargetKind::OtherHandler;

    QString dataDir; // profile store root, empty if none found

    // Empty for the browser's own default profile.
    QString profileKey;  // Chromium: directory name. Gecko: absolute profile path.
    QString profileName; // display name
    bool isDefaultProfile = false;

    // Taken from the entry's own new-private-window action where it has one,
    // because the flag is vendor-specific: Edge says --inprivate, Chrome says
    // --incognito, Firefox says --private-window.
    QString privateFlag;

    bool supportsPrivate() const
    {
        return !privateFlag.isEmpty();
    }

    QStringList launchCommand(bool privateWindow) const
    {
        // Known engines use the original command plus a vendor-specific flag,
        // retaining custom environment and user-data-dir options. An unknown
        // engine may need the entire advertised desktop action.
        if (privateWindow && !privateCommand.isEmpty() && (family == EngineFamily::Unknown || command.isEmpty())) {
            return privateCommand;
        }
        return command;
    }
};

} // namespace Lob
