#include "ui/app/AgentService.hpp"

#include "core/AgentWords.hpp"
#include "core/AltPresets.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/MoveJob.hpp"
#include "core/PauseHold.hpp"
#include "core/ProblemLog.hpp"
#include "core/ServerListModel.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentProblem.hpp"
#include "core/Version.hpp"
#include "integration/BarMenu.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#include <memory>

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

QString AgentService::failure(const QString &command, const QString &code, const QString &message)
{
    return QString::fromUtf8(core::AgentWords::failure(
        command, code, message, QDateTime::currentDateTimeUtc(), core::version()));
}

void AgentService::status(const QString &options, Reply reply)
{
    const Answers::ParsedOptions parsed = Answers::parseOptions(QStringLiteral("status"), options);
    if (!parsed.usage.isEmpty()) {
        reply(failure(QStringLiteral("status"), QStringLiteral("usage"), parsed.usage));
        return;
    }
    reply(QString::fromUtf8(Answers::status(statusInput())));
}

void AgentService::errors(const QString &options, Reply reply)
{
    const QString command = QStringLiteral("errors");
    const Answers::ParsedOptions parsed = Answers::parseOptions(command, options);
    if (!parsed.usage.isEmpty()) {
        reply(failure(command, QStringLiteral("usage"), parsed.usage));
        return;
    }
    const Answers::Server current = currentServer();
    if (!parsed.options.server.isEmpty()) {
        const Answers::ServerMatch match = matchServer(parsed.options.server);
        if (!match.code.isEmpty()) {
            reply(failure(command, match.code, match.message));
            return;
        }
        if (match.id != current.id) {
            // C45 V6: errors reads only the server omaseed is on; no word switches servers.
            const ServerListModel *list = m_app->serverList();
            const QString name =
                list->data(list->index(list->indexOfId(match.id), 0), ServerListModel::NameRole)
                    .toString();
            reply(failure(command,
                          QStringLiteral("not-current"),
                          Answers::notCurrentWords(current.name, name, command)));
            return;
        }
    }
    if (!current.reachable && !current.problem) {
        // Design 12: before the first full update there is nothing to read yet.
        const ServerListModel *list = m_app->serverList();
        const int row = current.id.isEmpty() ? -1 : list->indexOfId(current.id);
        const QString state =
            row < 0 ? QString()
                    : list->data(list->index(row, 0), ServerListModel::StateRole).toString();
        if (row < 0 || state == QLatin1String("syncing") || state == QLatin1String("connecting")) {
            reply(failure(command,
                          QStringLiteral("first-sync"),
                          QStringLiteral("%1 is still loading its torrents. Ask again in a "
                                         "moment.")
                              .arg(current.name)));
            return;
        }
    }

    // One answer, whichever comes first: the read's end or the wait's.
    auto answered = std::make_shared<bool>(false);
    auto answer = [this, reply, answered]() {
        if (*answered)
            return;
        *answered = true;
        reply(QString::fromUtf8(Answers::errors(errorsInput())));
    };
    core::ProblemLog *log = m_app->engine()->problems();
    // C45 V9: C2 reads the log every 15 s only while the window shows; a failure written
    // while it was hidden is read now. Not while the server is down: the read can't answer.
    if (log == nullptr || !current.reachable || !m_app->features().engineLog) {
        answer();
        return;
    }
    QTimer::singleShot(kErrorsWaitMs, this, answer);
    log->readNow(this, answer);
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

Answers::ErrorsInput AgentService::errorsInput() const
{
    Answers::ErrorsInput input;
    input.at = QDateTime::currentDateTimeUtc();
    input.omaseed = core::version();
    input.server = currentServer();

    const core::SyncEngine *engine = m_app->engine();
    const core::TorrentModel *torrents = engine->torrents();
    const core::TorrentFilterModel *filtered = m_app->filtered();
    const core::ProblemLog *log = engine->problems();
    const bool hasLog = m_app->features().engineLog;
    for (int row = 0; row < torrents->count(); ++row) {
        const core::Torrent &torrent = torrents->torrentAt(row);
        // C45 V9: the rail's needs-you, with the row's own words (C24 H6).
        if (filtered->questionOfHash(torrent.hash) != core::TorrentFilterModel::NeedsYou)
            continue;
        const core::RowWords words = filtered->wordsOfHash(torrent.hash);
        const core::TorrentProblem problem =
            log ? log->problem(torrent.hash) : core::TorrentProblem();
        const core::TorrentFailure failure =
            log ? log->failure(torrent.hash) : core::TorrentFailure();
        Answers::ErrorTorrent out;
        out.hash = torrent.hash;
        out.name = torrent.name;
        out.problem = !problem.headline.isEmpty() || !problem.causeKey.isEmpty();
        out.stuckMove = problem.stuckMove;
        out.missingFiles = problem.missingFiles;
        out.problemCause = problem.causeKey;
        out.failureLine = failure.line;
        out.fact = words.fact;
        out.sentence = words.sentence;
        out.fixWords = words.fix;
        out.fixAction = words.fixAction;
        out.since = out.problem ? problem.loggedAt : failure.loggedAt;
        out.phase = torrent.status.phase;
        out.savePath = torrent.savePath;
        if (log && hasLog) {
            const auto lines =
                log->linesNaming(torrent.name, core::ProblemMatch::hashPrefix(torrent), 3);
            for (const core::LogEntry &line : lines)
                out.log.append({line.id,
                                QDateTime::fromSecsSinceEpoch(line.timestamp, QTimeZone::UTC),
                                line.type,
                                line.message});
        }
        input.torrents.append(out);
    }
    if (log) {
        for (const QString &hash : log->dismissedTorrents())
            if (torrents->indexOfHash(hash) >= 0)
                ++input.dismissed;
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
