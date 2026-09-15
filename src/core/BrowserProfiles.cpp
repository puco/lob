#include "BrowserProfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

namespace Lob
{

namespace
{

using IniFile = QMap<QString, QMap<QString, QString>>;

// Hand-rolled rather than QSettings: profiles.ini is trivial, and this keeps
// section order and exact key casing predictable for the unit tests.
IniFile parseIni(const QString &path)
{
    IniFile ini;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return ini;
    }

    QTextStream stream(&file);
    QString section;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            ini.insert(section, {});
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || section.isEmpty()) {
            continue;
        }
        ini[section].insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return ini;
}

QString homeDir()
{
    return QDir::homePath();
}

} // namespace

QList<ProfileInfo> scanGeckoProfiles(const QString &dataDir)
{
    const QString iniPath = dataDir + QLatin1String("/profiles.ini");
    const IniFile ini = parseIni(iniPath);
    if (ini.isEmpty()) {
        return {};
    }

    // Modern Firefox binds a profile to each installation directory via an
    // [InstallXXXX] section, and that is what actually launches -- the legacy
    // [ProfileN] Default=1 flag only records "last selected". We cannot compute
    // which [Install...] belongs to a given binary (the hash derivation is not
    // documented), so we only trust it when there is exactly one.
    QString installDefaultPath;
    int installSections = 0;
    for (auto it = ini.constBegin(); it != ini.constEnd(); ++it) {
        if (!it.key().startsWith(QLatin1String("Install"))) {
            continue;
        }
        ++installSections;
        installDefaultPath = it.value().value(QStringLiteral("Default"));
    }
    if (installSections != 1) {
        installDefaultPath.clear();
    }

    static const QRegularExpression profileSection(QStringLiteral("^Profile\\d+$"));

    QList<ProfileInfo> profiles;
    QString legacyDefaultPath;

    for (auto it = ini.constBegin(); it != ini.constEnd(); ++it) {
        if (!profileSection.match(it.key()).hasMatch()) {
            continue;
        }
        const QMap<QString, QString> &entry = it.value();
        const QString rawPath = entry.value(QStringLiteral("Path"));
        if (rawPath.isEmpty()) {
            continue;
        }

        const bool relative = entry.value(QStringLiteral("IsRelative"), QStringLiteral("1")) != QLatin1String("0");
        const QString absolute = relative ? QDir(dataDir).absoluteFilePath(rawPath) : rawPath;

        ProfileInfo info;
        info.key = QDir::cleanPath(absolute);
        info.name = entry.value(QStringLiteral("Name"), QFileInfo(absolute).fileName());
        profiles.append(info);

        if (entry.value(QStringLiteral("Default")) == QLatin1String("1")) {
            legacyDefaultPath = rawPath;
        }
    }

    const QString wanted = installDefaultPath.isEmpty() ? legacyDefaultPath : installDefaultPath;
    if (!wanted.isEmpty()) {
        const QString wantedAbs = QDir::cleanPath(QDir(dataDir).absoluteFilePath(wanted));
        for (ProfileInfo &info : profiles) {
            if (info.key == wantedAbs) {
                info.isDefault = true;
                break;
            }
        }
    }

    return profiles;
}

QList<ProfileInfo> scanChromiumProfiles(const QString &dataDir)
{
    QFile file(dataDir + QLatin1String("/Local State"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return {};
    }

    const QJsonObject cache = doc.object()
                                  .value(QStringLiteral("profile"))
                                  .toObject()
                                  .value(QStringLiteral("info_cache"))
                                  .toObject();
    if (cache.isEmpty()) {
        return {};
    }

    const QString lastUsed = doc.object()
                                 .value(QStringLiteral("profile"))
                                 .toObject()
                                 .value(QStringLiteral("last_used"))
                                 .toString();

    QList<ProfileInfo> profiles;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();

        ProfileInfo info;
        info.key = it.key();

        // Precedence: the user's own label, then the signed-in account name,
        // then the account address, then the bare directory key.
        for (const auto &field : {"name", "gaia_name", "user_name"}) {
            const QString value = entry.value(QLatin1String(field)).toString();
            if (!value.isEmpty()) {
                info.name = value;
                break;
            }
        }
        if (info.name.isEmpty()) {
            info.name = it.key();
        }

        info.isDefault = !lastUsed.isEmpty() ? (it.key() == lastUsed) : (it.key() == QLatin1String("Default"));
        profiles.append(info);
    }

    return profiles;
}

