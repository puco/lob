#pragma once

#include "core/Target.h"

#include <QObject>
#include <QVariantList>

class QQmlApplicationEngine;
class QQuickWindow;

namespace Lob
{

class RuleModel;
class RuleStore;

/**
 * Backs the settings window: the two rule sections, the handful of plain
 * settings, and the list of non-browser handlers that are hidden until named.
 *
 * It holds no copy of anything. Every read comes from the live RuleStore and
 * every write goes back through it, so a file edited outside the window, a
 * choice remembered by the picker in this same process and an undo from the
 * CLI all arrive as one thing: the store changed, and the view follows. That
 * is why there is no conflict model here -- there is only ever one copy to
 * conflict with, and the genuinely concurrent write is caught by the store.
 */
class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *rules READ rulesModel CONSTANT)
    Q_PROPERTY(QObject *memories READ memoriesModel CONSTANT)

    Q_PROPERTY(int holdMs READ holdMs WRITE setHoldMs NOTIFY settingsChanged)
    Q_PROPERTY(bool stripTracking READ stripTracking WRITE setStripTracking NOTIFY settingsChanged)
    Q_PROPERTY(QString fallbackTargetId READ fallbackTargetId WRITE setFallbackTargetId NOTIFY settingsChanged)

    /// Every target a rule can name, for the chooser: {id, label, profileName}.
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)

    /// Applications registered for https that are not browsers, with whether
    /// each is enabled: {id, label, enabled}. Discovered all along and hidden
    /// until named, which nothing in the UI used to say.
    Q_PROPERTY(QVariantList otherHandlers READ otherHandlers NOTIFY targetsChanged)

    /// Whatever the store last refused, for showing beside the edit that
    /// caused it. Empty when the last write succeeded.
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

public:
    explicit SettingsController(RuleStore *store, QObject *parent = nullptr);

    QObject *rulesModel() const;
    QObject *memoriesModel() const;

    int holdMs() const;
    void setHoldMs(int ms);
    bool stripTracking() const;
    void setStripTracking(bool strip);
    QString fallbackTargetId() const;
    void setFallbackTargetId(const QString &id);

    QVariantList targets() const;
    QVariantList otherHandlers() const;
    QString lastError() const;

    Q_INVOKABLE void setOtherHandlerEnabled(const QString &id, bool enabled);

    /// Re-reads what is installed. The window is long-lived compared with the
    /// picker, so a browser installed while it is open has to be reachable
    /// without reopening it.
    Q_INVOKABLE void refreshTargets();

    /// Where rules.json lives, for the window to say so. People who edit it by
    /// hand should not have to guess that the window and the file are the same
    /// thing.
    Q_INVOKABLE QString configPath() const;

    /// Builds the window on first use and shows it. Later calls raise the one
    /// that already exists rather than opening a second.
    bool show(QQmlApplicationEngine *engine);

    /// Whether the window is on screen. A one-shot `lob --settings` has no
    /// link to route and would otherwise exit the moment it had opened it.
    bool isVisible() const;

Q_SIGNALS:
    void closed();
    void settingsChanged();
    void targetsChanged();
    void errorChanged();

private:
    void setError(const QString &error);

    RuleStore *m_store;
    RuleModel *m_rules;
    RuleModel *m_memories;
    QList<Target> m_targets;
    QQuickWindow *m_window = nullptr;
    QString m_error;
};

} // namespace Lob
