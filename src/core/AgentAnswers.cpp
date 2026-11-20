#include "core/AgentAnswers.hpp"

#include "core/AgentWords.hpp"
#include "core/AnnounceMask.hpp"
#include "core/ServerLog.hpp"
#include "core/TorrentProblem.hpp"

#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>

namespace omaseed::core::AgentAnswers {

namespace {

using AgentWords::isoUtc;
using AnnounceMask::maskText;

/// A whole JSON number in [low, high].
bool wholeIn(const QJsonValue &value, qint64 low, qint64 high, qint64 *out)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    const auto whole = static_cast<qint64>(number);
    if (static_cast<double>(whole) != number || whole < low || whole > high)
        return false;
    *out = whole;
    return true;
}

QString withoutBraces(const QString &id)
{
    QString bare = id;
    if (bare.startsWith(QLatin1Char('{')))
        bare.remove(0, 1);
    if (bare.endsWith(QLatin1Char('}')))
        bare.chop(1);
    return bare;
}

} // namespace

ParsedOptions parseOptions(const QString &command, const QString &json)
{
    ParsedOptions parsed;
    Options &options = parsed.options;
    options.level = QString::fromLatin1(AgentWords::kDefaultLevel);
    options.lines = AgentWords::kDefaultLines;
    options.landed = AgentWords::kDefaultLanded;
    if (json.trimmed().isEmpty())
        return parsed;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        parsed.usage = AgentWords::doesNotTake(command, json);
        return parsed;
    }
    const QStringList takes = AgentWords::optionsOf(command);
    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key();
        const QJsonValue value = it.value();
        const auto refuse = [&]() {
            const QString given =
                QJsonDocument(QJsonObject{{key, value}}).toJson(QJsonDocument::Compact);
            parsed.usage = AgentWords::doesNotTake(command, given);
        };
        if (!takes.contains(key)) {
            refuse();
            return parsed;
        }
        qint64 n = 0;
        if (key == QLatin1String("level")) {
            if (!AgentWords::levels().contains(value.toString())) {
                refuse();
                return parsed;
            }
            options.level = value.toString();
        } else if (key == QLatin1String("lines")) {
            if (!wholeIn(value, 1, AgentWords::kMaxLines, &n)) {
                refuse();
                return parsed;
            }
            options.lines = static_cast<int>(n);
        } else if (key == QLatin1String("landed")) {
            if (!wholeIn(value, 1, AgentWords::kMaxLanded, &n)) {
                refuse();
                return parsed;
            }
            options.landed = static_cast<int>(n);
        } else if (key == QLatin1String("since")) {
            if (!wholeIn(value, 0, qint64(1) << 53, &n)) {
                refuse();
                return parsed;
            }
            options.since = n;
        } else { // server, torrent: a string with something in it
            if (!value.isString() || value.toString().trimmed().isEmpty()) {
                refuse();
                return parsed;
            }
            if (key == QLatin1String("server"))
                options.server = value.toString();
            else
                options.torrent = value.toString().toLower();
        }
    }
    return parsed;
}

ServerMatch matchServer(const QList<ServerRef> &servers, const QString &query)
{
    ServerMatch match;
    const QString bare = withoutBraces(query);
    for (const ServerRef &server : servers) {
        if (!bare.isEmpty() && withoutBraces(server.id) == bare) {
            match.id = server.id;
            return match;
        }
    }
    for (const ServerRef &server : servers) {
        if (server.name == query) {
            match.id = server.id;
            return match;
        }
    }
    QStringList byCase;
    for (const ServerRef &server : servers)
        if (server.name.compare(query, Qt::CaseInsensitive) == 0)
            byCase << server.id;
    if (byCase.size() == 1) {
        match.id = byCase.first();
        return match;
    }
    match.code = QStringLiteral("unknown-server");
    match.message = QStringLiteral("No server called %1. omaseed status lists them.").arg(query);
    return match;
}

QString notCurrentWords(const QString &current, const QString &name, const QString &word)
{
    return QStringLiteral("omaseed is on %1, not %2. %3 reads only the server omaseed is on.")
        .arg(current, name, word);
}

