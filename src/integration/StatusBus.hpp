#pragma once

// StatusBus — `io.github.diogochaves.omaseed.Status`, the companion bar widget's way to act
// (docs/20 §5, #13). The widget reads the app through the status file (StatusWriter) and acts
// through these methods, called fire-and-forget with `busctl --user call`; it learns what
// happened from the next status write. Methods only: no properties, no signals.
//
//   ToggleAltSpeed() → b                         the new state the server was asked for
//   SetAltSpeed(b on) → b                        the same, for a caller that knows what it wants
//   DescribeSources(as sources) → s              JSON, one entry per source (see StatusHandler)
//   AddSources(as sources, s category, b stopped) → s   JSON: what was added, what was not
//   OpenAddSheet(as sources)                     the app's own add sheet with these sources
//   ShowTorrent(s hash)                          raise the window on that torrent
//   OpenFolder(s hash)                           show its content in the file manager
//   SwitchServer(s id) → b                       the app moves onto that server (docs/33 D24)
//   Invoke(s id) → b                             a row of the shared menu (BarMenu): add,
//                                                alt-speed, stop-all, start-all, settings,
//                                                quit, server:<id>
//
// It lives on the app's object path, `/io/github/diogochaves/omaseed`, beside
// `org.freedesktop.Application`: the adaptor is added to the object SingleInstance exported
// there. Nothing here knows the app: a StatusHandler (app::StatusService) does the work.

#include <QDBusConnection>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace omaseed::integration {

/// What the Status interface asks of the app. The two describe/add calls answer later (they
/// read files and talk to the server), through `reply`, exactly once.
class StatusHandler
{
public:
    using Reply = std::function<void(const QString &json)>;

    virtual ~StatusHandler() = default;

    /// Asks the server to flip alternative speed limits; returns the state asked for.
    virtual bool toggleAltSpeed() = 0;
    /// Asks the server for `on`; returns `on`, or the current state when there is no server.
    virtual bool setAltSpeed(bool on) = 0;
    /// A JSON array, one object per source, in order:
    ///   {"source", "kind": "magnet"|"url"|"file"|"none", "valid", "error", "name",
    ///    "size" (bytes, 0 unknown), "hash", "duplicate", "duplicateOn"}
    virtual void describeSources(const QStringList &sources, Reply reply) = 0;
    /// Adds every valid source that is not already on the server. A JSON object:
    ///   {"added": [{"source", "name", "hash"}], "duplicates": [{"source", "name", "hash"}],
    ///    "failed": [{"source", "error"}]}
    virtual void
    addSources(const QStringList &sources, const QString &category, bool stopped, Reply reply) = 0;
    virtual void openAddSheet(const QStringList &sources) = 0;
    virtual void showTorrent(const QString &hash) = 0;
    virtual void openFolder(const QString &hash) = 0;
    /// docs/33 §11 change 6: moves the app onto server `id`; true when it moved or was there.
    virtual bool switchServer(const QString &id) = 0;
    /// A row of the shared menu by its id (BarMenu::invoke); false for an unknown or disabled
    /// one.
    virtual bool invoke(const QString &id) = 0;
};

namespace StatusBus {

/// "io.github.diogochaves.omaseed.Status".
QString interfaceName();

/// Adds the Status interface to `exported`, the object registered with ExportAdaptors at the
/// app's object path on `connection` (SingleInstance), answering through `handler`, which
/// must outlive `exported`'s event processing. False when `connection` is not connected.
bool exportOn(QObject *exported, const QDBusConnection &connection, StatusHandler *handler);

} // namespace StatusBus

} // namespace omaseed::integration
