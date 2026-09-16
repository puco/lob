#include "DefaultBrowserManager.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <KConfig>
#include <KLocalizedString>

Q_LOGGING_CATEGORY(LOG_DEFAULT, "lob.default")

namespace Lob
{

namespace
{

QString ownStorageId()
{
    return QStringLiteral(LOB_APP_ID ".desktop");
}

KSharedConfig::Ptr stateConfig()
{
    return KSharedConfig::openConfig(QStringLiteral("lobstate"), KConfig::SimpleConfig);
}

QString associationsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
}

bool exists(const QString &id, const AssociationBackend &backend)
{
    return backend.handlerExists ? backend.handlerExists(id) : bool(KService::serviceByStorageId(id));
}

void migrateLegacy(KConfigGroup &state)
{
    const auto previous = state.readEntry("handlers", QStringList());
    const auto types = DefaultBrowserManager::handledMimeTypes();
    if (previous.size() != types.size()) { return; }
    for (int i = 0; i < types.size(); ++i) {
        KConfigGroup scheme(&state, types.at(i));
        if (scheme.readEntry("Recorded", false)) { continue; }
        scheme.writeEntry("Recorded", true);
        scheme.writeEntry("EffectiveHandler", previous.at(i));
        scheme.writeEntry("HadDefault", !previous.at(i).isEmpty());
        scheme.writeEntry("Default", previous.at(i).isEmpty() ? QStringList{} : QStringList{previous.at(i)});
        // v0.1 did not preserve Added Associations. Only remove our own entry.
        scheme.writeEntry("Legacy", true);
    }
    state.deleteEntry("handlers");
}

} // namespace

QStringList DefaultBrowserManager::handledMimeTypes()
{
    return {QStringLiteral("x-scheme-handler/http"), QStringLiteral("x-scheme-handler/https")};
}

QStringList DefaultBrowserManager::currentHandlers()
{
    QStringList handlers;
    const auto types = handledMimeTypes();
    for (const QString &type : types) {
        const KService::Ptr service = KApplicationTrader::preferredService(type);
        handlers << (service ? service->storageId() : QString());
    }
    return handlers;
}

bool DefaultBrowserManager::isDefault()
{
    const QStringList handlers = currentHandlers();
    return !handlers.contains(QString()) && std::all_of(handlers.cbegin(), handlers.cend(), [](const QString &id) {
        return id == ownStorageId();
    });
}

QString DefaultBrowserManager::shadowingConfig()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1String("/kde-mimeapps.list");
    if (!QFileInfo::exists(path)) {
        return {};
    }

    KSharedConfig::Ptr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    KConfigGroup defaults(config, QStringLiteral("Default Applications"));
    const auto types = handledMimeTypes();
    for (const QString &type : types) {
        const auto handlers = defaults.readXdgListEntry(type);
        if (!handlers.isEmpty() && handlers.first() != ownStorageId()) {
            return path;
        }
    }
    return {};
}

bool DefaultBrowserManager::claim(QString *error, const AssociationBackend &backend)
{
    if (error) { error->clear(); }
    if (!exists(ownStorageId(), backend)) {
        // KApplicationTrader reads KSycoca; right after installing the desktop
        // file it may not be indexed yet.
        if (error) {
            *error = i18n("%1 is not in the application database yet. "
                          "Install it and run kbuildsycoca6, then try again.",
                          ownStorageId());
        }
        return false;
    }

    const QStringList previous = backend.currentHandlers ? backend.currentHandlers() : currentHandlers();
    auto stateFile = stateConfig();
    stateFile->reparseConfiguration();
    KConfigGroup state(stateFile, QStringLiteral("PreviousHandlers"));
    migrateLegacy(state);
    KConfig config(associationsPath(), KConfig::SimpleConfig);
    KConfigGroup defaults(&config, QStringLiteral("Default Applications"));
    KConfigGroup added(&config, QStringLiteral("Added Associations"));
    const auto types = handledMimeTypes();
    for (int i = 0; i < types.size(); ++i) {
        const auto type = types.at(i);
        KConfigGroup scheme(&state, type);
        const auto localDefaults = defaults.readXdgListEntry(type);
        if (localDefaults.value(0) != ownStorageId()) {
            // A new claim after the user switched away records their latest choice.
            // KSycoca can still report Lob briefly after an external edit, so
            // use the fresh local entry rather than losing that newer choice.
            scheme.deleteGroup();
            scheme.writeEntry("Recorded", true);
            scheme.writeEntry("EffectiveHandler", previous.value(i) == ownStorageId() ? localDefaults.value(0) : previous.value(i));
            scheme.writeEntry("HadDefault", defaults.hasKey(type));
            scheme.writeEntry("Default", defaults.readXdgListEntry(type));
            scheme.writeEntry("HadAdded", added.hasKey(type));
            scheme.writeEntry("Added", added.readXdgListEntry(type));
        }
    }
    if (!stateFile->sync()) {
        if (error) { *error = i18n("Could not save the previous browser associations."); }
        return false;
    }
    for (const auto &type : types) {
        defaults.writeXdgListEntry(type, {ownStorageId()});
        auto associations = added.readXdgListEntry(type);
        associations.removeAll(ownStorageId());
        associations.prepend(ownStorageId());
        added.writeXdgListEntry(type, associations);
    }
    if (!config.sync()) {
        if (error) { *error = i18n("Could not write the browser associations; the previous handlers are retained."); }
        return false;
    }

    const QString shadow = shadowingConfig();
    if (!shadow.isEmpty() && error) {
        *error = i18n("Registered, but %1 also sets a default for http/https and takes precedence. "
                      "Lob will not receive links until that entry is removed.",
                      shadow);
    }

    return true;
}

