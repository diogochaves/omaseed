#pragma once

// AgentBus — `io.github.diogochaves.omaseed.Agent`, the words' way in (DECISIONS C45 V3): one
// method per read, each taking the options as JSON and answering one JSON object (C45 V4),
// called by the CLI (AgentClient) or by hand with `busctl --user call`.
//
//   Status(s options) → s      the server, what it does, how many need you (C45 V8)
//   Errors(s options) → s      every torrent that needs you, why, and its fix (C45 V9)
//   Log(s options) → s         qBittorrent's log, warnings and worse by default (C45 V10)
//   Moves(s options) → s       the move running now and the last that landed (C45 V11)
//
// Do comes with the actions.
// It lives on the app's object path beside org.freedesktop.Application and Status (StatusBus),
// which it leaves as they are. Every method answers later (a delayed reply), as StatusBus's
// slow methods do. Nothing here knows the app: an AgentHandler (app::AgentService) answers.

#include <QDBusConnection>
#include <QString>

#include <functional>

class QObject;

namespace omaseed::integration {

/// What the Agent interface asks of the app. Each read answers through `reply`, exactly once,
/// with one JSON object (an answer, or a failure with "error").
class AgentHandler
{
public:
    using Reply = std::function<void(const QString &json)>;

    virtual ~AgentHandler() = default;

    /// C45 V8. `options` is the JSON the caller sent ("{}" or "" for none).
    virtual void status(const QString &options, Reply reply) = 0;
    /// C45 V9: answers once the log has been read (qBittorrent), at most a few seconds on.
    virtual void errors(const QString &options, Reply reply) = 0;
    /// C45 V10: answers once qBittorrent's log has been read.
    virtual void log(const QString &options, Reply reply) = 0;
    /// C45 V11: any server's, from what omaseed keeps for it.
    virtual void moves(const QString &options, Reply reply) = 0;
};

namespace AgentBus {

/// "io.github.diogochaves.omaseed.Agent" (AgentClient::interfaceName's).
QString interfaceName();

/// Adds the Agent interface to `exported`, the object registered with ExportAdaptors at the
/// app's object path on `connection` (SingleInstance), answering through `handler`, which
/// must outlive `exported`'s event processing. False when `connection` is not connected.
bool exportOn(QObject *exported, const QDBusConnection &connection, AgentHandler *handler);

} // namespace AgentBus

} // namespace omaseed::integration
