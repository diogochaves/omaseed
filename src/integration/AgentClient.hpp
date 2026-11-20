#pragma once

// AgentClient — how a word reaches the running omaseed (DECISIONS C45 V5): one call of
// `io.github.diogochaves.omaseed.Agent` on the app's name and object, on the session bus.
//
// 1. The address is DBUS_SESSION_BUS_ADDRESS, else `unix:path=$XDG_RUNTIME_DIR/bus` when that
//    socket exists; with neither, or no connection, the answer is `no-bus`.
// 2. It connects under a private name, `omaseed-word-<pid>`, and disconnects after.
// 3. The name not owned is `not-running`; an owner without the Agent interface (or this
//    method) is `too-old`, its version read from the status file's `app.version` (the
//    widget's published contract) only to say so; no answer in time is `no-reply`.
//
// It never starts omaseed and never reads state.json, servers.json or moves/ (C37 U4).

#include "core/AgentWords.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace omaseed::integration::AgentClient {

/// The interface the reads are methods of.
QString interfaceName();

struct Reply
{
    QByteArray answer; ///< the app's JSON, as it sent it (an answer, or its own failure)
    QString code;      ///< "" when the app answered; else no-bus, not-running, too-old, no-reply
    QString message;   ///< the words for `code`
};

/// The session bus's address as above; "" when there is none.
QString busAddress();

/// Calls `method` (Status, Errors, Log, Moves) with `options` as its JSON string.
Reply call(const QString &method,
           const QJsonObject &options,
           int timeoutMs = core::AgentWords::kReplyTimeoutMs);

/// The failures' words (C45 V13); `running` is the status file's version, or "".
QString notRunningWords();
QString noBusWords();
QString noReplyWords(int timeoutMs);
QString tooOldWords(const QString &running, const QString &mine);

} // namespace omaseed::integration::AgentClient
