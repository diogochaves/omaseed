#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

QT_BEGIN_NAMESPACE
class QMenu;
class QAction;
class QPainter;
class QDBusServiceWatcher;
QT_END_NAMESPACE

namespace omaseed::integration {

class ThemeReader;

/// The bar-resident tray icon: `QSystemTrayIcon`, which Qt speaks as a StatusNotifierItem
/// under Wayland so the Omarchy shell draws both the icon and (through DBusMenu) the menu.
/// Five discrete icon states (spec v2 §6 Fig. 17), a menu matching Fig. 18 exactly, and a
/// tooltip built from live numbers. Icons are SVG templates in
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

    /// The menu's two disabled info lines: "↓ 4.21 MB/s · ↑ 812 KB/s" and
    /// "6 downloading · 118 seeding · 1.8 TB free".
    void setStats(const QString &downloadSpeedText,
                  const QString &uploadSpeedText,
                  int downloading,
                  int seeding,
                  const QString &freeText);

    /// External truth for the "alternative speed limits" checkbox. Never emits
    /// altSpeedToggled(): that signal is only for the user's own click or middle-click.
    bool altSpeedChecked() const;
    void setAltSpeedChecked(bool checked);

    /// Test hook: push palette entries directly, bypassing ThemeReader entirely. Recognised
    /// keys: accent, foreground, dim, red, yellow, background. Unknown keys are ignored;
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
    /// Menu: "show omaseed". Always shows and raises, never toggles.
    void showRequested();
    /// Menu: "add torrent…".
    void addRequested();
    /// Menu checkbox toggled by the user, or a middle click on the icon (which requests the
    /// opposite of the current checked state). The caller applies the new state and reports
    /// it back with setAltSpeedChecked().
    void altSpeedToggled(bool checked);
    /// Menu: "stop all".
    void stopAllRequested();
    /// Menu: "start all".
    void startAllRequested();
    /// Menu: "settings…".
    void settingsRequested();
    /// Menu: "quit". A real quit, not a hide.
    void quitRequested();
    /// Left click (QSystemTrayIcon::Trigger): toggle the main window's visibility.
    void toggleWindowRequested();

private:
    void buildMenu();
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
    QAction *m_altSpeedAction = nullptr;
    QAction *m_stopAllAction = nullptr;
    QAction *m_startAllAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;

    State m_state = Disconnected;
    int m_downloadingCount = 0;
    QString m_tooltip;
    QHash<QString, QColor> m_palette;
    bool m_loggedUnavailable = false;
    bool m_iconWanted = true;
    QDBusServiceWatcher *m_hostWatcher = nullptr;

    void waitForHost();
};

} // namespace omaseed::integration
