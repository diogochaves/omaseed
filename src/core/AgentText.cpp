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
    // DECISIONS C45: a problems-only read cannot see where the memory begins (fromStart null).
    const QJsonValue fromStart = answer.value(QLatin1String("fromStart"));
    if (!oldest.isEmpty() && fromStart.isNull())
        out +=
            QStringLiteral("The oldest of these qBittorrent still keeps is from %1.\n").arg(oldest);
    else if (!oldest.isEmpty())
        out += fromStart.toBool()
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

namespace {

/// The column a move's time starts at, as the board's mock lines them up.
constexpr qsizetype kMoveTimeColumn = 48;
const QString kMoveIndent(9, QLatin1Char(' '));

/// "today 12:10" for today, TorrentStory's words for any other day.
QString landedWhen(const QJsonValue &iso, const TextStyle &style)
{
    const QDateTime at = QDateTime::fromString(iso.toString(), Qt::ISODate);
    if (!at.isValid())
        return QString();
    const QString words = when(iso, style);
    return at.toLocalTime().date() == nowOf(style).date() ? QStringLiteral("today ") + words
                                                          : words;
}

/// "running  games → /mnt/fast/games      since 13:40": the label in its colour, the title
/// bold, the time at its column (on a line of its own when the head reaches it).
QString moveHead(const QString &label,
                 Colour colour,
                 const QJsonObject &move,
                 const QString &time,
                 const TextStyle &style)
{
    const QString title = move.value(QLatin1String("title")).toString();
    const QString to = move.value(QLatin1String("to")).toString();
    const QString tail = to.isEmpty() ? QString() : QStringLiteral(" → ") + to;
    const qsizetype plain = kMoveIndent.size() + title.size() + tail.size();
    // The title starts at the indent's column: "carrying" is the one label of 8.
    const QString padded = label.leftJustified(kMoveIndent.size() - 2, QLatin1Char(' '));
    QString out =
        paint(padded, colour, style) +
        QString(std::max<qsizetype>(1, kMoveIndent.size() - padded.size()), QLatin1Char(' ')) +
        paint(title, Colour::Bold, style) + tail;
    if (time.isEmpty())
        return out + QLatin1Char('\n');
    if (plain + 2 > kMoveTimeColumn)
        return out + QLatin1Char('\n') + QString(kMoveTimeColumn, QLatin1Char(' ')) + time +
               QLatin1Char('\n');
    return out + QString(kMoveTimeColumn - plain, QLatin1Char(' ')) + time + QLatin1Char('\n');
}

/// "20.0 of 48.0 GB", or "800 MB of 48.0 GB" when the units differ.
QString bytesOf(qint64 there, qint64 of)
{
    const QString done = Format::size(there);
    const QString all = Format::size(of);
    const qsizetype space = done.lastIndexOf(QLatin1Char(' '));
    if (space > 0 && all.endsWith(done.mid(space)))
        return done.left(space) + QStringLiteral(" of ") + all;
    return done + QStringLiteral(" of ") + all;
}

/// C18 D7's move back, from MoveHistory::check's counts: "all 3 go back", "it stays; moved
/// outside omaseed", parts joined with " · ".
QString backWords(const QJsonObject &back, const QString &serverName, const TextStyle &style)
{
    if (back.isEmpty())
        return QStringLiteral("known once omaseed is on %1").arg(serverName);
    const qint64 goBack = back.value(QLatin1String("goBack")).toInteger();
    const qint64 again = back.value(QLatin1String("movedAgain")).toInteger();
    const qint64 outside = back.value(QLatin1String("movedOutside")).toInteger();
    const qint64 gone = back.value(QLatin1String("gone")).toInteger();
    const QJsonArray by = back.value(QLatin1String("movedAgainBy")).toArray();
    const QString byWords =
        by.size() == 1
            ? QStringLiteral("moved again by %1's move")
                  .arg(landedWhen(by.at(0).toObject().value(QLatin1String("finished")), style))
            : QStringLiteral("moved again by later moves");
    const qint64 all = goBack + again + outside + gone;
    if (all == 1) {
        if (goBack == 1)
            return QStringLiteral("it goes back");
        if (again == 1)
            return QStringLiteral("it stays; ") + byWords;
        if (outside == 1)
            return QStringLiteral("it stays; moved outside omaseed");
        return QStringLiteral("it's gone from the library");
    }
    if (all > 0 && goBack == all)
        return QStringLiteral("all %1 go back").arg(all);
    QStringList parts;
    if (goBack > 0)
        parts << count(goBack, QStringLiteral("goes back"), QStringLiteral("go back"));
    if (again > 0)
        parts << QStringLiteral("%1 %2; %3")
                     .arg(again)
                     .arg(again == 1 ? QStringLiteral("stays") : QStringLiteral("stay"), byWords);
    if (outside > 0)
        parts << count(outside,
                       QStringLiteral("stays; moved outside omaseed"),
                       QStringLiteral("stay; moved outside omaseed"));
    if (gone > 0)
        parts << count(gone,
                       QStringLiteral("is gone from the library"),
                       QStringLiteral("are gone from the library"));
    return parts.isEmpty() ? QStringLiteral("nothing left to move back")
                           : parts.join(QStringLiteral(" · "));
}

} // namespace

