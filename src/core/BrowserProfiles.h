#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Lob
{

struct ProfileInfo {
    /// Chromium: the profile directory name, which is exactly what
    /// --profile-directory= takes. Gecko: the absolute profile path, which is
    /// what --profile takes.
    QString key;
    QString name;
    bool isDefault = false;
};

/// Parses <dataDir>/profiles.ini. Returns empty if absent or unreadable.
QList<ProfileInfo> scanGeckoProfiles(const QString &dataDir);

/// Parses <dataDir>/Local State -> profile.info_cache. Returns empty if absent.
QList<ProfileInfo> scanChromiumProfiles(const QString &dataDir);

/// Directories that might hold a profile store for this executable, most
/// likely first. Existence is not checked here.
QStringList geckoDataDirCandidates(const QString &execBasename, const QString &storageId);
QStringList chromiumDataDirCandidates(const QString &execBasename, const QString &storageId);

/// True if the directory actually looks like a store of that kind. This is the
/// confirmation step: fingerprinting guesses the family, this proves it.
bool isGeckoDataDir(const QString &dataDir);
bool isChromiumDataDir(const QString &dataDir);

} // namespace Lob
