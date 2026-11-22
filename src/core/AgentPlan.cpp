#include "core/AgentPlan.hpp"

#include "core/AgentWords.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace omaseed::core::AgentPlan {

namespace {

const QStringList &torrentActions()
{
    static const QStringList actions{QStringLiteral("retry"),
                                     QStringLiteral("start-here"),
                                     QStringLiteral("recheck"),
                                     QStringLiteral("reannounce"),
                                     QStringLiteral("dismiss"),
                                     QStringLiteral("locate"),
                                     QStringLiteral("move"),
                                     QStringLiteral("remove")};
    return actions;
}

Plan usage(const QString &words)
{
    Plan plan;
    plan.usage = words;
    return plan;
}

QString lineUsage(qsizetype index, const QString &words)
{
    return QStringLiteral("omaseed: do --plan, line %1: %2").arg(index + 1).arg(words);
}

} // namespace

Plan parse(const QString &text)
{
    const QString shape = QStringLiteral(
        "omaseed: do --plan takes a JSON plan: {\"v\": 1, \"lines\": [{\"action\": \"recheck\", "
        "\"torrents\": [\"3f9a1c0e\"]}]}. omaseed help do says how.");
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return usage(shape);
    const QJsonObject object = document.object();
    Plan plan;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("v")) {
            if (!it.value().isDouble() || it.value().toDouble() != 1)
                return usage(QStringLiteral("omaseed: do --plan reads plans of version 1 "
                                            "(\"v\": 1)."));
        } else if (key == QLatin1String("server")) {
            if (!it.value().isString() || it.value().toString().trimmed().isEmpty())
                return usage(QStringLiteral("omaseed: do --plan: \"server\" is a server's name "
                                            "or id, as omaseed status lists them."));
            plan.server = it.value().toString().trimmed();
        } else if (key != QLatin1String("lines")) {
            return usage(
                QStringLiteral(
                    "omaseed: do --plan: a plan doesn't take \"%1\"; it has v, server and lines.")
                    .arg(key));
        }
    }
    const QJsonValue lines = object.value(QLatin1String("lines"));
    if (!lines.isArray() || lines.toArray().isEmpty())
        return usage(shape);

    static const QRegularExpression hashLike(QStringLiteral("^[0-9a-f]{8,64}$"));
    const QJsonArray array = lines.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject())
            return usage(lineUsage(i, QStringLiteral("a line is an object with an action.")));
        const QJsonObject entry = array.at(i).toObject();
        Line line;
        bool hasTorrents = false;
        for (auto it = entry.constBegin(); it != entry.constEnd(); ++it) {
            const QString key = it.key();
            const QJsonValue value = it.value();
            if (key == QLatin1String("torrents")) {
                hasTorrents = true;
                if (!value.isArray())
                    return usage(lineUsage(i, QStringLiteral("torrents is a list of hashes.")));
                for (const QJsonValue &hash : value.toArray()) {
                    const QString given = hash.toString().trimmed().toLower();
                    if (!hash.isString() || !hashLike.match(given).hasMatch())
                        return usage(lineUsage(
                            i,
                            QStringLiteral("“%1” isn't a torrent's hash, or 8 or more of its "
                                           "first characters: omaseed errors gives them.")
                                .arg(hash.isString() ? hash.toString()
                                                     : QStringLiteral("a value that isn't text"))));
                    if (!line.torrents.contains(given))
                        line.torrents << given;
                }
            } else if (key == QLatin1String("action") || key == QLatin1String("to") ||
                       key == QLatin1String("move")) {
                if (!value.isString() || value.toString().trimmed().isEmpty())
                    return usage(
                        lineUsage(i, QStringLiteral("a line doesn't take \"%1\".").arg(key)));
                const QString text = value.toString().trimmed();
                (key == QLatin1String("action") ? line.action
                 : key == QLatin1String("to")   ? line.to
                                                : line.move) = text;
            } else {
                return usage(lineUsage(i, QStringLiteral("a line doesn't take \"%1\".").arg(key)));
            }
        }
        if (line.action.isEmpty())
            return usage(lineUsage(i, QStringLiteral("a line names its action.")));
        // C45 V14: kept, for the app to answer never (the plan refused, not malformed).
        if (AgentWords::isNeverWord(line.action)) {
            plan.lines << line;
            continue;
        }
        const bool withTorrents = torrentActions().contains(line.action);
        const bool serverWide =
            line.action == QLatin1String("pause") || line.action == QLatin1String("resume");
        const bool back = line.action == QLatin1String("move-back");
        if (!withTorrents && !serverWide && !back)
            return usage(lineUsage(i, QStringLiteral("do doesn't take \"%1\".").arg(line.action)));
        const bool withFolder =
            line.action == QLatin1String("locate") || line.action == QLatin1String("move");
        if (withFolder && line.to.isEmpty())
            return usage(
                lineUsage(i, QStringLiteral("%1 takes \"to\", the folder.").arg(line.action)));
        if (!withFolder && !line.to.isEmpty())
            return usage(lineUsage(i, QStringLiteral("%1 takes no \"to\".").arg(line.action)));
        if (back != !line.move.isEmpty())
            return usage(lineUsage(i,
                                   back
                                       ? QStringLiteral("move-back takes \"move\", one move's "
                                                        "id: omaseed moves --json gives them.")
                                       : QStringLiteral("%1 takes no \"move\".").arg(line.action)));
        if (withTorrents && line.torrents.isEmpty())
            return usage(
                lineUsage(i, QStringLiteral("%1 takes its torrents' hashes.").arg(line.action)));
        if ((serverWide || back) && hasTorrents)
            return usage(lineUsage(i,
                                   serverWide ? QStringLiteral("%1 takes no torrents: it holds the "
                                                               "whole server.")
                                                    .arg(line.action)
                                              : QStringLiteral("move-back takes no torrents, "
                                                               "only the move's id.")));
        plan.lines << line;
    }
    return plan;
}

