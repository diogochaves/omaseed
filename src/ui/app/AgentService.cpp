#include "ui/app/AgentService.hpp"

#include "core/AgentWords.hpp"
#include "core/AltPresets.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/MoveJob.hpp"
#include "core/PauseHold.hpp"
#include "core/ServerListModel.hpp"
#include "core/SyncEngine.hpp"
#include "core/Version.hpp"
#include "integration/BarMenu.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

namespace omaseed::app {

namespace Answers = core::AgentAnswers;
using core::ServerListModel;

AgentService::AgentService(App *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{}

void AgentService::setWindowShown(bool shown)
{
    m_windowShown = shown;
}

void AgentService::status(const QString &options, Reply reply)
{
    const Answers::ParsedOptions parsed = Answers::parseOptions(QStringLiteral("status"), options);
    if (!parsed.usage.isEmpty()) {
        reply(QString::fromUtf8(core::AgentWords::failure(QStringLiteral("status"),
                                                          QStringLiteral("usage"),
                                                          parsed.usage,
                                                          QDateTime::currentDateTimeUtc(),
                                                          core::version())));
        return;
    }
    reply(QString::fromUtf8(Answers::status(statusInput())));
}

Answers::Server AgentService::currentServer() const
{
    const core::GlobalState *state = m_app->engine()->globalState();
    Answers::Server server;
    server.id = m_app->serverId();
    server.name = m_app->serverName();
    // servers.json's id for the engine; the environment's server has no row, so its name.
    const ServerListModel *list = m_app->serverList();
    const int row = server.id.isEmpty() ? -1 : list->indexOfId(server.id);
    server.engine = row >= 0
                        ? list->data(list->index(row, 0), ServerListModel::EngineRole).toString()
                        : m_app->serverEngineName().toLower();
    server.address = m_app->serverUrlText();
    server.reachable = state->connected();
    if (server.reachable) {
        server.version = state->serverVersion();
        if (server.version.startsWith(QLatin1Char('v')))
            server.version.remove(0, 1);
    }
    // The status file's rule (StatusWriter::content): an engine that reports none is connected.
    const QString connection = state->connectionStatus();
    server.connection = !server.reachable      ? QStringLiteral("disconnected")
                        : connection.isEmpty() ? QStringLiteral("connected")
                                               : connection;
    if (!server.reachable) {
        const QVariantMap problem =
            m_app->serverProblem(state->lastError(), state->retryInSeconds());
        if (!problem.isEmpty()) {
            // The line without the retry: the retry is its own field (and its own line).
            const QString line = problem.value(QStringLiteral("lineBase")).toString();
            server.problem = Answers::Problem{
                problem.value(QStringLiteral("headline")).toString(),
                line.isEmpty() ? problem.value(QStringLiteral("line")).toString() : line,
                state->retryInSeconds(),
            };
        }
    }
    return server;
}

Answers::StatusInput AgentService::statusInput() const
{
    const core::SyncEngine *engine = m_app->engine();
    const core::GlobalState *state = engine->globalState();
    const core::FilterCounts *counts = engine->counts();

    Answers::StatusInput input;
    input.at = QDateTime::currentDateTimeUtc();
    input.omaseed = core::version();
    input.windowShown = m_windowShown;
    input.server = currentServer();
    if (input.server.reachable) {
        input.down = state->dlInfoSpeed();
        input.up = state->upInfoSpeed();
    }
    if (const core::AltPresets *presets = m_app->altPresets())
        input.altPick = presets->current();
    input.all = counts->total();
    input.needsYou = counts->needsYou();
    input.arriving = counts->arriving();
    input.landedToday = counts->landedToday();
    input.givingBack = counts->givingBack();
    input.resting = counts->resting();
    if (const MoveController *move = m_app->move()) {
        // The controller keeps an idle job between moves: only one under way counts.
        const core::MoveJob *job = move->job();
        if (job && job->isActive()) {
            input.movesRunning = 1;
            input.moveNow =
                Answers::MoveNow{job->title(), job->newPath(), job->moved(), job->total()};
            input.movesQueued = int(move->queued().size());
        }
    }
    if (const core::PauseHold *hold = m_app->pauseHold())
        input.paused = hold->count();

    // The bar menu's list (C45 V8: its servers, as the widget reads them), with each one's
    // engine, and the count last seen where the menu has none (the current one while down).
    const ServerListModel *list = m_app->serverList();
    for (const QJsonValue &value : m_app->barMenu()->serversJson()) {
        const QJsonObject entry = value.toObject();
        Answers::ListedServer server;
        server.id = entry.value(QStringLiteral("id")).toString();
        server.name = entry.value(QStringLiteral("name")).toString();
        server.current = entry.value(QStringLiteral("current")).toBool();
        server.state = entry.value(QStringLiteral("state")).toString();
        server.count = entry.value(QStringLiteral("count")).toInt(-1);
        server.reason = entry.value(QStringLiteral("reason")).toString();
        const qint64 seen = entry.value(QStringLiteral("seen")).toInteger();
        if (seen > 0)
            server.seen = QDateTime::fromSecsSinceEpoch(seen, QTimeZone::UTC);
        const int row = list->indexOfId(server.id);
        if (row >= 0) {
            const QModelIndex at = list->index(row, 0);
            server.engine = list->data(at, ServerListModel::EngineRole).toString();
            if (server.count < 0)
                server.count = list->data(at, ServerListModel::LastCountRole).toInt();
        }
        input.servers.append(server);
    }
    return input;
}

Answers::ServerMatch AgentService::matchServer(const QString &query) const
{
    const ServerListModel *list = m_app->serverList();
    QList<Answers::ServerRef> servers;
    for (int row = 0; row < list->count(); ++row) {
        const QModelIndex at = list->index(row, 0);
        servers.append({list->data(at, ServerListModel::IdRole).toString(),
                        list->data(at, ServerListModel::NameRole).toString()});
    }
    return Answers::matchServer(servers, query);
}

} // namespace omaseed::app
