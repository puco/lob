#pragma once

#include "core/Launcher.h"
#include "core/RedirectUnwrapper.h"
#include "core/Rule.h"
#include "core/Target.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QTimer>
#include <QFileSystemWatcher>

class QQmlApplicationEngine;
class QWindow;

namespace Lob
{

class RuleStore;
class TargetModel;

class PickerController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *targets READ targetsModel CONSTANT)
    Q_PROPERTY(QString url READ url NOTIFY contextChanged)
    Q_PROPERTY(QString displayHost READ displayHost NOTIFY contextChanged)
    Q_PROPERTY(QString wrapperHost READ wrapperHost NOTIFY contextChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY contextChanged)
    Q_PROPERTY(bool holding READ isHolding NOTIFY contextChanged)
    Q_PROPERTY(QString holdTitle READ holdTitle NOTIFY contextChanged)
    Q_PROPERTY(QString holdReason READ holdReason NOTIFY contextChanged)
    Q_PROPERTY(int holdMs READ holdMs NOTIFY contextChanged)
    Q_PROPERTY(bool remembered READ isRemembered NOTIFY contextChanged)
    Q_PROPERTY(QVariantList memoryScopes READ memoryScopes NOTIFY contextChanged)
    Q_PROPERTY(bool launching READ isLaunching NOTIFY contextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY contextChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY selectionChanged)

public:
    enum class Mode {
        Idle,
        Picker, ///< asking which target
        Hold,   ///< a rule already decided; counting down so it can be overridden
        Launching,
    };
    Q_ENUM(Mode)

    /// With KeyboardInteractivityExclusive a picker that fails to hide is a
    /// session-level keyboard trap, so it always gets taken down eventually.
    static constexpr int kWatchdogMs = 60'000;

    explicit PickerController(RuleStore *store, QObject *parent = nullptr, Launcher *launcher = nullptr,
                              int watchdogMs = kWatchdogMs);

    QAbstractItemModel *targetsModel() const;
    QString url() const;
    QString displayHost() const;

    /// Host of the redirector this link arrived through, empty if there was
    /// none. The picker says so, because a destination that is not the URL
    /// that was clicked is otherwise a surprise.
    QString wrapperHost() const;
    Mode mode() const;
    bool isHolding() const;
    QString holdTitle() const;
    QString holdReason() const;
    int holdMs() const;
    bool isRemembered() const;

    /// The scopes "remember this" can be asked for, for the URL on screen, in
    /// the order the picker cycles them. Each entry carries the scope, the
    /// literal pattern it would write, and the label naming it -- the pattern
    /// is shown rather than described, because a memory that turns out to
    /// cover more than expected is the failure this feature has to avoid.
    /// Scopes with nothing to say about this URL are left out, so a URL with
    /// no path never offers to remember a path.
    QVariantList memoryScopes() const;
    bool isLaunching() const { return m_mode == Mode::Launching; }
    QString errorMessage() const { return m_error; }

    /// Asks the system what is installed, then applies the filter below.
    /// F5 in the picker, a KSycoca change, and a profile store changing on
    /// disk all land here.
    Q_INVOKABLE void refreshTargets();

    /// Re-applies the enabledOtherHandlers filter to what discovery last
    /// found. A configuration change cannot install or remove a browser, so
    /// it has no business asking the system about them again.
    void refreshEnabledHandlers();

    /// Replaces what is routable, filter already applied. refreshTargets() is
    /// where these normally come from; tests use it to run without a KSycoca
    /// database, which is why it does not filter: a hand-built Target defaults
    /// to OtherHandler, and filtering here would drop every one of them.
    void setTargets(const QList<Target> &targets);
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    bool ensureWindow(QQmlApplicationEngine *engine);
    bool hasTargets() const;

    /// Resolves against everything discovered, including targets the list
    /// leaves out as duplicates. @p remembered reads @p id as a memory from
    /// before profile ids existed.
    Target targetById(const QString &id, bool remembered = false) const;

    /// Asks which target should open @p link.
    void showPicker(const Link &link, const QString &activationToken);

    /// Shows the countdown for an already-made decision, so it can be caught.
    void showHold(const Link &link, const QString &activationToken, const Decision &decision);

    bool launchFallback(const Link &link, const QString &activationToken, const QString &targetId);

    /// @p scope is a MemoryScope; MemoryScope::None chooses without remembering.
    Q_INVOKABLE void choose(int index, bool privateWindow, int scope);
    Q_INVOKABLE void copyUrl();
    Q_INVOKABLE void cancel();

    /// The countdown ran out: carry out the decision that was being held.
    Q_INVOKABLE void holdCompleted();

    /// Esc, Space, or a held modifier: abandon the decision and ask instead.
    Q_INVOKABLE void interruptHold();

Q_SIGNALS:
    void contextChanged();
    void finished();
    void errorOccurred(const QString &message);
    void selectionChanged();
    void clipboardCopied();

private:
    void present(const QString &activationToken);
    void hidePicker();
    void checkHeldModifiers();
    void runDecision();
    void begin(const Link &link, const QString &activationToken);
    void startLaunch(const Target &target, bool privateWindow, MemoryScope scope);
    void finish();
    void watchdogExpired();
    void watchTargetSources(const QList<Target> &targets);
    void applyTargets(const QList<Target> &targets);
    bool decisionPredatesProfileIds() const;

    RuleStore *m_store;
    TargetModel *m_model;
    Launcher *m_launcher;
    QWindow *m_window = nullptr;

    QList<Target> m_discovered; // what the system has, before the user's filter
    QList<Target> m_targets; // everything routable; the model lists a subset
    Link m_link;
    Mode m_mode = Mode::Idle;
    Decision m_decision;
    QElapsedTimer m_showTimer;
    QTimer m_holdTimer;
    QTimer m_watchdog;
    quint64 m_requestId = 0;
    QString m_activationToken;
    QString m_error;
    QSharedPointer<LaunchOperation> m_operation;
    int m_currentIndex = 0;
    bool m_launchStalled = false;
    bool m_usingLayerShell = false;
    QFileSystemWatcher m_targetWatcher;
    QTimer m_refreshTimer;
};

} // namespace Lob
