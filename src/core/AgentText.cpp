#include "core/AgentText.hpp"

#include "core/Format.hpp"
#include "core/ServerLog.hpp"
#include "core/TorrentStory.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>

namespace omaseed::core::AgentText {

namespace {

enum class Colour { Red = 31, Green = 32, Yellow = 33, Blue = 34, Bold = 1 };

QString paint(const QString &text, Colour colour, const TextStyle &style)
{
    if (!style.colour || text.isEmpty())
        return text;
    return QStringLiteral("\x1b[%1m").arg(int(colour)) + text + QStringLiteral("\x1b[0m");
}

QDateTime nowOf(const TextStyle &style)
{
    return (style.now.isValid() ? style.now : QDateTime::currentDateTime()).toLocalTime();
}

/// An answer's time ("2026-11-12T14:05:40Z") in TorrentStory's words; "" when null.
QString when(const QJsonValue &iso, const TextStyle &style)
{
    const QDateTime at = QDateTime::fromString(iso.toString(), Qt::ISODate);
    if (!at.isValid())
        return QString();
    return TorrentStory::timeWords(at.toLocalTime(), nowOf(style));
}

/// "1 torrent" / "2 torrents": a one/many pair, C41's plain kind (no `%n`).
QString count(qint64 n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

QString torrents(qint64 n)
{
    return count(n, QStringLiteral("torrent"), QStringLiteral("torrents"));
}

/// The rail's five questions (C24 H2) in two rows: what wants a look, then what is calm.
QString questionLines(const QJsonObject &questions)
{
    const auto n = [&](const char *key) { return questions.value(QLatin1String(key)).toInteger(); };
    QStringList act;
    if (n("needsYou") > 0)
        act << count(n("needsYou"), QStringLiteral("needs you"), QStringLiteral("need you"));
    if (n("arriving") > 0)
        act << QStringLiteral("%1 arriving").arg(n("arriving"));
    if (n("landedToday") > 0)
        act << QStringLiteral("%1 landed today").arg(n("landedToday"));
    QStringList calm;
    if (n("givingBack") > 0)
        calm << QStringLiteral("%1 giving back").arg(n("givingBack"));
    if (n("resting") > 0)
        calm << QStringLiteral("%1 resting").arg(n("resting"));

    const QString head = QStringLiteral("  ") + torrents(n("all"));
    const QString separator = QStringLiteral(" · ");
    if (act.isEmpty() && calm.isEmpty())
        return head + QLatin1Char('\n');
    if (act.isEmpty() || calm.isEmpty())
        return head + QStringLiteral(": ") + (act + calm).join(separator) + QLatin1Char('\n');
    return head + QStringLiteral(": ") + act.join(separator) + QLatin1Char('\n') +
           QString(head.size() + 2, QLatin1Char(' ')) + calm.join(separator) + QLatin1Char('\n');
}

/// "also seedbox · Transmission · 38 torrents, seen yesterday 21:10".
QString otherServerLine(const QJsonObject &server, const TextStyle &style)
{
    const QString name = server.value(QLatin1String("name")).toString();
    const QString engine = engineName(server.value(QLatin1String("engine")).toString());
    const QString state = server.value(QLatin1String("state")).toString();
    const qint64 seenCount = server.value(QLatin1String("count")).toInteger(-1);
    const QString seen = when(server.value(QLatin1String("seen")), style);

    QString tail;
    if (state == QLatin1String("down")) {
        tail = paint(QStringLiteral("unreachable"), Colour::Red, style) +
               QStringLiteral(" when last tried");
        if (!seen.isEmpty())
            tail += QStringLiteral(", ") + seen;
    } else if (state == QLatin1String("refused")) {
        const QString reason = server.value(QLatin1String("reason")).toString();
        tail = paint(reason.isEmpty() ? QStringLiteral("refused") : reason, Colour::Red, style) +
               QStringLiteral(" when last tried");
        if (!seen.isEmpty())
            tail += QStringLiteral(", ") + seen;
    } else if (seen.isEmpty()) {
        tail = QStringLiteral("never seen");
    } else {
        tail = seenCount >= 0 ? torrents(seenCount) + QStringLiteral(", seen ") + seen
                              : QStringLiteral("seen ") + seen;
    }
    return QStringLiteral("  also %1 · %2 · %3\n").arg(name, engine, tail);
}

QString speedLine(const QJsonObject &answer)
{
    const QJsonObject speed = answer.value(QLatin1String("speed")).toObject();
    const QString pick =
        answer.value(QLatin1String("alt")).toObject().value(QLatin1String("pick")).toString();
    const QString limits = pick.isEmpty() || pick == QLatin1String("off")
                               ? QStringLiteral("alternative limits off")
                               : QStringLiteral("alternative limits on (%1)").arg(pick);
    return QStringLiteral("  ↓ %1   ↑ %2   %3\n")
        .arg(Format::speed(speed.value(QLatin1String("down")).toInteger()),
             Format::speed(speed.value(QLatin1String("up")).toInteger()),
             limits);
}

QString moveLine(const QJsonObject &moves, const TextStyle &style)
{
    const QJsonObject now = moves.value(QLatin1String("now")).toObject();
    if (moves.value(QLatin1String("running")).toInt() <= 0 || now.isEmpty())
        return QString();
    QString line = QStringLiteral("  ") +
                   paint(QStringLiteral("1 move running"), Colour::Blue, style) +
                   QStringLiteral(": %1 → %2, %3 of %4 there")
                       .arg(now.value(QLatin1String("title")).toString(),
                            now.value(QLatin1String("to")).toString())
                       .arg(now.value(QLatin1String("there")).toInt())
                       .arg(now.value(QLatin1String("of")).toInt());
    const int queued = moves.value(QLatin1String("queued")).toInt();
    if (queued > 0)
        line += QStringLiteral(" · ") +
                count(queued, QStringLiteral("more waits"), QStringLiteral("more wait"));
    return line + QLatin1Char('\n');
}

} // namespace

QString wrap(const QString &text, int width, const QString &first, const QString &rest)
{
    QString out;
    QString line = first;
    bool empty = true; // nothing but the prefix on the line yet
    for (const QString &word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!empty && line.size() + 1 + word.size() > width) {
            out += line + QLatin1Char('\n');
            line = rest;
            empty = true;
        }
        if (!empty)
            line += QLatin1Char(' ');
        line += word;
        empty = false;
    }
    return out + line + QLatin1Char('\n');
}

QString engineName(const QString &engine)
{
    if (engine == QLatin1String("qbittorrent"))
        return QStringLiteral("qBittorrent");
    if (engine == QLatin1String("transmission"))
        return QStringLiteral("Transmission");
    if (engine == QLatin1String("deluge"))
        return QStringLiteral("Deluge");
    return engine;
}

QString failure(const QString &message, const TextStyle &style)
{
    return wrap(message, style.width, QString(), QString());
}

QString status(const QJsonObject &answer, const TextStyle &style)
{
    const QJsonObject server = answer.value(QLatin1String("server")).toObject();
    const QString name = server.value(QLatin1String("name")).toString();
    const QString engine = engineName(server.value(QLatin1String("engine")).toString());
    const bool reachable = server.value(QLatin1String("reachable")).toBool();
    const bool hidden = answer.value(QLatin1String("window")).toString() == QLatin1String("hidden");
    const QJsonArray servers = answer.value(QLatin1String("servers")).toArray();
    QJsonObject current;
    for (const QJsonValue &value : servers)
        if (value.toObject().value(QLatin1String("current")).toBool())
            current = value.toObject();

    QString out;
    QStringList head{
        paint(name.isEmpty() ? QStringLiteral("no server") : name, Colour::Bold, style)};
    if (reachable) {
        const QString version = server.value(QLatin1String("version")).toString();
        head << (version.isEmpty() ? engine : engine + QLatin1Char(' ') + version);
        const QString connection = server.value(QLatin1String("connection")).toString();
        head << paint(connection,
                      connection == QLatin1String("connected") ? Colour::Green : Colour::Yellow,
                      style);
    } else {
        if (!engine.isEmpty())
            head << engine;
        const QString state = current.value(QLatin1String("state")).toString();
        const bool problem = server.value(QLatin1String("problem")).isObject();
        if (state == QLatin1String("first-sync") && !problem)
            head << QStringLiteral("connecting");
        else if (state == QLatin1String("refused"))
            head << paint(QStringLiteral("refused"), Colour::Red, style);
        else
            head << paint(QStringLiteral("unreachable"), Colour::Red, style);
    }
    if (hidden)
        head << QStringLiteral("window hidden");
    out += head.join(QStringLiteral(" · ")) + QLatin1Char('\n');

    const QJsonObject questions = answer.value(QLatin1String("questions")).toObject();
    if (reachable) {
        out += speedLine(answer);
        out += questionLines(questions);
    } else {
        const QJsonObject problem = server.value(QLatin1String("problem")).toObject();
        const QString line = problem.value(QLatin1String("line")).toString();
        if (!line.isEmpty())
            out += wrap(line, style.width, QStringLiteral("  "), QStringLiteral("  "));
        const int retryIn = problem.value(QLatin1String("retryIn")).toInt();
        if (retryIn > 0)
            out += QStringLiteral("  omaseed tries again in %1 s.\n").arg(retryIn);
        const qint64 lastCount = current.value(QLatin1String("count")).toInteger(-1);
        const QString seen = when(current.value(QLatin1String("seen")), style);
        if (lastCount >= 0 && !seen.isEmpty())
            out += QStringLiteral("  %1 when last seen, %2\n").arg(torrents(lastCount), seen);
    }
    out += moveLine(answer.value(QLatin1String("moves")).toObject(), style);
    const qint64 paused = answer.value(QLatin1String("paused")).toInteger();
    if (paused > 0)
        out += QStringLiteral("  %1 paused by Pause Torrents: Resume Torrents starts them again\n")
                   .arg(paused);
    for (const QJsonValue &value : servers)
        if (!value.toObject().value(QLatin1String("current")).toBool())
            out += otherServerLine(value.toObject(), style);
    if (reachable && questions.value(QLatin1String("needsYou")).toInt() > 0)
        out += QStringLiteral("omaseed errors says what needs you.\n");
    return out;
}

QString errors(const QJsonObject &answer, const TextStyle &style)
{
    const QJsonObject server = answer.value(QLatin1String("server")).toObject();
    const QString name = server.value(QLatin1String("name")).toString();
    const QJsonObject problem = server.value(QLatin1String("problem")).toObject();
    const QJsonArray torrents = answer.value(QLatin1String("torrents")).toArray();

    QStringList head{
        paint(name.isEmpty() ? QStringLiteral("no server") : name, Colour::Bold, style)};
    // The server's own problem comes first (C45 V9); what is held of its torrents follows.
    if (!problem.isEmpty())
        head << paint(QStringLiteral("unreachable"), Colour::Red, style);
    if (!torrents.isEmpty())
        head << paint(
            count(torrents.size(), QStringLiteral("needs you"), QStringLiteral("need you")),
            Colour::Red,
            style);
    else if (problem.isEmpty())
        head << paint(QStringLiteral("nothing needs you."), Colour::Green, style);
    QString out = head.join(QStringLiteral(" · ")) + QLatin1Char('\n');
    if (!problem.isEmpty()) {
        const QString line = problem.value(QLatin1String("line")).toString();
        if (!line.isEmpty())
            out += wrap(line, style.width, QStringLiteral("  "), QStringLiteral("  "));
        const int retryIn = problem.value(QLatin1String("retryIn")).toInt();
        if (retryIn > 0)
            out += QStringLiteral("  omaseed tries again in %1 s.\n").arg(retryIn);
    }

    // The facts line up one column past the longest name, as the board's mock.
    qsizetype column = 0;
    for (const QJsonValue &value : torrents)
        column = std::max(column, value.toObject().value(QLatin1String("name")).toString().size());
    column += 2;

    for (const QJsonValue &value : torrents) {
        const QJsonObject torrent = value.toObject();
        const QString torrentName = torrent.value(QLatin1String("name")).toString();
        const QString kind = torrent.value(QLatin1String("kind")).toString();
        // C2 D1: a failure is yellow, everything else that needs you red.
        const Colour colour = kind == QLatin1String("failure") ? Colour::Yellow : Colour::Red;
        out += QLatin1Char('\n') + paint(torrentName, Colour::Bold, style) +
               QString(column - torrentName.size(), QLatin1Char(' ')) +
               paint(torrent.value(QLatin1String("fact")).toString(), colour, style) +
               QLatin1Char('\n');
        const QString sentence = torrent.value(QLatin1String("sentence")).toString();
        if (!sentence.isEmpty())
            out += wrap(sentence, style.width, QStringLiteral("  "), QStringLiteral("  "));
        QStringList meta;
        const QString since = when(torrent.value(QLatin1String("since")), style);
        if (!since.isEmpty())
            meta << QStringLiteral("since ") + since;
        const QString fix =
            torrent.value(QLatin1String("fix")).toObject().value(QLatin1String("words")).toString();
        if (!fix.isEmpty())
            meta << QStringLiteral("fix: ") + fix;
        if (!meta.isEmpty())
            out += QStringLiteral("  ") + meta.join(QStringLiteral(" · ")) + QLatin1Char('\n');
        for (const QJsonValue &logValue : torrent.value(QLatin1String("log")).toArray()) {
            const QJsonObject line = logValue.toObject();
            const QDateTime at =
                QDateTime::fromString(line.value(QLatin1String("at")).toString(), Qt::ISODate);
            const QString first =
                QStringLiteral("  log %1  ")
                    .arg(ServerLog::timeText(at.toSecsSinceEpoch(), nowOf(style)));
            out += wrap(line.value(QLatin1String("message")).toString(),
                        style.width,
                        first,
                        QString(first.size(), QLatin1Char(' ')));
        }
    }

    const qint64 dismissed = answer.value(QLatin1String("dismissed")).toInteger();
    if (dismissed > 0)
        out += QLatin1Char('\n') +
               (dismissed == 1
                    ? QStringLiteral("1 dismissed in the window is left out.\n")
                    : QStringLiteral("%1 dismissed in the window are left out.\n").arg(dismissed));
    return out;
}

QString log(const QJsonObject &answer, const TextStyle &style)
{
    const QJsonObject server = answer.value(QLatin1String("server")).toObject();
    const QString name = server.value(QLatin1String("name")).toString();
    const QString level = answer.value(QLatin1String("level")).toString();
    const QJsonArray lines = answer.value(QLatin1String("lines")).toArray();
    const qint64 matched = answer.value(QLatin1String("matched")).toInteger();

    QString levels;
    if (level == QLatin1String("normal"))
        levels = QStringLiteral("every level");
    else if (level == QLatin1String("critical"))
        levels = QStringLiteral("critical");
    else if (level == QLatin1String("info"))
        levels = QStringLiteral("info and worse");
    else
        levels = QStringLiteral("warnings and worse");
    QString which;
    const qint64 next = answer.value(QLatin1String("next")).toInteger();
    if (lines.isEmpty())
        which = next > 0 ? QStringLiteral("nothing new since %1").arg(next)
                         : QStringLiteral("no lines");
    else if (lines.size() < matched)
        which = QStringLiteral("the last %1 of %2").arg(lines.size()).arg(matched);
    else
        which = count(matched, QStringLiteral("line"), QStringLiteral("lines"));
    QString out =
        QStringList{paint(name.isEmpty() ? QStringLiteral("no server") : name, Colour::Bold, style),
                    QStringLiteral("qBittorrent's log"),
                    levels,
                    which}
            .join(QStringLiteral(" · ")) +
        QLatin1Char('\n');

    // C3 D15: where qBittorrent's memory of its log begins.
    const QString oldest = when(answer.value(QLatin1String("oldest")), style);
    if (!oldest.isEmpty())
        out += answer.value(QLatin1String("fromStart")).toBool()
                   ? QStringLiteral("qBittorrent's memory of its log begins at its start, %1.\n")
                         .arg(oldest)
                   : QStringLiteral("qBittorrent keeps its last 20 000 lines in memory; these "
                                    "start %1.\n")
                         .arg(oldest);

    // The time, the level in its colour (C3's chips), the message wrapped under itself.
    for (const QJsonValue &value : lines) {
        const QJsonObject line = value.toObject();
        const QDateTime at =
            QDateTime::fromString(line.value(QLatin1String("at")).toString(), Qt::ISODate);
        const QString lineLevel = line.value(QLatin1String("level")).toString();
        const QString time = ServerLog::timeText(at.toSecsSinceEpoch(), nowOf(style));
        const QString padded = lineLevel.leftJustified(8, QLatin1Char(' '));
        const QString first = time + QLatin1Char(' ') + padded + QLatin1Char(' ');
        QString block = wrap(line.value(QLatin1String("message")).toString(),
                             style.width,
                             first,
                             QString(first.size(), QLatin1Char(' ')));
        if (style.colour &&
            (lineLevel == QLatin1String("critical") || lineLevel == QLatin1String("warning"))) {
            const Colour colour =
                lineLevel == QLatin1String("critical") ? Colour::Red : Colour::Yellow;
            block.replace(time.size() + 1, padded.size(), paint(padded, colour, style));
        }
        out += block;
    }
    return out;
}

QString lines(const QJsonObject &answer, const TextStyle &style)
{
    const QString command = answer.value(QLatin1String("command")).toString();
    if (command == QLatin1String("status"))
        return status(answer, style);
    if (command == QLatin1String("errors"))
        return errors(answer, style);
    if (command == QLatin1String("log"))
        return log(answer, style);
    return QString();
}

} // namespace omaseed::core::AgentText
