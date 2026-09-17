#include "TrayController.h"

#include <KLocalizedString>
#include <KStatusNotifierItem>

#include <QAction>
#include <QCoreApplication>
#include <QMenu>

namespace Lob
{

TrayController::TrayController(QObject *parent)
    : QObject(parent)
    , m_item(new KStatusNotifierItem(QStringLiteral("lob"), this))
{
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_item->setStatus(KStatusNotifierItem::Passive);
    m_item->setIconByName(QStringLiteral(LOB_APP_ID));
    m_item->setTitle(i18n("Lob"));

    // Left-clicking a link router should do nothing surprising, so the default
    // activation is suppressed in favour of the explicit menu.
    m_item->setStandardActionsEnabled(false);

    auto *menu = new QMenu();

    auto *clipboard = menu->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), i18n("Open Link in Clipboard…"));
    connect(clipboard, &QAction::triggered, this, &TrayController::routeClipboardRequested);

    auto *configure = menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure Lob…"));
    connect(configure, &QAction::triggered, this, &TrayController::settingsRequested);

    menu->addSeparator();

    auto *pause = menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-pause")), i18n("Pause Routing"));
    pause->setCheckable(true);
    connect(pause, &QAction::toggled, this, [this](bool checked) {
        m_paused = checked;
        updateStatus();
        Q_EMIT pausedChanged(checked);
    });

    menu->addSeparator();

    auto *quit = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18n("Quit"));
    connect(quit, &QAction::triggered, this, &TrayController::quitRequested);

    m_item->setContextMenu(menu);
    updateStatus();
}

bool TrayController::isPaused() const
{
    return m_paused;
}

void TrayController::updateStatus()
{
    if (m_paused) {
        m_item->setIconByName(QStringLiteral("media-playback-pause"));
        m_item->setToolTip(QStringLiteral(LOB_APP_ID), i18n("Lob"), i18n("Routing paused"));
    } else {
        m_item->setIconByName(QStringLiteral(LOB_APP_ID));
        m_item->setToolTip(QStringLiteral(LOB_APP_ID), i18n("Lob"), i18n("Routing links"));
    }
}

} // namespace Lob
