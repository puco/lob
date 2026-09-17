#include "SettingsController.h"

#include "RuleModel.h"
#include "core/RuleStore.h"
#include "core/TargetRegistry.h"

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

namespace Lob
{

SettingsController::SettingsController(RuleStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_rules(new RuleModel(store, RuleModel::Written, this))
    , m_memories(new RuleModel(store, RuleModel::Remembered, this))
{
    connect(m_store, &RuleStore::changed, this, [this] {
        // A successful write clears whatever the last refusal was: the state
        // on screen is now the state on disk, so an error still sitting beside
        // a field would be describing something that no longer exists.
        setError(QString());
        Q_EMIT settingsChanged();
    });
    connect(m_store, &RuleStore::errorOccurred, this, [this](const QString &error) { setError(error); });
}

QObject *SettingsController::rulesModel() const { return m_rules; }
QObject *SettingsController::memoriesModel() const { return m_memories; }

int SettingsController::holdMs() const { return m_store->holdMs(); }
bool SettingsController::stripTracking() const { return m_store->stripTracking(); }
QString SettingsController::fallbackTargetId() const { return m_store->fallbackTargetId(); }
QString SettingsController::lastError() const { return m_error; }
QString SettingsController::configPath() const { return RuleStore::filePath(); }

void SettingsController::setHoldMs(int ms)
{
    if (ms != m_store->holdMs()) {
        m_store->setHoldMs(ms);
    }
}

void SettingsController::setStripTracking(bool strip)
{
    if (strip != m_store->stripTracking()) {
        m_store->setStripTracking(strip);
    }
}

void SettingsController::setFallbackTargetId(const QString &id)
{
    if (id != m_store->fallbackTargetId()) {
        m_store->setFallbackTargetId(id);
    }
}

void SettingsController::setOtherHandlerEnabled(const QString &id, bool enabled)
{
    m_store->setOtherHandlerEnabled(id, enabled);
    Q_EMIT targetsChanged();
}

void SettingsController::setError(const QString &error)
{
    if (m_error == error) {
        return;
    }
    m_error = error;
    Q_EMIT errorChanged();
}

void SettingsController::refreshTargets()
{
    m_targets = TargetRegistry::discover(QStringLiteral(LOB_APP_ID) + QStringLiteral(".desktop"));
    Q_EMIT targetsChanged();
}

QVariantList SettingsController::targets() const
{
    QVariantList out;
    // Every discovered target, not the picker's shortened list: a rule can
    // name a profile the picker leaves out as redundant, and --list shows
    // those too.
    for (const Target &target : m_targets) {
        if (target.kind != TargetKind::Browser && !m_store->enabledOtherHandlers().contains(target.id)) {
            continue;
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), target.id},
            {QStringLiteral("label"), target.label},
            {QStringLiteral("profileName"), target.profileName},
            {QStringLiteral("iconName"), target.iconName},
        });
    }
    return out;
}

QVariantList SettingsController::otherHandlers() const
{
    QVariantList out;
    const QStringList enabled = m_store->enabledOtherHandlers();
    for (const Target &target : m_targets) {
        if (target.kind == TargetKind::Browser || !target.profileKey.isEmpty()) {
            continue;
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), target.id},
            {QStringLiteral("label"), target.label},
            {QStringLiteral("iconName"), target.iconName},
            {QStringLiteral("enabled"), enabled.contains(target.id)},
        });
    }
    return out;
}

bool SettingsController::show(QQmlApplicationEngine *engine)
{
    if (m_window) {
        // One window. Asking for settings twice is asking to see the settings,
        // not to get a second copy that can disagree with the first.
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
        return true;
    }

    refreshTargets();
    engine->rootContext()->setContextProperty(QStringLiteral("settings"), this);

    const int before = engine->rootObjects().size();
    engine->loadFromModule(QStringLiteral(LOB_APP_ID), QStringLiteral("Settings"));
    if (engine->rootObjects().size() <= before) {
        return false;
    }

    m_window = qobject_cast<QQuickWindow *>(engine->rootObjects().constLast());
    if (!m_window) {
        return false;
    }

    // Closing the window is the end of a one-shot run, and nothing at all for
    // the daemon -- which is why the signal is reported rather than acted on
    // here.
    connect(m_window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            Q_EMIT closed();
        }
    });

    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    return true;
}

bool SettingsController::isVisible() const
{
    return m_window && m_window->isVisible();
}

} // namespace Lob