QString moves(const QJsonObject &answer, const TextStyle &style)
{
    const QJsonObject server = answer.value(QLatin1String("server")).toObject();
    const QString name = server.value(QLatin1String("name")).toString();
    const QJsonArray running = answer.value(QLatin1String("running")).toArray();
    const QJsonArray landed = answer.value(QLatin1String("landed")).toArray();
    const qint64 kept = answer.value(QLatin1String("kept")).toInteger();

    QString out =
        QStringList{paint(name.isEmpty() ? QStringLiteral("no server") : name, Colour::Bold, style),
                    running.isEmpty() ? QStringLiteral("no move running")
                                      : QStringLiteral("%1 running").arg(running.size()),
                    kept > 0 ? QStringLiteral("%1 landed").arg(kept)
                             : QStringLiteral("none landed")}
            .join(QStringLiteral(" · ")) +
        QLatin1Char('\n');

    if (!running.isEmpty())
        out += QLatin1Char('\n');
    for (const QJsonValue &value : running) {
        const QJsonObject move = value.toObject();
        const QString state = move.value(QLatin1String("state")).toString();
        const bool paused = state == QLatin1String("paused");
        const QString since = when(move.value(QLatin1String("started")), style);
        out += moveHead(state,
                        paused ? Colour::Yellow : Colour::Blue,
                        move,
                        since.isEmpty() ? QString() : QStringLiteral("since ") + since,
                        style);
        const QJsonObject torrents = move.value(QLatin1String("torrents")).toObject();
        QStringList figures{QStringLiteral("%1 of %2 there")
                                .arg(torrents.value(QLatin1String("there")).toInt())
                                .arg(torrents.value(QLatin1String("of")).toInt())};
        const int failed = torrents.value(QLatin1String("failed")).toInt();
        if (failed > 0)
            figures << QStringLiteral("%1 failed").arg(failed);
        if (paused) {
            // The band's words (C15 T1–T4): why it paused; going on or ending stays the
            // window's (docs/62 §7).
            const QString line = move.value(QLatin1String("line")).toString();
            if (!line.isEmpty())
                out += wrap(line, style.width, kMoveIndent, kMoveIndent);
            figures << QStringLiteral("the window can go on or end it");
        } else {
            const QJsonObject bytes = move.value(QLatin1String("bytes")).toObject();
            const qint64 of = bytes.value(QLatin1String("of")).toInteger();
            if (of > 0)
                figures << bytesOf(bytes.value(QLatin1String("there")).toInteger(), of);
            const QString now =
                move.value(QLatin1String("now")).toObject().value(QLatin1String("name")).toString();
            if (!now.isEmpty())
                figures << QStringLiteral("moving ") + now;
        }
        const qsizetype queued = move.value(QLatin1String("queued")).toArray().size();
        if (queued > 0)
            figures << count(queued, QStringLiteral("more waits"), QStringLiteral("more wait"));
        out += wrap(figures.join(QStringLiteral(" · ")), style.width, kMoveIndent, kMoveIndent);
    }

    if (!landed.isEmpty())
        out += QLatin1Char('\n');
    for (const QJsonValue &value : landed) {
        const QJsonObject move = value.toObject();
        out += moveHead(QStringLiteral("landed"),
                        Colour::Green,
                        move,
                        landedWhen(move.value(QLatin1String("finished")), style),
                        style);
        out += wrap(QStringLiteral("move back: ") +
                        backWords(move.value(QLatin1String("back")).toObject(), name, style),
                    style.width,
                    kMoveIndent,
                    kMoveIndent);
    }
    if (kept > landed.size())
        out += QStringLiteral("… %1 more: omaseed moves --landed 20\n").arg(kept - landed.size());
    return out;
}

