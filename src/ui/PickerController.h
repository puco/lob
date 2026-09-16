#pragma once

#include "core/Launcher.h"
#include "core/Rule.h"
#include "core/Target.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QObject>
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
    Q_PROPERTY(Mode mode READ mode NOTIFY contextChanged)
    Q_PROPERTY(bool holding READ isHolding NOTIFY contextChanged)
    Q_PROPERTY(QString holdTitle READ holdTitle NOTIFY contextChanged)
    Q_PROPERTY(QString holdReason READ holdReason NOTIFY contextChanged)
    Q_PROPERTY(int holdMs READ holdMs NOTIFY contextChanged)
    Q_PROPERTY(bool remembered READ isRemembered NOTIFY contextChanged)
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
    Mode mode() const;
    bool isHolding() const;
    QString holdTitle() const;
    QString holdReason() const;
    int holdMs() const;
    bool isRemembered() const;
    bool isLaunching() const { return m_mode == Mode::Launching; }
    QString errorMessage() const { return m_error; }

    Q_INVOKABLE void refreshTargets();

    /// Replaces what is routable and listed. refreshTargets() is where these
    /// normally come from; tests use it to run without a KSycoca database.
    void setTargets(const QList<Target> &targets);
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    bool ensureWindow(QQmlApplicationEngine *engine);
    bool hasTargets() const;

    /// Resolves against everything discovered, including targets the list
    /// leaves out as duplicates. @p remembered reads @p id as a memory from
    /// before profile ids existed.
    Target targetById(const QString &id, bool remembered = false) const;

    /// Asks which target should open @p url.
    void showPicker(const QUrl &url, const QString &activationToken);

    /// Shows the countdown for an already-made decision, so it can be caught.
    void showHold(const QUrl &url, const QString &activationToken, const Decision &decision);

    bool launchFallback(const QUrl &url, const QString &activationToken, const QString &targetId);

    Q_INVOKABLE void choose(int index, bool privateWindow, bool remember);
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
    void begin(const QUrl &url, const QString &activationToken);
    void startLaunch(const Target &target, bool privateWindow, bool remember);
    void finish();
    void watchdogExpired();
    void watchTargetSources(const QList<Target> &targets);
    bool decisionPredatesProfileIds() const;

    RuleStore *m_store;
    TargetModel *m_model;
    Launcher *m_launcher;
    QWindow *m_window = nullptr;

    QList<Target> m_targets; // everything discovered; the model lists a subset
    QUrl m_url;
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
    QFileSystemWatcher m_targetWatcher;
    QTimer m_refreshTimer;
};

} // namespace Lob
