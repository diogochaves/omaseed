#pragma once

// FakeTransmission — a small Transmission 4.1.3 of the tests' own, on FakeHttp (#85, E4): the
// session id's 409, Basic auth when `login` is set, and every JSON-RPC call answered by the
// test's `answer`, each one recorded. `fixture()` reads the daemon's recorded answers
// (tests/fixtures/transmission/); the including target defines OMASEED_FIXTURES_DIR.

#include "FakeHttp.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

namespace faketransmission {

using namespace fakehttp;

inline QJsonObject fixture(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/transmission/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return QJsonDocument::fromJson(file.readAll()).object();
}

inline QByteArray jsonBody(const QJsonValue &value)
{
    return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                           : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}

inline QByteArray ok(const QByteArray &body, const QList<QByteArray> &headers = {})
{
    return httpResponse(200, "OK", body, headers);
}

/// One JSON-RPC call as the fake daemon took it.
struct Call
{
    QString method;
    QJsonObject params;
    bool inBatch = false;
};

/// A Transmission 4.1.3: the 409 dance, Basic auth when `login` is set, and each method
/// answered by `answer` (a result object, or {"__error": the JSON-RPC error}).
class FakeTransmission
{
public:
    QByteArray sessionId = "s3ss10n";
    QByteArray login; ///< "user:pass", or empty for none
    QByteArray rpcVersion = "6.0.1";
    bool legacy = false; ///< answer as a 4.0 daemon (its own envelope)
    bool alwaysConflict = false;
    std::function<QJsonObject(const QString &method, const QJsonObject &params)> answer;
    QList<Call> calls;
    int expireNext = 0; ///< how many next requests get a fresh id, as an expired one would

    FakeServer::Handler handler()
    {
        return [this](const Recorded &request, int) {
            if (!request.target.startsWith("/transmission/rpc"))
                return httpResponse(404, "Not Found", "Not Found");
            if (!login.isEmpty() && request.header("Authorization") != "Basic " + login.toBase64())
                return httpResponse(401,
                                    "Unauthorized",
                                    "<h1>401: Unauthorized</h1>",
                                    {"WWW-Authenticate: Basic realm=\"Transmission\""});
            if (alwaysConflict || expireNext > 0 ||
                request.header("X-Transmission-Session-Id") != sessionId) {
                if (alwaysConflict || expireNext > 0) {
                    expireNext = qMax(0, expireNext - 1);
                    sessionId += "x";
                }
                QList<QByteArray> headers{"X-Transmission-Session-Id: " + sessionId};
                if (!rpcVersion.isEmpty())
                    headers.append("X-Transmission-Rpc-Version: " + rpcVersion);
                return httpResponse(409, "Conflict", "<h1>409: Conflict</h1>", headers);
            }
            const QJsonDocument document = QJsonDocument::fromJson(request.body);
            if (document.isArray()) {
                QJsonArray answers;
                for (const QJsonValue &one : document.array())
                    answers.append(respond(one.toObject(), true));
                return ok(jsonBody(answers));
            }
            return ok(jsonBody(respond(document.object(), false)));
        };
    }

    QStringList methods() const
    {
        QStringList list;
        for (const Call &call : calls)
            list.append(call.method);
        return list;
    }

    const Call &last() const { return calls.constLast(); }

private:
    QJsonObject respond(const QJsonObject &request, bool inBatch)
    {
        const QString method = request.value(QStringLiteral("method")).toString();
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        calls.append({method, params, inBatch});
        const QJsonObject result = answer ? answer(method, params) : QJsonObject();
        if (legacy)
            return {{QStringLiteral("result"), QStringLiteral("success")},
                    {QStringLiteral("arguments"), result}};
        QJsonObject reply{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                          {QStringLiteral("id"), request.value(QStringLiteral("id"))}};
        if (result.contains(QStringLiteral("__error")))
            reply.insert(QStringLiteral("error"), result.value(QStringLiteral("__error")));
        else
            reply.insert(QStringLiteral("result"), result);
        return reply;
    }
};

/// The poll's answers: the recorded torrents (every one, or those of `recent`), the recorded
/// session and stats, a free_space.
struct PollAnswers
{
    QList<QJsonObject> torrents;
    QJsonArray removed;
    QJsonObject sessionGet = fixture("session_get.json").value(QStringLiteral("result")).toObject();

    PollAnswers()
    {
        for (const QJsonValue &t : fixture("torrent_get_all.json")
                                       .value(QStringLiteral("result"))
                                       .toObject()
                                       .value(QStringLiteral("torrents"))
                                       .toArray())
            torrents.append(t.toObject());
    }

    QJsonObject answer(const QString &method, const QJsonObject &params) const
    {
        if (method == QLatin1String("session_stats"))
            return fixture("session_stats.json").value(QStringLiteral("result")).toObject();
        if (method == QLatin1String("session_get"))
            return sessionGet;
        if (method == QLatin1String("free_space"))
            return fixture("free_space.json").value(QStringLiteral("result")).toObject();
        if (method != QLatin1String("torrent_get"))
            return {};
        // Asked as a table, answered as one.
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        QJsonArray table{fields};
        for (const QJsonObject &t : torrents) {
            QJsonArray row;
            for (const QJsonValue &field : fields)
                row.append(t.value(field.toString()));
            table.append(row);
        }
        QJsonObject result{{QStringLiteral("torrents"), table}};
        if (params.value(QStringLiteral("ids")).toString() == QLatin1String("recently_active"))
            result.insert(QStringLiteral("removed"), removed);
        return result;
    }

    QJsonObject &byHash(const QString &hash)
    {
        for (QJsonObject &t : torrents) {
            if (t.value(QStringLiteral("hash_string")).toString() == hash)
                return t;
        }
        qFatal("no torrent %s", qPrintable(hash));
    }
};

} // namespace faketransmission
