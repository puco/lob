#include "DefaultBrowserManager.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

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
        if (!defaults.readXdgListEntry(type).isEmpty()) {
            return path;
        }
    }
    return {};
}

bool DefaultBrowserManager::claim(QString *error)
{
    const KService::Ptr me = KService::serviceByStorageId(ownStorageId());
    if (!me) {
        // KApplicationTrader reads KSycoca; right after installing the desktop
        // file it may not be indexed yet.
        if (error) {
            *error = QStringLiteral("%1 is not in the application database yet. "
                                    "Install it and run kbuildsycoca6, then try again.")
                         .arg(ownStorageId());
        }
        return false;
    }

    const QStringList previous = currentHandlers();

    // Never record ourselves: claiming twice would otherwise overwrite the only
    // note of what to restore.
    if (!previous.contains(ownStorageId())) {
        KConfigGroup state(stateConfig(), QStringLiteral("PreviousHandlers"));
        state.writeEntry("handlers", previous);
        state.sync();
    }

    const auto types = handledMimeTypes();
    for (const QString &type : types) {
        KApplicationTrader::setPreferredService(type, me);
    }

    const QString shadow = shadowingConfig();
    if (!shadow.isEmpty() && error) {
        *error = QStringLiteral("Registered, but %1 also sets a default for http/https and takes precedence. "
                                "Lob will not receive links until that entry is removed.")
                     .arg(shadow);
    }

    // Deliberately not re-checking with isDefault() here. That reads through
    // KSycoca, which still holds the pre-write associations for a moment, so
    // an immediate check reports failure for a write that in fact succeeded.
    // The writes above either threw or did not; `lob --status` is the check.
    return true;
}

bool DefaultBrowserManager::restore(QString *error)
{
    KConfigGroup state(stateConfig(), QStringLiteral("PreviousHandlers"));
    const QStringList previous = state.readEntry("handlers", QStringList());

    const auto types = handledMimeTypes();
    if (previous.size() != types.size()) {
        if (error) {
            *error = QStringLiteral("No previous handler was recorded; set your browser from System Settings.");
        }
        return false;
    }

    bool ok = true;
    for (int i = 0; i < types.size(); ++i) {
        if (previous.at(i).isEmpty()) {
            continue;
        }
        const KService::Ptr service = KService::serviceByStorageId(previous.at(i));
        if (!service) {
            qCWarning(LOG_DEFAULT) << "previous handler" << previous.at(i) << "is gone";
            ok = false;
            continue;
        }
        KApplicationTrader::setPreferredService(types.at(i), service);
    }

    state.deleteEntry("handlers");
    state.sync();
    return ok;
}

} // namespace Lob