bool isGeckoDataDir(const QString &dataDir)
{
    return !dataDir.isEmpty() && QFileInfo::exists(dataDir + QLatin1String("/profiles.ini"));
}

bool isChromiumDataDir(const QString &dataDir)
{
    return !dataDir.isEmpty() && QFileInfo::exists(dataDir + QLatin1String("/Local State"));
}

QStringList geckoDataDirCandidates(const QString &execBasename, const QString &storageId)
{
    static const QMap<QString, QString> known = {
        {QStringLiteral("firefox"), QStringLiteral(".mozilla/firefox")},
        {QStringLiteral("firefox-developer-edition"), QStringLiteral(".mozilla/firefox")},
        {QStringLiteral("firefox-nightly"), QStringLiteral(".mozilla/firefox")},
        {QStringLiteral("librewolf"), QStringLiteral(".librewolf")},
        {QStringLiteral("zen"), QStringLiteral(".zen")},
        {QStringLiteral("zen-browser"), QStringLiteral(".zen")},
        {QStringLiteral("floorp"), QStringLiteral(".floorp")},
        {QStringLiteral("waterfox"), QStringLiteral(".waterfox")},
        {QStringLiteral("mullvad-browser"), QStringLiteral(".mullvad-browser")},
        {QStringLiteral("icecat"), QStringLiteral(".mozilla/icecat")},
    };

    QStringList candidates;
    const QString relative = known.value(execBasename);
    if (!relative.isEmpty()) {
        candidates << homeDir() + QLatin1Char('/') + relative;
    }

    // Flatpak relocates the whole home dir under ~/.var/app/<app-id>/.
    const QString appId = QString(storageId).remove(QLatin1String(".desktop"));
    if (appId.contains(QLatin1Char('.')) && !relative.isEmpty()) {
        candidates << homeDir() + QLatin1String("/.var/app/") + appId + QLatin1Char('/') + relative;
    }
    // Snap uses its own layout.
    if (execBasename == QLatin1String("firefox")) {
        candidates << homeDir() + QLatin1String("/snap/firefox/common/.mozilla/firefox");
    }

    return candidates;
}

QStringList chromiumDataDirCandidates(const QString &execBasename, const QString &storageId)
{
    static const QMap<QString, QString> known = {
        {QStringLiteral("google-chrome"), QStringLiteral("google-chrome")},
        {QStringLiteral("google-chrome-stable"), QStringLiteral("google-chrome")},
        {QStringLiteral("google-chrome-beta"), QStringLiteral("google-chrome-beta")},
        {QStringLiteral("google-chrome-unstable"), QStringLiteral("google-chrome-unstable")},
        {QStringLiteral("chromium"), QStringLiteral("chromium")},
        {QStringLiteral("chromium-browser"), QStringLiteral("chromium")},
        {QStringLiteral("thorium-browser"), QStringLiteral("thorium")},
        {QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge")},
        {QStringLiteral("microsoft-edge-stable"), QStringLiteral("microsoft-edge")},
        {QStringLiteral("microsoft-edge-beta"), QStringLiteral("microsoft-edge-beta")},
        {QStringLiteral("microsoft-edge-dev"), QStringLiteral("microsoft-edge-dev")},
        {QStringLiteral("vivaldi"), QStringLiteral("vivaldi")},
        {QStringLiteral("vivaldi-stable"), QStringLiteral("vivaldi")},
        {QStringLiteral("opera"), QStringLiteral("opera")},
        {QStringLiteral("opera-beta"), QStringLiteral("opera-beta")},
        // Brave nests one level deeper than everyone else.
        {QStringLiteral("brave"), QStringLiteral("BraveSoftware/Brave-Browser")},
        {QStringLiteral("brave-browser"), QStringLiteral("BraveSoftware/Brave-Browser")},
        {QStringLiteral("brave-browser-beta"), QStringLiteral("BraveSoftware/Brave-Browser-Beta")},
        {QStringLiteral("brave-browser-nightly"), QStringLiteral("BraveSoftware/Brave-Browser-Nightly")},
    };

    QStringList candidates;
    const QString relative = known.value(execBasename);
    if (!relative.isEmpty()) {
        candidates << homeDir() + QLatin1String("/.config/") + relative;
    }

    const QString appId = QString(storageId).remove(QLatin1String(".desktop"));
    if (appId.contains(QLatin1Char('.')) && !relative.isEmpty()) {
        candidates << homeDir() + QLatin1String("/.var/app/") + appId + QLatin1String("/config/") + relative;
    }

    return candidates;
}

} // namespace Lob