QJsonObject options(const Line &line, const QString &server, bool dryRun)
{
    QJsonObject out{{QStringLiteral("action"), line.action}};
    if (!line.torrents.isEmpty())
        out.insert(QStringLiteral("torrents"), QJsonArray::fromStringList(line.torrents));
    if (!line.to.isEmpty())
        out.insert(QStringLiteral("to"), line.to);
    if (!line.move.isEmpty())
        out.insert(QStringLiteral("move"), line.move);
    if (!server.isEmpty())
        out.insert(QStringLiteral("server"), server);
    if (dryRun)
        out.insert(QStringLiteral("dryRun"), true);
    return out;
}

QList<Group> groups(const QList<Line> &lines, const QList<bool> &asks)
{
    QList<Group> out;
    QHash<QString, qsizetype> at;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const bool asking = asks.value(i);
        const Line &line = lines.at(i);
        const QString to = asking ? line.to : QString();
        const QString key = (asking ? QStringLiteral("a\n") : QStringLiteral("r\n")) + line.action +
                            QLatin1Char('\n') + to;
        if (const auto found = at.constFind(key); found != at.constEnd()) {
            out[found.value()].lines << int(i);
            continue;
        }
        at.insert(key, out.size());
        Group group;
        group.action = line.action;
        group.to = to;
        group.asks = asking;
        group.lines << int(i);
        out << group;
    }
    return out;
}

QString actionTitle(const QString &action)
{
    if (action == QLatin1String("move-back"))
        return QStringLiteral("Move back");
    if (action == QLatin1String("start-here"))
        return QStringLiteral("Start here");
    return action.left(1).toUpper() + action.mid(1);
}

QString doneLine(int done, int of, int rechecking)
{
    QString line = QStringLiteral("Done: %1 of %2").arg(done).arg(of);
    if (rechecking > 0)
        line += QStringLiteral(" · %1 rechecking").arg(rechecking);
    return line;
}

QString outcome(int done, int of, int rechecking)
{
    return doneLine(done, of, rechecking) + QStringLiteral(" · by an agent");
}

} // namespace omaseed::core::AgentPlan