QList<std::pair<QString, QJsonValue>>
envelope(const QString &command, const QDateTime &at, const QString &omaseed)
{
    return {
        {QStringLiteral("v"), 1},
        {QStringLiteral("command"), command},
        {QStringLiteral("at"), isoUtc(at)},
        {QStringLiteral("omaseed"), omaseed},
    };
}

QJsonObject serverJson(const Server &server)
{
    QJsonValue problem = QJsonValue::Null;
    if (server.problem) {
        problem = QJsonObject{
            {QStringLiteral("headline"), maskText(server.problem->headline)},
            {QStringLiteral("line"), maskText(server.problem->line)},
            {QStringLiteral("retryIn"), server.problem->retryIn},
        };
    }
    return QJsonObject{
        {QStringLiteral("id"), server.id},
        {QStringLiteral("name"), server.name},
        {QStringLiteral("engine"), server.engine},
        {QStringLiteral("version"), server.version},
        {QStringLiteral("address"), server.address},
        {QStringLiteral("reachable"), server.reachable},
        // C45 V8: added within v1 beside the board's fields, the status file's word for it.
        {QStringLiteral("connection"), server.connection},
        {QStringLiteral("problem"), problem},
    };
}

QJsonArray serversJson(const QList<ListedServer> &servers)
{
    QJsonArray out;
    for (const ListedServer &server : servers) {
        out.append(QJsonObject{
            {QStringLiteral("id"), server.id},
            {QStringLiteral("name"), server.name},
            {QStringLiteral("engine"), server.engine},
            {QStringLiteral("current"), server.current},
            {QStringLiteral("state"), server.state},
            {QStringLiteral("count"), server.count},
            {QStringLiteral("seen"), isoUtc(server.seen)},
            {QStringLiteral("reason"), maskText(server.reason)},
        });
    }
    return out;
}

QByteArray status(const StatusInput &input)
{
    QJsonValue now = QJsonValue::Null;
    if (input.moveNow) {
        now = QJsonObject{
            {QStringLiteral("title"), maskText(input.moveNow->title)},
            {QStringLiteral("to"), input.moveNow->to},
            {QStringLiteral("there"), input.moveNow->there},
            {QStringLiteral("of"), input.moveNow->of},
        };
    }
    QList<std::pair<QString, QJsonValue>> fields =
        envelope(QStringLiteral("status"), input.at, input.omaseed);
    fields.append({
        {QStringLiteral("window"),
         input.windowShown ? QStringLiteral("shown") : QStringLiteral("hidden")},
        {QStringLiteral("server"), serverJson(input.server)},
        {QStringLiteral("speed"),
         QJsonObject{{QStringLiteral("down"), input.down}, {QStringLiteral("up"), input.up}}},
        {QStringLiteral("alt"), QJsonObject{{QStringLiteral("pick"), input.altPick}}},
        {QStringLiteral("questions"),
         QJsonObject{
             {QStringLiteral("all"), input.all},
             {QStringLiteral("needsYou"), input.needsYou},
             {QStringLiteral("arriving"), input.arriving},
             {QStringLiteral("landedToday"), input.landedToday},
             {QStringLiteral("givingBack"), input.givingBack},
             {QStringLiteral("resting"), input.resting},
         }},
        {QStringLiteral("moves"),
         QJsonObject{
             {QStringLiteral("running"), input.movesRunning},
             {QStringLiteral("queued"), input.movesQueued},
             {QStringLiteral("now"), now},
         }},
        {QStringLiteral("paused"), input.paused},
        {QStringLiteral("servers"), serversJson(input.servers)},
    });
    return AgentWords::orderedJson(fields);
}

QString kindOf(const ErrorTorrent &torrent)
{
    if (torrent.problem && torrent.stuckMove)
        return QStringLiteral("stuck-move");
    if ((torrent.problem && torrent.missingFiles) || torrent.phase == TorrentPhase::Missing)
        return QStringLiteral("missing-files");
    if (torrent.problem || torrent.failureLine.isEmpty())
        return QStringLiteral("error");
    return QStringLiteral("failure");
}

