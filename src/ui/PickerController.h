#pragma once

#include "core/Launcher.h"
#include "core/Target.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QUrl>

class QQmlApplicationEngine;
class QWindow;

namespace Lob
{

class TargetModel;

class PickerController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *targets READ targetsModel CONSTANT)
    Q_PROPERTY(QString url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString displayHost READ displayHost NOTIFY urlChanged)

public:
    explicit PickerController(QObject *parent = nullptr);

    QAbstractItemModel *targetsModel() const;
    QString url() const;
    QString displayHost() const;

    /// Rescans installed browsers. Cheap enough to call at startup.
    void refreshTargets();

    /// Builds the picker window and keeps it alive. Safe to call repeatedly.
    bool ensureWindow(QQmlApplicationEngine *engine);

    void showFor(const QUrl &url, const QString &activationToken);

    Q_INVOKABLE void choose(int index, bool privateWindow);
    Q_INVOKABLE void copyUrl();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void urlChanged();
    void finished();
    void errorOccurred(const QString &message);

private:
    void hidePicker();

    TargetModel *m_model;
    Launcher *m_launcher;
    QWindow *m_window = nullptr;
    QUrl m_url;
};

} // namespace Lob
