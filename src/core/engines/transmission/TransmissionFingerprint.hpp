#pragma once

// TransmissionFingerprint — does Transmission answer at this address? (C25 K5, K6, K8)
//
// Transmission's RPC is one POST endpoint, /transmission/rpc. K6: the address may be its web
// UI's (…/transmission/web/), its RPC's, a bare host:port, or a proxy's own path in front of
// either; rpcUrl() finds the endpoint. The first POST is answered 409 with the
// `X-Transmission-Session-Id` to send back (its anti-CSRF dance; 4.1 adds its RPC version,
// `X-Transmission-Rpc-Version: 6.0.0`), or 401 with `WWW-Authenticate: Basic
// realm="Transmission"` when it wants a login first. Sent again with the id, `session_get`
// names its version ("4.1.3 (6e5b2d1)"). Below RPC 6.0.0 (4.1, the first with JSON-RPC 2.0)
// omaseed refuses it (EngineKind's engineSupport).

#include "core/engines/EngineFingerprint.hpp"

#include <QUrl>

namespace omaseed::core::TransmissionFingerprint {

/// K6: the RPC endpoint behind `address`.
QUrl rpcUrl(const QUrl &address);

/// JSON-RPC 2.0 session_get for the version fields (4.1's snake_case names).
QByteArray sessionGetBody();

struct Reading
{
    Fingerprint fingerprint = Fingerprint::NotItself;
    QByteArray sessionId;    ///< a 409's, to send back
    QString rpcVersion;      ///< "6.0.0", from the 409's header or session_get
    QString version;         ///< "4.1.3", from session_get
    bool wantsLogin = false; ///< 401: Basic auth asked for
};

/// Any answer to a POST at rpcUrl(): the first (409 or 401) or the one after the session id.
Reading read(const HttpAnswer &answer);

} // namespace omaseed::core::TransmissionFingerprint