QString doAnswer(const QJsonObject &answer, const TextStyle &style)
{
    QString out;
    QString outcome = answer.value(QLatin1String("outcome")).toString();
    // docs/62 §5: "Allowed." shows only when it asked.
    if (answer.value(QLatin1String("asked")).toBool() &&
        answer.value(QLatin1String("answer")).toString() == QLatin1String("allowed"))
        outcome = QStringLiteral("Allowed. ") + outcome;
    const QJsonArray failed = answer.value(QLatin1String("failed")).toArray();
    const QJsonArray done = answer.value(QLatin1String("done")).toArray();
    const QString head = wrap(outcome, style.width, QString(), QStringLiteral("  "));
    out += failed.isEmpty()
               ? head
               : paint(head.chopped(1), done.isEmpty() ? Colour::Red : Colour::Yellow, style) +
                     QLatin1Char('\n');
    for (const QJsonValue &value : failed) {
        const QJsonObject torrent = value.toObject();
        const QString line =
            wrap(QStringLiteral("%1: %2").arg(torrent.value(QLatin1String("name")).toString(),
                                              torrent.value(QLatin1String("message")).toString()),
                 style.width,
                 QStringLiteral("  "),
                 QStringLiteral("    "));
        out += paint(line.chopped(1), Colour::Red, style) + QLatin1Char('\n');
    }
    return out;
}

QString doList(const QJsonObject &answer, const TextStyle &style)
{
    // C50 GT3: one line per kind, as the Agents row draws them.
    const auto said = [](const QString &kind, const QString &level) {
        if (kind == QLatin1String("reads"))
            return level == QLatin1String("off") ? QStringLiteral("agents get nothing")
                                                 : QStringLiteral("agents read the library");
        if (level == QLatin1String("allow"))
            return QStringLiteral("run at once");
        if (level == QLatin1String("ask"))
            return QStringLiteral("ask each time");
        return QStringLiteral("not allowed");
    };
    const auto spelled = [](const QString &action) {
        if (action == QLatin1String("move"))
            return QStringLiteral("move --to FOLDER");
        if (action == QLatin1String("move-back"))
            return QStringLiteral("move-back MOVE");
        return action;
    };
    QString out = paint(QStringLiteral("Settings › General › Agents"), Colour::Bold, style) +
                  QLatin1Char('\n');
    const QString indent(17, QLatin1Char(' '));
    for (const QJsonValue &value : answer.value(QLatin1String("lines")).toArray()) {
        const QJsonObject line = value.toObject();
        const QString kind = line.value(QLatin1String("kind")).toString();
        const QString level = line.value(QLatin1String("level")).toString();
        const QString name = kind.left(1).toUpper() + kind.mid(1);
        QStringList actions;
        for (const QJsonValue &action : line.value(QLatin1String("actions")).toArray())
            actions << spelled(action.toString());
        const QString levelWord = level.leftJustified(6);
        const QString painted =
            level == QLatin1String("allow") ? paint(levelWord, Colour::Green, style)
            : level == QLatin1String("ask") ? paint(levelWord, Colour::Yellow, style)
                                            : levelWord;
        out += QStringLiteral("  ") + name.leftJustified(9) + painted + said(kind, level) +
               QLatin1Char('\n');
        out += wrap(actions.join(QStringLiteral(" · ")), style.width, indent, indent);
    }
    QStringList never;
    for (const QJsonValue &value : answer.value(QLatin1String("never")).toArray())
        never << value.toString();
    if (!never.isEmpty())
        out += wrap(
            QStringLiteral("Never through omaseed do: %1.").arg(never.join(QStringLiteral(", "))),
            style.width,
            QString(),
            QString());
    return out;
}

QString doDryRun(const QJsonObject &answer, const TextStyle &style)
{
    // C50 GT5: say first, then do.
    const bool asks = answer.value(QLatin1String("would")).toString() == QLatin1String("ask");
    const QString line = wrap(QStringLiteral("Dry run, nothing done: ") +
                                  answer.value(QLatin1String("outcome")).toString(),
                              style.width,
                              QString(),
                              QStringLiteral("  "));
    return paint(line.chopped(1), asks ? Colour::Yellow : Colour::Green, style) + QLatin1Char('\n');
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
    if (command == QLatin1String("moves"))
        return moves(answer, style);
    if (command == QLatin1String("do")) {
        if (answer.value(QLatin1String("list")).toBool())
            return doList(answer, style);
        if (answer.value(QLatin1String("dryRun")).toBool())
            return doDryRun(answer, style);
        return doAnswer(answer, style);
    }
    return QString();
}

} // namespace omaseed::core::AgentText
