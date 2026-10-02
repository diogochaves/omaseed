#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QLocalServer;

namespace omaseed::integration {

class SingleInstanceDBusAdaptor;

/// One running omaseed per user session. The first instance to start owns the D-Bus name
/// `io.github.diogochaves.omaseed` and implements `org.freedesktop.Application` on
/// `/io/github/diogochaves/omaseed` (Activate, Open, ActivateAction), matching
/// `DBusActivatable=true` in the .desktop file. A later launch that finds the name already
/// owned calls `Open` (when it carries URIs) or `Activate` on the owner, then reports
/// `isPrimary() == false` so `main.cpp` can exit immediately without showing a window.
///
/// When the session bus is unreachable (headless, or a broken environment), the same
/// primary/secondary handshake runs over a `QLocalServer` named after the application id
/// under `XDG_RUNTIME_DIR` (the temp dir only when that is unset), user-only, with a lock
/// file beside it so two simultaneous starts cannot both become primary.
///
/// A secondary checks the primary's answer. When the call fails (the primary is quitting,
/// hung, or the name's owner is not omaseed) it tries once to take the name itself; when that
/// fails too it says so on stderr rather than dropping the URIs silently.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    /// Tries to become the single primary instance for this user session.
    ///
    /// Returns true when this process is now primary and startup should continue; `uris`
    /// (the process's own positional arguments, if any) are then delivered back through
    /// `openRequested()` once the event loop starts, so callers have exactly one path for
    /// "URIs to open" whether they came from this process or a later one.
    ///
    /// Returns false when another instance is already primary: `uris` (or, if empty, a
    /// plain activation) have already been forwarded to it and the caller should exit
    /// immediately, without creating any windows.
    bool start(const QStringList &uris);

    bool isPrimary() const { return m_isPrimary; }

    /// The D-Bus connection that owns the well-known name, once start() made this process
    /// primary on the bus; a disconnected connection otherwise (secondary, or the local-socket
    /// fallback). Anything else the app exports for callers addressing the name (the theme's
    /// `Reload`) has to be registered on this connection: the bus routes a call sent to a
    /// well-known name to the connection that owns it, not to the process's default one.
    QDBusConnection connection() const;

    /// The launch arguments made independent of this process's working directory, so a
    /// primary with a different cwd opens the same file: an argument that is not already a
    /// magnet, http(s) or file URI and names an existing file (relative or absolute) becomes a
    /// `file://` URI of its absolute path. Everything else passes through unchanged.
    static QStringList absoluteUris(const QStringList &arguments, const QString &workingDirectory);

    /// The `activation-token` hint from the most recent Activate/Open call's platform_data,
    /// if the caller supplied one (the freedesktop.Application convention for XDG activation
    /// tokens), consumed on read. Empty when none was supplied.
    QString takeLastActivationToken();

signals:
    /// The user asked for the running instance to come to the front, with no URIs attached
    /// (a second `omaseed` launch with no arguments, or a taskbar/dock activation).
    void activateRequested();
    /// One or more URIs (magnet links or .torrent paths) should be opened, from a second
    /// launch's arguments or, once, from this process's own.
    void openRequested(const QStringList &uris);
    /// `org.freedesktop.Application.ActivateAction` with a known action, from the bar widget:
    /// "settings" (the popup's gear) raises the window on omaseed's settings, "retry" polls an
    /// unreachable server now, with no window.
    void actionRequested(const QString &name);

private:
    friend class SingleInstanceDBusAdaptor;

    bool startOnSessionBus(const QString &busAddress, const QStringList &uris);
    bool startOnLocalSocket(const QStringList &uris);
    bool becomePrimaryOnBus(QDBusConnection &connection,
                            const QString &connectionName,
                            const QStringList &uris);
    void deliverOwnUris(const QStringList &uris);
    void acceptLocalConnection();

    void handleActivate(const QVariantMap &platformData);
    void handleOpen(const QStringList &uris, const QVariantMap &platformData);
    void handleActivateAction(const QString &name, const QVariantMap &platformData);

    QString localSocketName() const;
    static QVariantMap outgoingPlatformData();
    static QString activationTokenFrom(const QVariantMap &platformData);

    bool m_isPrimary = false;
    QString m_connectionName;
    QString m_lastActivationToken;
    QLocalServer *m_localServer = nullptr;
};

} // namespace omaseed::integration
