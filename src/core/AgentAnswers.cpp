#include "core/AgentAnswers.hpp"

#include "core/AgentWords.hpp"
#include "core/AnnounceMask.hpp"

#include <QJsonDocument>
#include <QJsonValue>

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

} // namespace omaseed::core::AgentAnswers
