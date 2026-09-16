#pragma once

#include <QObject>

class KStatusNotifierItem;

namespace Lob
{

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(QObject *parent = nullptr);

    bool isPaused() const;

Q_SIGNALS:
    /// Routing is paused: send links straight to the fallback browser.
    void pausedChanged(bool paused);
    void routeClipboardRequested();
    void quitRequested();

private:
    void updateStatus();

    KStatusNotifierItem *m_item;
    bool m_paused = false;
};

} // namespace Lob