QString causeOf(const ErrorTorrent &torrent)
{
    if (torrent.problem)
        return torrent.problemCause;
    if (torrent.failureLine.isEmpty())
        return QString();
    // A failure keeps no cause of its own: its line says it, as ProblemLog reads a job's.
    const ProblemLine line = ProblemLine::parse(torrent.failureLine);
    if (line.kind == ProblemLine::Kind::Unknown)
        return QString();
    return ProblemMatch::describe(line).causeKey;
}

FixDo fixDo(const QString &fixAction)
{
    const QString id = fixAction.section(QLatin1Char(':'), -1);
    if (id == QLatin1String("start"))
        return {QStringLiteral("retry"), {}};
    if (id == QLatin1String("startHere"))
        return {QStringLiteral("start-here"), {}};
    if (id == QLatin1String("location"))
        return {QStringLiteral("move"), {QStringLiteral("--to")}};
    if (id == QLatin1String("recheck"))
        return {QStringLiteral("recheck"), {}};
    return {}; // addAgain, folder: the window's own, no action of `do`'s (docs/62 V9)
}

QString phaseName(TorrentPhase phase)
{
    switch (phase) {
    case TorrentPhase::Metadata:
        return QStringLiteral("metadata");
    case TorrentPhase::Downloading:
        return QStringLiteral("downloading");
    case TorrentPhase::Seeding:
        return QStringLiteral("seeding");
    case TorrentPhase::Checking:
        return QStringLiteral("checking");
    case TorrentPhase::Loading:
        return QStringLiteral("loading");
    case TorrentPhase::Allocating:
        return QStringLiteral("allocating");
    case TorrentPhase::Moving:
        return QStringLiteral("moving");
    case TorrentPhase::Stopped:
        return QStringLiteral("stopped");
    case TorrentPhase::Errored:
        return QStringLiteral("errored");
    case TorrentPhase::Missing:
        return QStringLiteral("missing");
    case TorrentPhase::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

TorrentMatch
matchTorrent(const QList<LogTorrent> &library, const QString &query, const QString &serverName)
{
    const QString key = query.trimmed().toLower();
    TorrentMatch match;
    QList<LogTorrent> found;
    for (const LogTorrent &torrent : library) {
        if (torrent.hash.compare(key, Qt::CaseInsensitive) == 0) {
            match.torrent = torrent;
            return match;
        }
        if (!key.isEmpty() && torrent.hash.startsWith(key, Qt::CaseInsensitive))
            found.append(torrent);
    }
    if (found.isEmpty()) {
        match.code = QStringLiteral("unknown-torrent");
        match.message = QStringLiteral("No torrent %1 on %2.").arg(query, serverName);
        return match;
    }
    if (found.size() == 1 && key.size() >= 8) {
        match.torrent = found.constFirst();
        return match;
    }
    match.code = QStringLiteral("ambiguous");
    match.message = QStringLiteral("%1 matches %2 on %3: give at least 8 characters of the hash.")
                        .arg(query,
                             found.size() == 1 ? QStringLiteral("1 torrent")
                                               : QStringLiteral("%1 torrents").arg(found.size()),
                             serverName);
    return match;
}

int levelOf(const QString &name)
{
    for (const int level :
         {ServerLog::Normal, ServerLog::Info, ServerLog::Warning, ServerLog::Critical}) {
        if (ServerLog::levelName(level) == name)
            return level;
    }
    return 0;
}

QString torrentNamedIn(const QString &message, const QList<LogTorrent> &library)
{
    // ServerLog::torrentNamed's rule (C3 D13), over plain rows instead of the library model.
    const QStringList quoted = ProblemMatch::quotedSegments(message);
    QString byPrefix;
    for (const QString &part : quoted) {
        for (const LogTorrent &torrent : library) {
            if (part == torrent.name)
                return torrent.hash;
            if (byPrefix.isEmpty() && !torrent.prefix.isEmpty() && part.size() > 7 &&
                part.at(6) == QLatin1Char(' ') && part.startsWith(torrent.prefix))
                byPrefix = torrent.hash;
        }
    }
    return byPrefix;
}

QByteArray log(const LogInput &input)
{
    const Options &options = input.options;
    const int least = std::max(1, levelOf(options.level));
    const qint64 newest = input.log.isEmpty() ? -1 : input.log.constLast().id;
    // qBittorrent numbers its log from 0 again after a restart (docs/62 V10).
    // An empty log keeps `since` as it is: nothing says it was restarted.
    const bool restarted = !input.log.isEmpty() && options.since > newest + 1;
    const qint64 since = restarted ? -1 : options.since;

    QList<const LogLine *> matched;
    for (const LogLine &line : input.log) {
        if (line.level < least)
            continue;
        if (since >= 0 && line.id < since)
            continue;
        if (input.torrent &&
            !ServerLog::names(line.message, input.torrent->name, input.torrent->prefix))
            continue;
        matched.append(&line);
    }
    const qsizetype from = std::max<qsizetype>(0, matched.size() - std::max(1, options.lines));
    QJsonArray lines;
    for (qsizetype i = from; i < matched.size(); ++i) {
        const LogLine &line = *matched.at(i);
        const QString about =
            input.torrent ? input.torrent->hash : torrentNamedIn(line.message, input.library);
        lines.append(QJsonObject{
            {QStringLiteral("id"), line.id},
            {QStringLiteral("at"), isoUtc(line.at)},
            {QStringLiteral("level"), ServerLog::levelName(line.level)},
            {QStringLiteral("message"), maskText(line.message)},
            {QStringLiteral("torrent"), about.isEmpty() ? QJsonValue() : QJsonValue(about)},
        });
    }

    QList<std::pair<QString, QJsonValue>> fields =
        envelope(QStringLiteral("log"), input.at, input.omaseed);
    fields.append({
        {QStringLiteral("server"), serverJson(input.server)},
        {QStringLiteral("level"), ServerLog::levelName(least)},
        {QStringLiteral("fromStart"), !input.log.isEmpty() && input.log.constFirst().id == 0},
        {QStringLiteral("oldest"),
         input.log.isEmpty() ? QJsonValue() : isoUtc(input.log.constFirst().at)},
        {QStringLiteral("matched"), qint64(matched.size())},
        {QStringLiteral("lines"), lines},
        {QStringLiteral("next"), newest >= 0 ? newest + 1 : std::max<qint64>(0, options.since)},
        {QStringLiteral("restarted"), restarted},
    });
    return AgentWords::orderedJson(fields);
}

QString moveStateName(MoveJob::State state)
{
    switch (state) {
    case MoveJob::State::Running:
        return QStringLiteral("running");
    case MoveJob::State::Pausing:
        return QStringLiteral("pausing");
    case MoveJob::State::Paused:
        return QStringLiteral("paused");
    case MoveJob::State::Ending:
        return QStringLiteral("ending");
    case MoveJob::State::Carrying:
        return QStringLiteral("carrying");
    case MoveJob::State::Idle:
    case MoveJob::State::Done:
    case MoveJob::State::Ended:
        break;
    }
    return QString();
}

QString pausedForName(MoveJob::PauseReason reason)
{
    switch (reason) {
    case MoveJob::PauseReason::User:
        return QStringLiteral("by-you");
    case MoveJob::PauseReason::Quit:
        return QStringLiteral("quit");
    case MoveJob::PauseReason::FirstFailed:
        return QStringLiteral("first-failed");
    case MoveJob::PauseReason::Refused:
        return QStringLiteral("refused");
    case MoveJob::PauseReason::NoSpace:
        return QStringLiteral("no-space");
    case MoveJob::PauseReason::NearlyFull:
        return QStringLiteral("nearly-full");
    case MoveJob::PauseReason::Stuck:
        return QStringLiteral("stuck");
    case MoveJob::PauseReason::None:
        break;
    }
    return QString();
}

QString runningMoveId(const QDateTime &started)
{
    return QStringLiteral("j-") +
           QString::number(started.isValid() ? started.toSecsSinceEpoch() : 0, 36);
}

RunningMove runningOf(const MoveJob &job, const QString &headline, const QString &line)
{
    RunningMove move;
    move.id = runningMoveId(job.startedAt());
    move.kind = job.kind();
    move.title = job.title();
    move.to = job.newPath();
    move.started = job.startedAt();
    move.state = moveStateName(job.state());
    if (job.state() == MoveJob::State::Paused)
        move.pausedFor = pausedForName(job.pauseReason());
    move.headline = headline;
    move.line = line;
    move.of = job.total();
    move.there = job.moved();
    move.failed = job.failed();
    move.leftOut = job.count(MoveJob::Step::LeftOut);
    move.bytesOf = job.bytesTotal();
    move.bytesThere = job.bytesMoved();
    move.nowHash = job.current();
    if (const MoveJob::Entry *entry = move.nowHash.isEmpty() ? nullptr : job.entry(move.nowHash))
        move.nowName = entry->name;
    move.queued = job.queuedTitles();
    return move;
}

QList<LandedMove>
landedOf(const MoveHistory &history, int count, const QHash<QString, QString> *savePaths)
{
    QList<LandedMove> landed;
    for (const MoveRecord &record : history.records()) {
        if (landed.size() >= count)
            break;
        LandedMove move;
        move.id = record.id;
        move.kind = record.kind;
        move.title = record.title;
        move.to = record.newPath;
        move.started = record.startedAt;
        move.finished = record.finishedAt;
        for (auto it = record.from.constBegin(); it != record.from.constEnd(); ++it)
            move.torrents.append(
                {it.key(), record.names.value(it.key()), it.value(), record.to.value(it.key())});
        std::sort(move.torrents.begin(),
                  move.torrents.end(),
                  [](const MovedTorrent &a, const MovedTorrent &b) {
                      return a.name != b.name ? a.name < b.name : a.hash < b.hash;
                  });
        if (savePaths != nullptr) {
            const MoveHistory::BackCheck check = history.check(record.id, *savePaths);
            MoveBack back;
            back.goBack = int(check.goBack.size());
            back.movedOutside = int(check.outside.size());
            back.gone = int(check.gone.size());
            for (const auto &[id, hashes] : check.movedAgain) {
                back.movedAgain += int(hashes.size());
                const MoveRecord *later = history.find(id);
                back.movedAgainBy.append({id, later ? later->finishedAt : QDateTime()});
            }
            move.back = back;
        }
        landed.append(move);
    }
    return landed;
}

QByteArray moves(const MovesInput &input)
{
    QJsonArray running;
    for (const RunningMove &move : input.running) {
        running.append(QJsonObject{
            {QStringLiteral("id"), move.id},
            {QStringLiteral("kind"), move.kind},
            {QStringLiteral("title"), move.title},
            {QStringLiteral("to"), move.to},
            {QStringLiteral("started"), isoUtc(move.started)},
            {QStringLiteral("state"), move.state},
            {QStringLiteral("pausedFor"),
             move.pausedFor.isEmpty() ? QJsonValue() : QJsonValue(move.pausedFor)},
            {QStringLiteral("headline"), maskText(move.headline)},
            {QStringLiteral("line"), maskText(move.line)},
            {QStringLiteral("torrents"),
             QJsonObject{
                 {QStringLiteral("of"), move.of},
                 {QStringLiteral("there"), move.there},
                 {QStringLiteral("failed"), move.failed},
                 {QStringLiteral("leftOut"), move.leftOut},
             }},
            {QStringLiteral("bytes"),
             QJsonObject{
                 {QStringLiteral("of"), move.bytesOf},
                 {QStringLiteral("there"), move.bytesThere},
             }},
            {QStringLiteral("now"),
             move.nowHash.isEmpty() ? QJsonValue()
                                    : QJsonValue(QJsonObject{
                                          {QStringLiteral("hash"), move.nowHash},
                                          {QStringLiteral("name"), move.nowName},
                                      })},
            {QStringLiteral("queued"), QJsonArray::fromStringList(move.queued)},
        });
    }
    QJsonArray landed;
    for (const LandedMove &move : input.landed) {
        QJsonArray torrents;
        for (const MovedTorrent &torrent : move.torrents)
            torrents.append(QJsonObject{
                {QStringLiteral("hash"), torrent.hash},
                {QStringLiteral("name"), torrent.name},
                {QStringLiteral("from"), torrent.from},
                {QStringLiteral("to"), torrent.to},
            });
        QJsonValue back;
        if (move.back) {
            QJsonArray by;
            for (const auto &[id, finished] : move.back->movedAgainBy)
                by.append(QJsonObject{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("finished"), isoUtc(finished)},
                });
            back = QJsonObject{
                {QStringLiteral("goBack"), move.back->goBack},
                {QStringLiteral("movedAgain"), move.back->movedAgain},
                {QStringLiteral("movedOutside"), move.back->movedOutside},
                {QStringLiteral("gone"), move.back->gone},
                {QStringLiteral("movedAgainBy"), by},
            };
        }
        landed.append(QJsonObject{
            {QStringLiteral("id"), move.id},
            {QStringLiteral("kind"), move.kind},
            {QStringLiteral("title"), move.title},
            {QStringLiteral("to"), move.to},
            {QStringLiteral("started"), isoUtc(move.started)},
            {QStringLiteral("finished"), isoUtc(move.finished)},
            {QStringLiteral("torrents"), torrents},
            {QStringLiteral("back"), back},
        });
    }
    QList<std::pair<QString, QJsonValue>> fields =
        envelope(QStringLiteral("moves"), input.at, input.omaseed);
    fields.append({
        {QStringLiteral("server"), serverJson(input.server)},
        {QStringLiteral("running"), running},
        {QStringLiteral("landed"), landed},
        {QStringLiteral("kept"), input.kept},
    });
    return AgentWords::orderedJson(fields);
}

QByteArray errors(const ErrorsInput &input)
{
    // C45 V9: only qBittorrent keeps a log omaseed reads; the others' text is the sentence.
    const bool hasLog = input.server.engine == QLatin1String("qbittorrent");
    QJsonArray torrents;
    for (const ErrorTorrent &torrent : input.torrents) {
        QJsonArray log;
        if (hasLog) {
            const qsizetype from = std::max<qsizetype>(0, torrent.log.size() - 3);
            for (qsizetype i = from; i < torrent.log.size(); ++i) {
                const LogLine &line = torrent.log.at(i);
                log.append(QJsonObject{
                    {QStringLiteral("id"), line.id},
                    {QStringLiteral("at"), isoUtc(line.at)},
                    {QStringLiteral("level"), ServerLog::levelName(line.level)},
                    {QStringLiteral("message"), maskText(line.message)},
                });
            }
        }
        const FixDo fix = fixDo(torrent.fixAction);
        torrents.append(QJsonObject{
            {QStringLiteral("hash"), torrent.hash},
            {QStringLiteral("name"), torrent.name},
            {QStringLiteral("kind"), kindOf(torrent)},
            {QStringLiteral("cause"), causeOf(torrent)},
            {QStringLiteral("fact"), maskText(torrent.fact)},
            {QStringLiteral("sentence"), maskText(torrent.sentence)},
            {QStringLiteral("since"), isoUtc(torrent.since)},
            {QStringLiteral("state"), phaseName(torrent.phase)},
            {QStringLiteral("savePath"), torrent.savePath},
            {QStringLiteral("fix"),
             QJsonObject{
                 {QStringLiteral("words"), torrent.fixWords},
                 {QStringLiteral("do"), fix.action},
                 {QStringLiteral("needs"), QJsonArray::fromStringList(fix.needs)},
             }},
            {QStringLiteral("log"), log},
        });
    }
    QList<std::pair<QString, QJsonValue>> fields =
        envelope(QStringLiteral("errors"), input.at, input.omaseed);
    fields.append({
        {QStringLiteral("server"), serverJson(input.server)},
        {QStringLiteral("torrents"), torrents},
        {QStringLiteral("dismissed"), input.dismissed},
    });
    return AgentWords::orderedJson(fields);
}

} // namespace omaseed::core::AgentAnswers