bool DefaultBrowserManager::restore(QString *error, const AssociationBackend &backend)
{
    if (error) { error->clear(); }
    auto stateFile = stateConfig();
    stateFile->reparseConfiguration();
    KConfigGroup state(stateFile, QStringLiteral("PreviousHandlers"));
    migrateLegacy(state);
    KConfig config(associationsPath(), KConfig::SimpleConfig);
    KConfigGroup defaults(&config, QStringLiteral("Default Applications"));
    KConfigGroup added(&config, QStringLiteral("Added Associations"));
    const auto types = handledMimeTypes();
    bool ok = true;
    bool any = false;
    QStringList restored;
    for (const auto &type : types) {
        KConfigGroup scheme(&state, type);
        if (!scheme.readEntry("Recorded", false)) { continue; }
        any = true;
        if (defaults.readXdgListEntry(type).value(0) != ownStorageId()) {
            restored << type; // someone chose another browser; respect it
            continue;
        }
        const auto previous = scheme.readEntry("Default", QStringList());
        if (!previous.isEmpty() && !std::any_of(previous.cbegin(), previous.cend(), [&](const QString &id) { return exists(id, backend); })) {
            ok = false;
            continue;
        }
        if (scheme.readEntry("HadDefault", false)) { defaults.writeXdgListEntry(type, previous); }
        else { defaults.deleteEntry(type); }

        auto associations = added.readXdgListEntry(type);
        associations.removeAll(ownStorageId());
        // Retain associations other programs have added since the claim.
        for (const auto &id : scheme.readEntry("Added", QStringList())) {
            if (!associations.contains(id)) { associations.append(id); }
        }
        if (associations.isEmpty() && !scheme.readEntry("HadAdded", false)) { added.deleteEntry(type); }
        else { added.writeXdgListEntry(type, associations); }
        restored << type;
    }
    if (!config.sync()) {
        if (error) { *error = i18n("Could not restore the browser associations; recovery information is retained."); }
        return false;
    }
    for (const auto &type : restored) { KConfigGroup(&state, type).deleteGroup(); }
    if (!stateFile->sync()) { ok = false; }
    if (error && (!any || !ok)) {
        *error = any ? i18n("Some browser associations could not be restored. Their recovery information is retained.")
                     : i18n("No previous handler was recorded; set your browser from System Settings.");
    }
    return any && ok;
}

QString DefaultBrowserManager::previousHandler(const QString &schemeName)
{
    auto file = stateConfig();
    file->reparseConfiguration();
    KConfigGroup state(file, QStringLiteral("PreviousHandlers"));
    const QString type = QStringLiteral("x-scheme-handler/") + schemeName;
    KConfigGroup scheme(&state, type);
    if (scheme.readEntry("Recorded", false)) { return scheme.readEntry("EffectiveHandler", QString()); }
    return state.readEntry("handlers", QStringList()).value(handledMimeTypes().indexOf(type));
}

} // namespace Lob
