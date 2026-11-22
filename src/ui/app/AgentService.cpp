#include "ui/app/AgentService.hpp"

#include "core/AgentText.hpp"
#include "core/AgentWords.hpp"
#include "core/AltPresets.hpp"
#include "core/AnnounceMask.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/LocateCheck.hpp"
#include "core/MoveJob.hpp"
#include "core/PauseHold.hpp"
#include "core/ProblemLog.hpp"
#include "core/Reply.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerLog.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentProblem.hpp"
#include "core/Version.hpp"
#include "core/engines/EngineClient.hpp"
#include "core/engines/LogEntry.hpp"
#include "integration/BarMenu.hpp"
#include "integration/SkinWords.hpp"
#include "ui/app/AgentDo.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>

namespace omaseed::app {

namespace Answers = core::AgentAnswers;
using core::ServerListModel;

AgentService::AgentService(App *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_do(new AgentDo(app, this))
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

QString AgentService::readsOff(const QString &command) const
{
    // C50 GT8: a courtesy, not a wall; the words are board 67's reads line.
    if (m_app->agentGate().reads())
        return {};
    return failure(command,
                   QStringLiteral("not-allowed"),
                   QStringLiteral("Agents get nothing from omaseed's commands: Reads is off in "
                                  "Settings › General › Agents. Only the person can turn it on."));
}

void AgentService::useSkin(const QString &options, Reply reply)
{
    const QString command = QStringLiteral("skin use");
    const QJsonObject parsed = QJsonDocument::fromJson(options.toUtf8()).object();
    const QString name = parsed.value(QStringLiteral("name")).toString();
    if (name.isEmpty() || parsed.size() != 1) {
        reply(failure(command,
                      QStringLiteral("usage"),
                      QStringLiteral("omaseed: skin use takes a skin's name: omaseed skin use "
                                     "warm. omaseed help skin says how.")));
        return;
    }
    const integration::SkinWords::Pick picked =
        integration::SkinWords::pick(m_app->skinsDir(), name);
    if (!picked.code.isEmpty()) {
        reply(failure(command, picked.code, picked.message));
        return;
    }
    m_app->setSkinChoice(name);
    reply(QString::fromUtf8(integration::SkinWords::use(
        name, picked.skin, true, QDateTime::currentDateTimeUtc(), core::version())));
}

void AgentService::doAction(const QString &options, uint callerPid, Reply reply)
{
    m_do->run(options, callerPid, reply);
}

void AgentService::status(const QString &options, Reply reply)
{
    if (const QString off = readsOff(QStringLiteral("status")); !off.isEmpty()) {
        reply(off);
        return;
    }
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
    if (const QString off = readsOff(command); !off.isEmpty()) {
        reply(off);
        return;
    }
    const Answers::ParsedOptions parsed = Answers::parseOptions(command, options);
    if (!parsed.usage.isEmpty()) {
        reply(failure(command, QStringLiteral("usage"), parsed.usage));
        return;
    }
    const Answers::Server current = currentServer();
    if (!parsed.options.server.isEmpty()) {
        QString id;
        const QString refused = serverFailure(command, parsed.options.server, &id);
        if (!refused.isEmpty()) {
            reply(refused);
            return;
        }
        if (id != current.id) {
            // C45 V6: errors reads only the server omaseed is on; no word switches servers.
            reply(notCurrent(command, current.name, id));
            return;
        }
    }
    if (firstSync(current)) {
        reply(firstSyncFailure(command, current.name));
        return;
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

QString AgentService::serverFailure(const QString &command, const QString &query, QString *id) const
{
    const Answers::ServerMatch match = matchServer(query);
    *id = match.id;
    return match.code.isEmpty() ? QString() : failure(command, match.code, match.message);
}

QString
AgentService::notCurrent(const QString &command, const QString &current, const QString &id) const
{
    const ServerListModel *list = m_app->serverList();
    const QString name =
        list->data(list->index(list->indexOfId(id), 0), ServerListModel::NameRole).toString();
    return failure(
        command, QStringLiteral("not-current"), Answers::notCurrentWords(current, name, command));
}

bool AgentService::firstSync(const Answers::Server &current) const
{
    if (current.reachable || current.problem)
        return false;
    // Design 12: before the first full update there is nothing to read yet.
    const ServerListModel *list = m_app->serverList();
    const int row = current.id.isEmpty() ? -1 : list->indexOfId(current.id);
    const QString state =
        row < 0 ? QString()
                : list->data(list->index(row, 0), ServerListModel::StateRole).toString();
    return row < 0 || state == QLatin1String("syncing") || state == QLatin1String("connecting");
}

QString AgentService::firstSyncFailure(const QString &command, const QString &name) const
{
    return failure(
        command,
        QStringLiteral("first-sync"),
        QStringLiteral("%1 is still loading its torrents. Ask again in a moment.").arg(name));
}

QList<Answers::LogTorrent> AgentService::logLibrary() const
{
    const core::TorrentModel *torrents = m_app->engine()->torrents();
    QList<Answers::LogTorrent> library;
    library.reserve(torrents->count());
    for (int row = 0; row < torrents->count(); ++row) {
        const core::Torrent &torrent = torrents->torrentAt(row);
        library.append({torrent.hash, torrent.name, core::ProblemMatch::hashPrefix(torrent)});
    }
    return library;
}

void AgentService::log(const QString &options, Reply reply)
{
    const QString command = QStringLiteral("log");
    if (const QString off = readsOff(command); !off.isEmpty()) {
        reply(off);
        return;
    }
    const Answers::ParsedOptions parsed = Answers::parseOptions(command, options);
    if (!parsed.usage.isEmpty()) {
        reply(failure(command, QStringLiteral("usage"), parsed.usage));
        return;
    }
    const Answers::Server current = currentServer();
    if (!parsed.options.server.isEmpty()) {
        QString id;
        const QString refused = serverFailure(command, parsed.options.server, &id);
        if (!refused.isEmpty()) {
            reply(refused);
            return;
        }
        if (id != current.id) {
            reply(notCurrent(command, current.name, id)); // C45 V6
            return;
        }
    }
    // C45 V10: only qBittorrent keeps a log omaseed can read.
    if (!m_app->features().engineLog) {
        reply(failure(command,
                      QStringLiteral("no-log"),
                      QStringLiteral("%1 runs %2, which keeps no log omaseed can read. omaseed "
                                     "errors has each torrent's own error text.")
                          .arg(current.name, core::AgentText::engineName(current.engine))));
        return;
    }
    const auto unreachable = [command, name = current.name](const QString &line) {
        return failure(
            command,
            QStringLiteral("unreachable"),
            core::AnnounceMask::maskText(
                QStringLiteral("%1 is unreachable, so its log can't be read: %2").arg(name, line)));
    };
    if (!current.reachable) {
        if (firstSync(current))
            reply(firstSyncFailure(command, current.name));
        else
            reply(unreachable(current.problem ? current.problem->line : QString()));
        return;
    }

    Answers::LogInput input;
    input.omaseed = core::version();
    input.server = current;
    input.options = parsed.options;
    input.library = logLibrary();
    if (!parsed.options.torrent.isEmpty()) {
        const Answers::TorrentMatch match =
            Answers::matchTorrent(input.library, parsed.options.torrent, current.name);
        if (!match.code.isEmpty()) {
            reply(failure(command, match.code, match.message));
            return;
        }
        input.torrent = match.torrent;
    }
    core::EngineClient *client = m_app->client();
    if (client == nullptr) {
        reply(unreachable(QString()));
        return;
    }
    // Design 13: one read, the whole log at once; warnings and worse need only qBittorrent's
    // problems.
    const bool problemsOnly = Answers::levelOf(parsed.options.level) >= core::ServerLog::Warning;
    QPointer<AgentService> self(this);
    client->log(-1, problemsOnly)
        ->then(this,
               [self, reply, asked = input, unreachable](
                   const core::Result<QList<core::LogEntry>> &result) {
                   if (self.isNull())
                       return;
                   if (result.isError()) {
                       const QVariantMap problem =
                           self->m_app->serverProblem(result.error().toString(), 0);
                       const QString line = problem.value(QStringLiteral("lineBase")).toString();
                       reply(unreachable(line.isEmpty()
                                             ? problem.value(QStringLiteral("line")).toString()
                                             : line));
                       return;
                   }
                   Answers::LogInput input = asked;
                   for (const core::LogEntry &entry : result.value()) {
                       if (entry.id < 0)
                           continue;
                       input.log.append(
                           {entry.id,
                            QDateTime::fromSecsSinceEpoch(entry.timestamp, QTimeZone::UTC),
                            entry.type,
                            entry.message});
                   }
                   input.at = QDateTime::currentDateTimeUtc();
                   reply(QString::fromUtf8(Answers::log(input)));
               });
}

void AgentService::moves(const QString &options, Reply reply)
{
    const QString command = QStringLiteral("moves");
    if (const QString off = readsOff(command); !off.isEmpty()) {
        reply(off);
        return;
    }
    const Answers::ParsedOptions parsed = Answers::parseOptions(command, options);
    if (!parsed.usage.isEmpty()) {
        reply(failure(command, QStringLiteral("usage"), parsed.usage));
        return;
    }
    Answers::MovesInput input;
    input.at = QDateTime::currentDateTimeUtc();
    input.omaseed = core::version();
    input.server = currentServer();
    QString id = input.server.id;
    if (!parsed.options.server.isEmpty()) {
        const QString refused = serverFailure(command, parsed.options.server, &id);
        if (!refused.isEmpty()) {
            reply(refused);
            return;
        }
    }
    const MoveController *move = m_app->move();
    if (move == nullptr) {
        reply(QString::fromUtf8(Answers::moves(input)));
        return;
    }
    if (id == input.server.id) {
        const core::MoveJob *job = move->job();
        if (job && job->isActive()) {
            const QVariantMap band = move->band();
            input.running.append(
                Answers::runningOf(*job,
                                   band.value(QStringLiteral("headline")).toString(),
                                   band.value(QStringLiteral("line")).toString()));
        }
        // C18 D7: what a move back would do now, against the library as it is.
        QHash<QString, QString> savePaths;
        const core::TorrentModel *torrents = m_app->engine()->torrents();
        for (int row = 0; row < torrents->count(); ++row)
            savePaths.insert(torrents->torrentAt(row).hash, torrents->torrentAt(row).savePath);
        const core::MoveHistory &history = move->history();
        input.landed = Answers::landedOf(history, parsed.options.landed, &savePaths);
        input.kept = int(history.records().size());
    } else {
        // C45 V11: another server, from what omaseed keeps for it; its library isn't here.
        const ServerListModel *list = m_app->serverList();
        const QModelIndex at = list->index(list->indexOfId(id), 0);
        Answers::Server server;
        server.id = id;
        server.name = list->data(at, ServerListModel::NameRole).toString();
        server.engine = list->data(at, ServerListModel::EngineRole).toString();
        server.address = list->data(at, ServerListModel::UrlTextRole).toString();
        server.connection = QStringLiteral("disconnected");
        input.server = server;
        if (const std::unique_ptr<core::MoveJob> job = move->savedJobOf(id); job && job->isActive())
            input.running.append(Answers::runningOf(*job, QString(), QString()));
        const core::MoveHistory history = move->historyOf(id);
        input.landed = Answers::landedOf(history, parsed.options.landed, nullptr);
        input.kept = int(history.records().size());
    }
    reply(QString::fromUtf8(Answers::moves(input)));
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
    std::optional<core::LocateCheck::Mounts> mounts;
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
        // #300: a torrent in missing files whose drive isn't mounted says so; the mount
        // table read once, only when one is missing.
        if (Answers::kindOf(out) == QLatin1String("missing-files")) {
            const QString local = m_app->freeSpaceProbe().localPathFor(torrent.savePath);
            if (!local.isEmpty()) {
                if (!mounts)
                    mounts = core::LocateCheck::readMounts();
                out.unmountedDrive = core::LocateCheck::unmountedDrive(
                    local, *mounts, core::LocateCheck::existsLocal, qEnvironmentVariable("USER"));
            }
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
