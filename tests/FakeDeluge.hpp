#pragma once

// FakeDeluge — a small deluge-web 2.2 of the tests' own, on FakeHttp (#87, E6): one password,
// the `_session_id` cookie it hands out, "Not authenticated" (code 1) without it, and every
// other call answered by the test's `answer`, each one recorded. `fixture()` reads deluge-web's
// recorded answers (tests/fixtures/deluge/); the including target defines OMASEED_FIXTURES_DIR.
// `RecordedDeluge` answers the engine's reads from them: web.update_ui and core.get_torrent(s)_
// status are cut from the recorded torrents to the keys and torrents asked for.

#include "FakeHttp.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

namespace fakedeluge {

using namespace fakehttp;

inline QJsonObject fixture(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/deluge/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return QJsonDocument::fromJson(file.readAll()).object();
}

/// A recorded answer's `result`.
inline QJsonValue result(const char *name)
{
    return fixture(name).value(QStringLiteral("result"));
}

/// A recorded error, as `answer` hands it back.
inline QJsonValue errorOf(const char *name)
{
    return QJsonObject{{QStringLiteral("__error"), fixture(name).value(QStringLiteral("error"))}};
}

inline QJsonValue errorOf(int code, const QString &message)
{
    return QJsonObject{
        {QStringLiteral("__error"),
         QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

/// One call as the fake deluge-web took it.
struct Call
{
    QString method;
    QJsonArray params;
};

class FakeDeluge
{
public:
    QString password = QStringLiteral("p@ss w0rd");
    /// Each method's answer: a result, or errorOf(…).
    std::function<QJsonValue(const QString &method, const QJsonArray &params)> answer;
    QList<Call> calls;
    int logins = 0;
    /// The next this many calls (sign-ins aside) get a 503, as a proxy in front of a deluge-web
    /// that is restarting would answer.
    int unavailable = 0;

    /// Every session handed out so far runs out (deluge-web's session_timeout, or a restart).
    void expireSessions() { m_valid.clear(); }

    FakeServer::Handler handler()
    {
        return [this](const Recorded &request, int) {
            if (request.target != "/json")
                return httpResponse(404, "Not Found", "Not Found");
            const QJsonObject call = QJsonDocument::fromJson(request.body).object();
            const QString method = call.value(QStringLiteral("method")).toString();
            const QJsonArray params = call.value(QStringLiteral("params")).toArray();
            const QJsonValue id = call.value(QStringLiteral("id"));
            calls.append({method, params});
            if (unavailable > 0 && method != QLatin1String("auth.login")) {
                --unavailable;
                return httpResponse(503, "Service Unavailable", "busy");
            }
            QList<QByteArray> headers;
            QJsonValue value;
            if (method == QLatin1String("auth.login")) {
                ++logins;
                const bool ok = params.at(0).toString() == password;
                if (ok) {
                    const QByteArray session = "s" + QByteArray::number(++m_sessions);
                    m_valid.append(session);
                    headers.append("Set-Cookie: _session_id=" + session + "; Path=/");
                }
                value = ok;
            } else if (method == QLatin1String("auth.check_session")) {
                value = m_valid.contains(cookie(request));
            } else if (!m_valid.contains(cookie(request))) {
                value = errorOf(1, QStringLiteral("Not authenticated"));
            } else {
                value = answer ? answer(method, params) : QJsonValue();
            }
            QJsonObject reply{{QStringLiteral("id"), id}};
            if (value.isObject() && value.toObject().contains(QStringLiteral("__error"))) {
                reply.insert(QStringLiteral("result"), QJsonValue());
                reply.insert(QStringLiteral("error"),
                             value.toObject().value(QStringLiteral("__error")));
            } else {
                reply.insert(QStringLiteral("result"), value);
                reply.insert(QStringLiteral("error"), QJsonValue());
            }
            return httpResponse(
                200, "OK", QJsonDocument(reply).toJson(QJsonDocument::Compact), headers);
        };
    }

    QStringList methods() const
    {
        QStringList list;
        for (const Call &call : calls)
            list.append(call.method);
        return list;
    }

    /// The calls of `method`, in order.
    QList<Call> callsOf(const QString &method) const
    {
        QList<Call> list;
        for (const Call &call : calls) {
            if (call.method == method)
                list.append(call);
        }
        return list;
    }

private:
    static QByteArray cookie(const Recorded &request)
    {
        for (const QByteArray &part : request.header("Cookie").split(';')) {
            const QByteArray trimmed = part.trimmed();
            if (trimmed.startsWith("_session_id="))
                return trimmed.mid(12);
        }
        return {};
    }

    int m_sessions = 0;
    QList<QByteArray> m_valid;
};

/// The engine's reads answered from the recorded daemon: its six torrents (every key the
/// recorder asked for), its filters and stats, its labels and settings.
struct RecordedDeluge
{
    QJsonObject torrents = result("core_get_torrents_status.json").toObject();
    QJsonObject updateUi = result("web_update_ui.json").toObject();
    bool connected = true;
    bool labelPlugin = true;

    /// `byHash`'s torrents of `ids` (every one when empty) with only `keys` (every key when
    /// empty).
    QJsonObject cut(const QJsonArray &ids, const QJsonArray &keys) const
    {
        QJsonObject out;
        for (auto it = torrents.constBegin(); it != torrents.constEnd(); ++it) {
            if (!ids.isEmpty() && !ids.contains(it.key()))
                continue;
            const QJsonObject all = it.value().toObject();
            QJsonObject some;
            for (const QJsonValue &key : keys) {
                // With the plugin off a real deluge-web still sends the label each torrent last
                // had (web_update_ui_no_label.json): so does this one.
                const QString name = key.toString();
                if (all.contains(name))
                    some.insert(name, all.value(name));
            }
            out.insert(it.key(), keys.isEmpty() ? all : some);
        }
        return out;
    }

    QJsonValue answer(const QString &method, const QJsonArray &params) const
    {
        if (method == QLatin1String("web.update_ui")) {
            if (!connected)
                return result("web_update_ui_disconnected.json");
            QJsonObject answer = updateUi;
            answer.insert(QStringLiteral("torrents"), cut({}, params.at(0).toArray()));
            if (!labelPlugin) {
                QJsonObject filters = answer.value(QStringLiteral("filters")).toObject();
                filters.remove(QStringLiteral("label"));
                answer.insert(QStringLiteral("filters"), filters);
            }
            return answer;
        }
        if (method == QLatin1String("core.get_torrents_status")) {
            const QJsonValue id = params.at(0).toObject().value(QStringLiteral("id"));
            return cut(id.isString() ? QJsonArray{id} : id.toArray(), params.at(1).toArray());
        }
        if (method == QLatin1String("core.get_torrent_status")) {
            const QJsonObject one = cut({params.at(0)}, params.at(1).toArray());
            return one.value(params.at(0).toString()).toObject();
        }
        if (method == QLatin1String("core.get_session_status"))
            return result("core_get_session_status.json");
        if (method == QLatin1String("web.connected"))
            return connected;
        if (method == QLatin1String("web.get_hosts"))
            return result("web_get_hosts.json");
        if (method == QLatin1String("web.get_host_status"))
            return result("web_get_host_status.json");
        if (method == QLatin1String("web.connect"))
            return result("web_connect.json");
        if (method == QLatin1String("daemon.get_version"))
            return result("daemon_get_version.json");
        if (method == QLatin1String("core.get_libtorrent_version"))
            return result("core_get_libtorrent_version.json");
        if (method == QLatin1String("core.get_enabled_plugins"))
            return labelPlugin ? result("core_get_enabled_plugins.json")
                               : result("core_get_enabled_plugins_none.json");
        if (method == QLatin1String("label.get_labels"))
            return result("label_get_labels.json");
        if (method == QLatin1String("label.get_options"))
            return params.at(0).toString() == QLatin1String("fixtures")
                       ? result("label_get_options.json")
                       : result("label_get_options_empty.json");
        if (method == QLatin1String("core.get_config_values"))
            return result("core_get_config_values.json");
        if (method == QLatin1String("core.get_config")) {
            // The whole of core.conf, its plugins as the Label switch says.
            QJsonObject config = result("core_get_config.json").toObject();
            if (!labelPlugin)
                config.insert(QStringLiteral("enabled_plugins"), QJsonArray());
            return config;
        }
        if (method == QLatin1String("core.get_free_space"))
            return result("core_get_free_space.json");
        return QJsonValue();
    }
};

} // namespace fakedeluge
