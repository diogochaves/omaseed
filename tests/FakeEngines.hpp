#pragma once

// FakeEngines — FakeHttp handlers that answer as each engine would, from the shapes in the
// engine matrix (§1.1, §4.1): neither Transmission nor Deluge is installed on this machine,
// and the tests that recognise them (#84) talk to these instead.
//   * qbittorrent(): app/version, webapiVersion and an empty torrents/info, signed out or not;
//   * transmission(): the 409 with X-Transmission-Session-Id (and 4.1's RPC version), a 401
//     with a Transmission realm when it wants a login, then session_get;
//   * deluge(): deluge-web's JSON-RPC on /json, auth.check_session, auth.login with its
//     cookie, web.connected, web.get_hosts and daemon.get_version.

#include "FakeHttp.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fakeengines {

using fakehttp::FakeServer;
using fakehttp::httpResponse;
using fakehttp::Recorded;

/// What a fake engine answers: one handler per engine, 404 for every other path.
inline QByteArray notFound()
{
    return httpResponse(404, "Not Found", "Not Found");
}

inline QByteArray json(const QJsonObject &object, const QList<QByteArray> &headers = {})
{
    return httpResponse(200, "OK", QJsonDocument(object).toJson(QJsonDocument::Compact), headers);
}

inline QString methodOf(const Recorded &request)
{
    return QJsonDocument::fromJson(request.body)
        .object()
        .value(QStringLiteral("method"))
        .toString();
}

/// A Transmission 4.1.3: the 409 dance, then session_get. With `login` set it asks for Basic
/// first and takes only that login.
inline FakeServer::Handler transmission(const QByteArray &login = {},
                                        const QByteArray &rpcVersion = "6.0.0")
{
    return [login, rpcVersion](const Recorded &request, int) {
        if (!request.target.startsWith("/transmission/rpc"))
            return notFound();
        if (!login.isEmpty() && request.header("Authorization") != "Basic " + login.toBase64())
            return httpResponse(401,
                                "Unauthorized",
                                "<h1>401: Unauthorized</h1>",
                                {"WWW-Authenticate: Basic realm=\"Transmission\""});
        if (request.header("X-Transmission-Session-Id") != "s3ss10n") {
            QList<QByteArray> headers{"X-Transmission-Session-Id: s3ss10n"};
            if (!rpcVersion.isEmpty())
                headers.append("X-Transmission-Rpc-Version: " + rpcVersion);
            return httpResponse(409, "Conflict", "<h1>409: Conflict</h1>", headers);
        }
        if (rpcVersion.isEmpty()) // 4.0 and older: the legacy envelope
            return json(
                {{QStringLiteral("result"), QStringLiteral("success")},
                 {QStringLiteral("arguments"),
                  QJsonObject{{QStringLiteral("version"), QStringLiteral("3.00 (bb6b5a062e)")}}}});
        return json({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                     {QStringLiteral("id"), 1},
                     {QStringLiteral("result"),
                      QJsonObject{{QStringLiteral("version"), QStringLiteral("4.1.3 (6e5b2d1)")},
                                  {QStringLiteral("rpc_version_semver"),
                                   QString::fromLatin1(rpcVersion)}}}});
    };
}

/// deluge-web 2.2.0 with password "deluge": its session cookie, one daemon.
inline FakeServer::Handler deluge(bool connected = true)
{
    return [connected](const Recorded &request, int) {
        if (request.target != "/json")
            return notFound();
        const QJsonObject call = QJsonDocument::fromJson(request.body).object();
        const QString method = call.value(QStringLiteral("method")).toString();
        const int id = call.value(QStringLiteral("id")).toInt();
        const auto answer = [id](const QJsonValue &result, const QList<QByteArray> &headers = {}) {
            return json({{QStringLiteral("result"), result},
                         {QStringLiteral("error"), QJsonValue::Null},
                         {QStringLiteral("id"), id}},
                        headers);
        };
        if (method == QLatin1String("auth.check_session"))
            return answer(false);
        if (method == QLatin1String("auth.login")) {
            const bool ok = call.value(QStringLiteral("params")).toArray().at(0).toString() ==
                            QLatin1String("deluge");
            return ok ? answer(true, {"Set-Cookie: _session_id=c00k1e; Path=/json"})
                      : answer(false);
        }
        if (!request.header("Cookie").contains("_session_id=c00k1e"))
            return json(
                {{QStringLiteral("result"), QJsonValue::Null},
                 {QStringLiteral("error"),
                  QJsonObject{{QStringLiteral("message"), QStringLiteral("Not authenticated")},
                              {QStringLiteral("code"), 1}}},
                 {QStringLiteral("id"), id}});
        if (method == QLatin1String("web.connected"))
            return answer(connected);
        if (method == QLatin1String("web.get_hosts"))
            return answer(QJsonArray{QJsonArray{QStringLiteral("c0ffee"),
                                                QStringLiteral("127.0.0.1"),
                                                58846,
                                                QStringLiteral("localclient")}});
        if (method == QLatin1String("daemon.get_version"))
            return answer(QStringLiteral("2.2.0"));
        return answer(QJsonValue::Null);
    };
}

/// qBittorrent 5.3.0: Web API 2.16.2, no torrents. `signedOut` answers app/version with 403.
inline FakeServer::Handler qbittorrent(bool signedOut = false)
{
    return [signedOut](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/app/version"))
            return signedOut ? httpResponse(403, "Forbidden", "Forbidden")
                             : httpResponse(200, "OK", "v5.3.0");
        if (request.target.startsWith("/api/v2/app/webapiVersion"))
            return httpResponse(200, "OK", "2.16.2");
        if (request.target.startsWith("/api/v2/torrents/info"))
            return httpResponse(200, "OK", "[]");
        return notFound();
    };
}

} // namespace fakeengines
