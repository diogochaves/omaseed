#pragma once

#include "integration/BarMenu.hpp"

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QSystemTrayIcon>

QT_BEGIN_NAMESPACE
class QMenu;
class QAction;
class QActionGroup;
class QPainter;
class QDBusServiceWatcher;
QT_END_NAMESPACE

namespace omaseed::integration {

class ThemeReader;

/// The bar-resident tray icon: `QSystemTrayIcon`, which Qt speaks as a StatusNotifierItem
/// under Wayland so the Omarchy shell draws both the icon and (through DBusMenu) the menu.
/// Five discrete icon states (spec v2 §6 Fig. 17), a menu, and a tooltip built from live
/// numbers. The menu is "show omaseed" and the two figure rows (Fig. 18), then the shared
/// BarMenu's rows, the same ones the bar widget's ⋯ shows (docs/33 §11, D10, D24; DECISIONS
/// C13): the servers with two or more, alternative limits, stop all, start all, add, settings,
/// quit. Icons are SVG templates in
/// `src/integration/resources/tray/` with placeholder fills (`#ACCENT`, `#FG`, `#RED`,
/// `#DIM`) substituted for the live theme palette and rendered to a multi-size `QIcon`; the
/// downloading count and the alt-speed mark are painted on top.
///
/// If a `ThemeReader` is given, the palette tracks it live (`ThemeReader::changed`). Tests
/// (and anything else that wants the icon without a reader) can push a palette directly with
/// `setPalette()`.
class Tray : public QObject
{
    Q_OBJECT

public:
    enum State { Disconnected, Idle, Downloading, AltSpeed, Error };
    Q_ENUM(State)

    explicit Tray(ThemeReader *reader = nullptr, QObject *parent = nullptr);
    ~Tray() override;

    /// False on a desktop with no StatusNotifierHost. The tray stays constructed (so the
    /// menu, tooltip and icon logic are all still testable) but is never shown.
    bool isAvailable() const;

    State state() const { return m_state; }
    void setState(State state);

    /// The count badge painted on the Downloading icon (Fig. 17's "6"). Capped visually at
    /// "9+"; zero paints no badge.
    int downloadingCount() const { return m_downloadingCount; }
    void setDownloadingCount(int count);

    /// Builds and applies "home · ↓ 4.21 MB/s ↑ 812 KB/s · 6 downloading · 118 seeding".
    void setTooltip(const QString &server,
                    const QString &downloadSpeedText,
                    const QString &uploadSpeedText,
                    int downloading,
                    int seeding);
    QString tooltip() const { return m_tooltip; }
    /// Board 42 J6 (DECISIONS C15 M20): a running move's line under the tooltip ("Moving
    /// games: 52 %, about 50 min left"); "" for none.
    void setJobLine(const QString &line);

    /// The menu's two disabled info lines: "↓ 4.21 MB/s · ↑ 812 KB/s" and
    /// "6 downloading · 118 seeding · 1.8 TB free".
    void setStats(const QString &downloadSpeedText,
                  const QString &uploadSpeedText,
                  int downloading,
                  int seeding,
                  const QString &freeText);

    /// The shared menu the tray shows under its own rows (App's). Until one is set the tray
    /// shows a menu of its own with the same rows and nobody answering them. Not owned.
    void setMenuModel(BarMenu *model);
    BarMenu *menuModel() const { return m_model; }

    /// The "alternative speed limits" row's check, as the menu model says.
    bool altSpeedChecked() const;

    /// docs/31 T1: which way the alternative set goes ("burst", "turtle", "alt"). Under burst
    /// the AltSpeed icon's mark is a green disc with ⏶; otherwise today's yellow ⏷.
    QString altKind() const { return m_altKind; }
    void setAltKind(const QString &kind);

    /// Test hook: push palette entries directly, bypassing ThemeReader entirely. Recognised
    /// keys: accent, foreground, dim, red, yellow, green, background. Unknown keys are ignored;
    /// missing keys keep their previous value.
    void setPalette(const QHash<QString, QColor> &palette);

    /// The icon for a state at the current palette, built fresh each call. Exposed mainly
    /// for tests; setState() applies the same icon to the tray.
    QIcon iconForState(State state) const;

    QSystemTrayIcon *systemTrayIcon() const { return m_trayIcon; }

    /// Shows or hides the icon (Settings' tray icon mode). Shown while no StatusNotifierHost
    /// exists yet, it appears once one registers, not never.
    void setIconVisible(bool visible);
    bool iconWanted() const { return m_iconWanted; }
    QMenu *menu() const { return m_menu; }

Q_SIGNALS:
    /// Menu: "show omaseed". Always shows and raises, never toggles. Every other row goes to
    /// the menu model (BarMenu::invoke), and a middle click is its "alt-speed".
    void showRequested();
    /// Left click (QSystemTrayIcon::Trigger): toggle the main window's visibility.
    void toggleWindowRequested();

private:
    void buildMenu();
    /// The model's rows under the tray's own: rebuilt when their ids or kinds change, and
    /// updated in place otherwise (an open menu keeps its rows).
    void syncSharedRows();
    void refreshIcon();
    void applyFromReader();
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    static QString resourceForState(State state);
    static QHash<QString, QColor> defaultPalette();
    void paintCountBadge(QPainter *painter, int size, int count) const;
    void paintAltSpeedMark(QPainter *painter, int size) const;
    void paintErrorMark(QPainter *painter, int size) const;

    ThemeReader *m_reader = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_menu = nullptr;

    QAction *m_showAction = nullptr;
    QAction *m_addAction = nullptr;
    QAction *m_speedsInfoAction = nullptr;
    QAction *m_statsInfoAction = nullptr;
    QPointer<BarMenu> m_model;
    BarMenu *m_ownModel = nullptr;
    /// The shared rows' actions, by row id, in order.
    QList<QPair<QString, QAction *>> m_sharedActions;
    QActionGroup *m_serverGroup = nullptr;

    State m_state = Disconnected;
    int m_downloadingCount = 0;
    QString m_altKind = QStringLiteral("alt");
    QString m_tooltip;
    QString m_tooltipBase; ///< the tooltip without the job's line
    QString m_jobLine;
    QHash<QString, QColor> m_palette;
    bool m_loggedUnavailable = false;
    bool m_iconWanted = true;
    QDBusServiceWatcher *m_hostWatcher = nullptr;

    void waitForHost();
};

} // namespace omaseed::integration
