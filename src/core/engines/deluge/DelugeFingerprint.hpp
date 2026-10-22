#pragma once

// DelugeFingerprint — does deluge-web answer at this address? (C25 K5, K7, K10)
//
// deluge-web speaks JSON-RPC v1 on one endpoint, /json. `auth.check_session` is the one
// method it answers before a sign-in: `{"result": false, "error": null, "id": 1}` (engine
// matrix §1.1). With the password (K7: one password, no user name) `auth.login` sets its
// `_session_id` cookie; then `web.connected` says whether it has a daemon, `daemon.info` names
// the daemon's version ("2.2.0"), and `web.get_hosts` lists the daemons it knows (K10's
// daemon step).

#include "core/engines/EngineFingerprint.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>

namespace omaseed::core::DelugeFingerprint {

/// deluge-web's /json behind `address` (a proxy's path kept; …/json taken as it is).
QUrl jsonUrl(const QUrl &address);

/// `{"method": method, "params": params, "id": id}`.
QByteArray call(const QString &method, const QJsonArray &params, int id);

struct Reading
{
    Fingerprint fingerprint = Fingerprint::NotItself;
    QJsonValue result;    ///< the call's `result`
    QString errorMessage; ///< the call's `error.message`, e.g. "Not authenticated"
};

/// Any answer to a POST at jsonUrl(): JSON-RPC v1 with an `id`, a `result` and an `error`.
Reading read(const HttpAnswer &answer);

/// web.get_hosts' rows, `[id, host, port, user]`, as "host:port"; their ids beside them.
QStringList hostNames(const QJsonValue &hosts, QStringList *ids = nullptr);

} // namespace omaseed::core::DelugeFingerprint
