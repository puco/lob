#pragma once

#include "core/Launcher.h"
#include "core/Rule.h"
#include "core/Target.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QObject>
#include <QUrl>

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

public:
    enum class Mode {
        Picker, ///< asking which target
        Hold,   ///< a rule already decided; counting down so it can be overridden
    };
    Q_ENUM(Mode)

    explicit PickerController(RuleStore *store, QObject *parent = nullptr);

    QAbstractItemModel *targetsModel() const;
    QString url() const;
    QString displayHost() const;
    Mode mode() const;
    bool isHolding() const;
    QString holdTitle() const;
    QString holdReason() const;
    int holdMs() const;
    bool isRemembered() const;

    void refreshTargets();
    bool ensureWindow(QQmlApplicationEngine *engine);
    bool hasTargets() const;

    Target targetById(const QString &id) const;

    /// Asks which target should open @p url.
    void showPicker(const QUrl &url, const QString &activationToken);

    /// Shows the countdown for an already-made decision, so it can be caught.
    void showHold(const QUrl &url, const QString &activationToken, const Decision &decision);

    bool launchFallback(const QUrl &url);

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

private:
    void present(const QString &activationToken);
    void hidePicker();
    void checkHeldModifiers();
    void runDecision();

    RuleStore *m_store;
    TargetModel *m_model;
    Launcher *m_launcher;
    QWindow *m_window = nullptr;

    QUrl m_url;
    Mode m_mode = Mode::Picker;
    Decision m_decision;
    QElapsedTimer m_showTimer;
};

} // namespace Lob
