#pragma once

// WidgetHeartbeat — is the companion bar widget (`chaves.omaseed`) running? (docs/20 Q7, #14)
//
// The widget touches `$XDG_RUNTIME_DIR/omaseed/widget.alive` every 30 s. While that file is
// under 90 s old the widget is alive, and under the tray icon mode "auto" the app hides its
// own tray icon: one omaseed mark in the bar, not two. Remove the widget and the file goes
// stale; the icon is back within 90 s.
//
// The directory is watched (the file appearing or going), the file itself too (a touch), and
// a single-shot timer fires when the current file would turn 90 s old, so staleness is noticed
// without polling.

#include <QDateTime>
#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

namespace omaseed::integration {

class WidgetHeartbeat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool widgetAlive READ widgetAlive NOTIFY widgetAliveChanged)

public:
    /// How old the heartbeat file may be and still mean "the widget is running".
    static constexpr int kFreshSeconds = 90;

    /// Watches `directory` (default: `$XDG_RUNTIME_DIR/omaseed`, created 0700 if missing).
    /// Without a runtime directory there is nothing to watch and the widget is never alive.
    explicit WidgetHeartbeat(QObject *parent = nullptr, const QString &directory = QString());

    bool widgetAlive() const { return m_alive; }
    QString filePath() const { return m_filePath; }

    /// Whether a heartbeat file last touched at `mtime` (invalid: there is none) means the
    /// widget is running at `now`.
    static bool isFresh(const QDateTime &mtime, const QDateTime &now);
    /// The tray icon's visibility for a tray icon mode ("auto", "always", "never") and the
    /// heartbeat file's `mtime` at `now`: "always" shows it, "never" hides it, and "auto"
    /// hides it only while the widget is alive. Pure, so the rule is testable without files.
    static bool trayIconVisible(const QString &mode, const QDateTime &mtime, const QDateTime &now);
    /// The same rule once the file has been read: `widgetAlive` is isFresh()'s answer.
    static bool trayIconVisible(const QString &mode, bool widgetAlive);

    /// `$XDG_RUNTIME_DIR/omaseed`, or empty when XDG_RUNTIME_DIR is unset.
    static QString defaultDirectory();

    /// Reads the file again now (also what the watcher and the timer call).
    void refresh();

Q_SIGNALS:
    void widgetAliveChanged(bool alive);

private:
    void rewatch();

    QString m_directory;
    QString m_filePath;
    bool m_alive = false;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_staleTimer = nullptr;
};

} // namespace omaseed::integration
