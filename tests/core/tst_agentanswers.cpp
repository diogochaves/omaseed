// DECISIONS C45 V4, V6, V7, V8 (#163): what the running omaseed answers, from plain inputs:
// the envelope, the options a method takes, --server's match, status's JSON and errors' (V9).

#include "core/AgentAnswers.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QTest>
#include <QTimeZone>

namespace Answers = omaseed::core::AgentAnswers;

namespace {

const QDateTime kAt(QDate(2026, 11, 12), QTime(14, 5, 40), QTimeZone::UTC);

Answers::StatusInput attic()
{
    Answers::StatusInput input;
    input.at = kAt;
    input.omaseed = QStringLiteral("0.14.0");
    input.server.id = QStringLiteral("{0b9e}");
    input.server.name = QStringLiteral("attic");
    input.server.engine = QStringLiteral("qbittorrent");
    input.server.version = QStringLiteral("5.3.0");
    input.server.address = QStringLiteral("192.168.1.20:8080");
    input.server.reachable = true;
    input.server.connection = QStringLiteral("connected");
    input.down = 4415221;
    input.up = 831488;
    input.all = 142;
    input.needsYou = 2;
    input.arriving = 6;
    input.landedToday = 3;
    input.givingBack = 118;
    input.resting = 13;
    input.movesRunning = 1;
    input.moveNow =
        Answers::MoveNow{QStringLiteral("games"), QStringLiteral("/mnt/fast/games"), 5, 12};
    Answers::ListedServer current;
    current.id = input.server.id;
    current.name = QStringLiteral("attic");
    current.engine = QStringLiteral("qbittorrent");
    current.current = true;
    current.state = QStringLiteral("ok");
    current.count = 142;
    current.seen = kAt;
    Answers::ListedServer seedbox;
    seedbox.id = QStringLiteral("{77c1}");
    seedbox.name = QStringLiteral("seedbox");
    seedbox.engine = QStringLiteral("transmission");
    seedbox.state = QStringLiteral("ok");
    seedbox.count = 38;
    seedbox.seen = QDateTime(QDate(2026, 11, 11), QTime(21, 10, 2), QTimeZone::UTC);
    input.servers = {current, seedbox};
    return input;
}

QJsonObject parse(const QByteArray &bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        qWarning() << error.errorString() << bytes;
    return document.object();
}

Answers::ErrorsInput errorsOn(const QString &engine)
{
    Answers::ErrorsInput input;
    input.at = kAt;
    input.omaseed = QStringLiteral("0.14.0");
    input.server = attic().server;
    input.server.engine = engine;
    return input;
}

Answers::ErrorTorrent lantern()
{
    Answers::ErrorTorrent torrent;
    torrent.hash = QStringLiteral("3f9a1c0e00000000000000000000000000000000");
    torrent.name = QStringLiteral("Lantern Festival Field Recordings");
    torrent.problem = true;
    torrent.problemCause = QStringLiteral("noAccess");
    torrent.fact = QStringLiteral("no access");
    torrent.sentence = QStringLiteral("qBittorrent can't write to /data/music/Lantern.");
    torrent.fixWords = QStringLiteral("move…");
    torrent.fixAction = QStringLiteral("problem:location");
    torrent.since = QDateTime(QDate(2026, 11, 12), QTime(14, 3, 11), QTimeZone::UTC);
    torrent.phase = omaseed::core::TorrentPhase::Stopped;
    torrent.savePath = QStringLiteral("/data/music");
    for (int id = 4117; id <= 4120; ++id)
        torrent.log.append({id,
                            torrent.since,
                            id == 4120 ? 8 : 4,
                            QStringLiteral("File error alert. Torrent: \"Lantern\". %1").arg(id)});
    return torrent;
}

QJsonObject firstTorrent(const Answers::ErrorsInput &input)
{
    return parse(Answers::errors(input))
        .value(QStringLiteral("torrents"))
        .toArray()
        .at(0)
        .toObject();
}

const QString kArcade = QStringLiteral("8be2d41700000000000000000000000000000000");
const QString kLantern = QStringLiteral("3f9a1c0e00000000000000000000000000000000");

/// A log of 100 lines, ids 0–99 a minute apart from 11 Nov 09:02:13: every fifth a warning,
/// every 25th critical (0 too), the other even ones info, the odd ones normal. 20 warnings
/// and worse, 60 info and worse, 4 critical. Line 45 names Arcade Set 01, line 50 Lantern by
/// its hash prefix (libtorrent 2.1).
Answers::LogInput logBoard()
{
    Answers::LogInput input;
    input.at = kAt;
    input.omaseed = QStringLiteral("0.14.0");
    input.server = attic().server;
    input.options = Answers::parseOptions(QStringLiteral("log"), QString()).options;
    const QDateTime start(QDate(2026, 11, 11), QTime(9, 2, 13), QTimeZone::UTC);
    for (int id = 0; id < 100; ++id) {
        const int level = id % 25 == 0 ? 8 : id % 5 == 0 ? 4 : id % 2 == 0 ? 2 : 1;
        QString message = QStringLiteral("UPnP/NAT-PMP: Port mapping failure. Line %1").arg(id);
        if (id == 45)
            message = QStringLiteral("Failed to move torrent. Torrent: \"Arcade Set 01\". "
                                     "Reason: \"No space left on device\"");
        if (id == 50)
            message = QStringLiteral("File error alert. Torrent: \"3f9a1c Lantern\". Reason: "
                                     "\"Permission denied\"");
        input.log.append({id, start.addSecs(60 * id), level, message});
    }
    input.library = {{kArcade, QStringLiteral("Arcade Set 01"), QStringLiteral("8be2d4")},
                     {kLantern, QStringLiteral("Lantern"), QStringLiteral("3f9a1c")}};
    return input;
}

QList<int> idsOf(const QJsonObject &answer)
{
    QList<int> ids;
    for (const QJsonValue &line : answer.value(QStringLiteral("lines")).toArray())
        ids << line.toObject().value(QStringLiteral("id")).toInt();
    return ids;
}

/// A folder job's journal (MoveJob::toJson's shape): games to /mnt/fast/games, 5 of 12 there,
/// Arcade Set 02 moving, one left out, "3 torrents" waiting behind it; 4 GiB each.
QJsonObject gamesJournal(const QString &state, const QString &pauseReason = QStringLiteral("None"))
{
    QJsonArray entries;
    const qint64 gib = qint64(4) << 30;
    for (int i = 0; i < 13; ++i) {
        QString step = i < 5 ? QStringLiteral("Moved") : QStringLiteral("Waiting");
        if (i == 5)
            step = QStringLiteral("Moving");
        if (i == 12)
            step = QStringLiteral("LeftOut");
        entries.append(QJsonObject{
            {QStringLiteral("hash"), QStringLiteral("c41e09aa%1").arg(i, 32, 10, QLatin1Char('0'))},
            {QStringLiteral("name"),
             QStringLiteral("Arcade Set %1").arg(i - 3, 2, 10, QLatin1Char('0'))},
            {QStringLiteral("from"), QStringLiteral("/mnt/archive/games")},
            {QStringLiteral("to"), QStringLiteral("/mnt/fast/games")},
            {QStringLiteral("bytes"), gib},
            {QStringLiteral("step"), step},
        });
    }
    return {
        {QStringLiteral("version"), 1},
        {QStringLiteral("server"), QStringLiteral("{0b9e}")},
        {QStringLiteral("spec"),
         QJsonObject{{QStringLiteral("kind"), QStringLiteral("folder")},
                     {QStringLiteral("title"), QStringLiteral("games")}}},
        {QStringLiteral("state"), state},
        {QStringLiteral("pauseReason"), pauseReason},
        {QStringLiteral("root"), QStringLiteral("/mnt/archive/games")},
        {QStringLiteral("newPath"), QStringLiteral("/mnt/fast/games")},
        {QStringLiteral("startedAt"), QStringLiteral("2026-11-12T13:40:05.000Z")},
        {QStringLiteral("entries"), entries},
        {QStringLiteral("queued"),
         QJsonArray{
             QJsonObject{{QStringLiteral("spec"),
                          QJsonObject{{QStringLiteral("title"), QStringLiteral("3 torrents")}}}}}},
    };
}

omaseed::core::MoveRecord record(const QString &id,
                                 const QString &title,
                                 const QString &to,
                                 const QDateTime &finished,
                                 const QHash<QString, QString> &from)
{
    omaseed::core::MoveRecord move;
    move.id = id;
    move.kind = QStringLiteral("selection");
    move.title = title;
    move.newPath = to;
    move.startedAt = finished.addSecs(-88);
    move.finishedAt = finished;
    move.from = from;
    for (auto it = from.constBegin(); it != from.constEnd(); ++it) {
        move.to.insert(it.key(), to);
        move.names.insert(it.key(), QStringLiteral("Track ") + it.key());
    }
    return move;
}

/// Two landed moves: m-1b20 (today 12:10) took h5 on from /data/archive to /data/music2;
/// m-09ae (yesterday 21:10) took h1–h4 and h5 from /data/incoming.
omaseed::core::MoveHistory twoMoves()
{
    omaseed::core::MoveHistory history;
    const QString incoming = QStringLiteral("/data/incoming");
    history.add(record(QStringLiteral("m-09ae"),
                       QStringLiteral("5 torrents"),
                       QStringLiteral("/data/archive"),
                       QDateTime(QDate(2026, 11, 11), QTime(21, 10, 19), QTimeZone::UTC),
                       {{QStringLiteral("h1"), incoming},
                        {QStringLiteral("h2"), incoming},
                        {QStringLiteral("h3"), incoming},
                        {QStringLiteral("h4"), incoming},
                        {QStringLiteral("h5"), incoming}}),
                kAt);
    history.add(record(QStringLiteral("m-1b20"),
                       QStringLiteral("Track h5"),
                       QStringLiteral("/data/music2"),
                       QDateTime(QDate(2026, 11, 12), QTime(12, 10, 19), QTimeZone::UTC),
                       {{QStringLiteral("h5"), QStringLiteral("/data/archive")}}),
                kAt);
    return history;
}

/// h1, h2 where m-09ae put them; h3 moved outside omaseed; h4 gone; h5 moved by m-1b20.
const QHash<QString, QString> kSavePaths{{QStringLiteral("h1"), QStringLiteral("/data/archive")},
                                         {QStringLiteral("h2"), QStringLiteral("/data/archive")},
                                         {QStringLiteral("h3"), QStringLiteral("/mnt/usb")},
                                         {QStringLiteral("h5"), QStringLiteral("/data/music2")}};

QJsonObject movesAnswer(const omaseed::core::MoveHistory &history,
                        const QHash<QString, QString> *savePaths,
                        int count = 5)
{
    Answers::MovesInput input;
    input.at = kAt;
    input.omaseed = QStringLiteral("0.14.0");
    input.server = attic().server;
    input.landed = Answers::landedOf(history, count, savePaths);
    input.kept = int(history.records().size());
    return parse(Answers::moves(input));
}

} // namespace

class TestAgentAnswers : public QObject
{
    Q_OBJECT

private slots:
    void envelopeFields();
    void timesAreUtcWithZ();
    void statusReachable();
    void statusUnreachableHasProblem();
    void serversCarryEngineAndIsoSeen();
    void listsAlwaysPresent();
    void optionsDefaultsAndRefusals();
    void serverMatching();

    void errorNoAccessQbt();
    void failureMoveHasACause();
    void stuckMove();
    void missingFiles();
    void transmissionEngineText();
    void delugePaused();
    void fixDoMapping();
    void urlsMasked();

    void logDefaultsWarningFifty();
    void logLevelAndWorse();
    void logSinceFromId();
    void logSinceBeyondIsRestarted();
    void logTorrentFilter();
    void logNextAfterNewest();
    void logFromStartAndOldest();

    void movesRunningFields();
    void movesPausedFor();
    void landedBackFromCheck();
    void landedMovedAgainBy();
    void anotherServerBackNull();
};

void TestAgentAnswers::envelopeFields()
{
    const QByteArray bytes = Answers::status(attic());
    // C45 V4: v, command, at, omaseed lead, in that order, then the answer's own fields.
    QVERIFY2(bytes.startsWith(R"({"v":1,"command":"status","at":"2026-11-12T14:05:40Z",)"
                              R"("omaseed":"0.14.0","window":"hidden","server":)"),
             bytes.constData());
    const QJsonObject object = parse(bytes);
    QCOMPARE(object.value(QLatin1String("v")).toInt(), 1);
    QVERIFY(!object.contains(QLatin1String("error")));
}

void TestAgentAnswers::timesAreUtcWithZ()
{
    Answers::StatusInput input = attic();
    // A local time is said in UTC.
    input.at = QDateTime(QDate(2026, 11, 12), QTime(16, 5, 40), QTimeZone(2 * 3600));
    input.servers[1].seen = QDateTime(); // never
    const QJsonObject object = parse(Answers::status(input));
    QCOMPARE(object.value(QLatin1String("at")).toString(), QStringLiteral("2026-11-12T14:05:40Z"));
    const QJsonArray servers = object.value(QLatin1String("servers")).toArray();
    QCOMPARE(servers.at(0).toObject().value(QLatin1String("seen")).toString(),
             QStringLiteral("2026-11-12T14:05:40Z"));
    QVERIFY(servers.at(1).toObject().value(QLatin1String("seen")).isNull());
}

void TestAgentAnswers::statusReachable()
{
    const QJsonObject object = parse(Answers::status(attic()));
    QCOMPARE(object.value(QLatin1String("window")).toString(), QStringLiteral("hidden"));
    const QJsonObject server = object.value(QLatin1String("server")).toObject();
    QCOMPARE(server.value(QLatin1String("id")).toString(), QStringLiteral("{0b9e}"));
    QCOMPARE(server.value(QLatin1String("name")).toString(), QStringLiteral("attic"));
    QCOMPARE(server.value(QLatin1String("engine")).toString(), QStringLiteral("qbittorrent"));
    QCOMPARE(server.value(QLatin1String("version")).toString(), QStringLiteral("5.3.0"));
    QCOMPARE(server.value(QLatin1String("address")).toString(),
             QStringLiteral("192.168.1.20:8080"));
    QCOMPARE(server.value(QLatin1String("reachable")).toBool(), true);
    QCOMPARE(server.value(QLatin1String("connection")).toString(), QStringLiteral("connected"));
    QVERIFY(server.value(QLatin1String("problem")).isNull());

    const QJsonObject speed = object.value(QLatin1String("speed")).toObject();
    QCOMPARE(speed.value(QLatin1String("down")).toInteger(), 4415221);
    QCOMPARE(speed.value(QLatin1String("up")).toInteger(), 831488);
    QCOMPARE(object.value(QLatin1String("alt")).toObject().value(QLatin1String("pick")).toString(),
             QStringLiteral("off"));
    const QJsonObject questions = object.value(QLatin1String("questions")).toObject();
    QCOMPARE(questions.value(QLatin1String("all")).toInt(), 142);
    QCOMPARE(questions.value(QLatin1String("needsYou")).toInt(), 2);
    QCOMPARE(questions.value(QLatin1String("arriving")).toInt(), 6);
    QCOMPARE(questions.value(QLatin1String("landedToday")).toInt(), 3);
    QCOMPARE(questions.value(QLatin1String("givingBack")).toInt(), 118);
    QCOMPARE(questions.value(QLatin1String("resting")).toInt(), 13);
    const QJsonObject moves = object.value(QLatin1String("moves")).toObject();
    QCOMPARE(moves.value(QLatin1String("running")).toInt(), 1);
    QCOMPARE(moves.value(QLatin1String("queued")).toInt(), 0);
    const QJsonObject now = moves.value(QLatin1String("now")).toObject();
    QCOMPARE(now.value(QLatin1String("title")).toString(), QStringLiteral("games"));
    QCOMPARE(now.value(QLatin1String("to")).toString(), QStringLiteral("/mnt/fast/games"));
    QCOMPARE(now.value(QLatin1String("there")).toInt(), 5);
    QCOMPARE(now.value(QLatin1String("of")).toInt(), 12);
    QCOMPARE(object.value(QLatin1String("paused")).toInt(), 0);
}

void TestAgentAnswers::statusUnreachableHasProblem()
{
    Answers::StatusInput input = attic();
    input.server.reachable = false;
    input.server.connection = QStringLiteral("disconnected");
    // C45 V7: a URL in the words is masked, its path and query (a passkey) gone.
    input.server.problem =
        Answers::Problem{QStringLiteral("attic is unreachable"),
                         QStringLiteral("Connection refused at 192.168.1.20:8080. See "
                                        "https://tracker.example/announce?passkey=s3cr3tpass"),
                         12};
    const QByteArray bytes = Answers::status(input);
    QVERIFY(!bytes.contains("s3cr3tpass"));
    const QJsonObject server = parse(bytes).value(QLatin1String("server")).toObject();
    QCOMPARE(server.value(QLatin1String("reachable")).toBool(), false);
    QCOMPARE(server.value(QLatin1String("connection")).toString(), QStringLiteral("disconnected"));
    const QJsonObject problem = server.value(QLatin1String("problem")).toObject();
    QCOMPARE(problem.value(QLatin1String("headline")).toString(),
             QStringLiteral("attic is unreachable"));
    QVERIFY(problem.value(QLatin1String("line"))
                .toString()
                .startsWith(QLatin1String("Connection refused at 192.168.1.20:8080.")));
    QCOMPARE(problem.value(QLatin1String("retryIn")).toInt(), 12);
}

void TestAgentAnswers::serversCarryEngineAndIsoSeen()
{
    const QJsonArray servers =
        parse(Answers::status(attic())).value(QLatin1String("servers")).toArray();
    QCOMPARE(servers.size(), 2);
    const QJsonObject seedbox = servers.at(1).toObject();
    QCOMPARE(seedbox.value(QLatin1String("id")).toString(), QStringLiteral("{77c1}"));
    QCOMPARE(seedbox.value(QLatin1String("name")).toString(), QStringLiteral("seedbox"));
    QCOMPARE(seedbox.value(QLatin1String("engine")).toString(), QStringLiteral("transmission"));
    QCOMPARE(seedbox.value(QLatin1String("current")).toBool(), false);
    QCOMPARE(seedbox.value(QLatin1String("state")).toString(), QStringLiteral("ok"));
    QCOMPARE(seedbox.value(QLatin1String("count")).toInt(), 38);
    QCOMPARE(seedbox.value(QLatin1String("seen")).toString(),
             QStringLiteral("2026-11-11T21:10:02Z"));
    QVERIFY(seedbox.contains(QLatin1String("reason")));
    QCOMPARE(servers.at(0).toObject().value(QLatin1String("current")).toBool(), true);
}

void TestAgentAnswers::listsAlwaysPresent()
{
    Answers::StatusInput input;
    input.at = kAt;
    input.omaseed = QStringLiteral("0.14.0");
    const QJsonObject object = parse(Answers::status(input));
    QVERIFY(object.value(QLatin1String("servers")).isArray());
    QCOMPARE(object.value(QLatin1String("servers")).toArray().size(), 0);
    QVERIFY(object.value(QLatin1String("moves")).toObject().value(QLatin1String("now")).isNull());
    QVERIFY(
        object.value(QLatin1String("server")).toObject().value(QLatin1String("problem")).isNull());
    QCOMPARE(object.value(QLatin1String("paused")).toInt(), 0);
}

void TestAgentAnswers::optionsDefaultsAndRefusals()
{
    // Nothing given: the defaults of C45 design 3.
    Answers::ParsedOptions parsed = Answers::parseOptions(QStringLiteral("log"), QString());
    QVERIFY(parsed.usage.isEmpty());
    QCOMPARE(parsed.options.level, QStringLiteral("warning"));
    QCOMPARE(parsed.options.lines, 50);
    QCOMPARE(parsed.options.since, qint64(-1));
    QCOMPARE(parsed.options.landed, 5);
    parsed = Answers::parseOptions(QStringLiteral("status"), QStringLiteral("{}"));
    QVERIFY(parsed.usage.isEmpty());

    parsed = Answers::parseOptions(
        QStringLiteral("log"),
        QStringLiteral(
            R"({"server":"attic","level":"info","lines":3,"since":4120,"torrent":"3F9A1C0E"})"));
    QVERIFY2(parsed.usage.isEmpty(), qPrintable(parsed.usage));
    QCOMPARE(parsed.options.server, QStringLiteral("attic"));
    QCOMPARE(parsed.options.level, QStringLiteral("info"));
    QCOMPARE(parsed.options.lines, 3);
    QCOMPARE(parsed.options.since, qint64(4120));
    QCOMPARE(parsed.options.torrent, QStringLiteral("3f9a1c0e"));

    // An unknown key, a key the word doesn't take, a wrong type, out of range, not an object.
    for (const auto &[command, json] : QList<std::pair<QString, QString>>{
             {QStringLiteral("status"), QStringLiteral(R"({"server":"attic"})")},
             {QStringLiteral("errors"), QStringLiteral(R"({"bogus":1})")},
             {QStringLiteral("log"), QStringLiteral(R"({"lines":"3"})")},
             {QStringLiteral("log"), QStringLiteral(R"({"lines":0})")},
             {QStringLiteral("log"), QStringLiteral(R"({"lines":2.5})")},
             {QStringLiteral("log"), QStringLiteral(R"({"level":"loud"})")},
             {QStringLiteral("moves"), QStringLiteral(R"({"landed":21})")},
             {QStringLiteral("moves"), QStringLiteral(R"({"server":""})")},
             {QStringLiteral("errors"), QStringLiteral("[1]")},
             {QStringLiteral("errors"), QStringLiteral("{")}}) {
        parsed = Answers::parseOptions(command, json);
        QVERIFY2(parsed.usage.startsWith(QStringLiteral("omaseed: %1 doesn't take ").arg(command)),
                 qPrintable(json));
    }
}

void TestAgentAnswers::serverMatching()
{
    const QList<Answers::ServerRef> servers{
        {QStringLiteral("{0b9e}"), QStringLiteral("attic")},
        {QStringLiteral("{77c1}"), QStringLiteral("seedbox")},
        {QStringLiteral("{88aa}"), QStringLiteral("Box")},
        {QStringLiteral("{99bb}"), QStringLiteral("BOX")},
    };
    QCOMPARE(Answers::matchServer(servers, QStringLiteral("{77c1}")).id, QStringLiteral("{77c1}"));
    QCOMPARE(Answers::matchServer(servers, QStringLiteral("77c1")).id, QStringLiteral("{77c1}"));
    QCOMPARE(Answers::matchServer(servers, QStringLiteral("attic")).id, QStringLiteral("{0b9e}"));
    QCOMPARE(Answers::matchServer(servers, QStringLiteral("ATTIC")).id, QStringLiteral("{0b9e}"));
    QCOMPARE(Answers::matchServer(servers, QStringLiteral("BOX")).id, QStringLiteral("{99bb}"));
    // Two by case, none at all: unknown.
    const Answers::ServerMatch two = Answers::matchServer(servers, QStringLiteral("box"));
    QCOMPARE(two.code, QStringLiteral("unknown-server"));
    const Answers::ServerMatch none = Answers::matchServer(servers, QStringLiteral("nobody"));
    QVERIFY(none.id.isEmpty());
    QCOMPARE(none.code, QStringLiteral("unknown-server"));
    QCOMPARE(none.message, QStringLiteral("No server called nobody. omaseed status lists them."));
    QCOMPARE(Answers::notCurrentWords(
                 QStringLiteral("attic"), QStringLiteral("seedbox"), QStringLiteral("errors")),
             QStringLiteral("omaseed is on attic, not seedbox. errors reads only the server "
                            "omaseed is on."));
}

// C45 V9: an error on qBittorrent, the board's first torrent: its words, its fix as `do`
// spells it, and the last 3 log lines, newest last.
void TestAgentAnswers::errorNoAccessQbt()
{
    Answers::ErrorsInput input = errorsOn(QStringLiteral("qbittorrent"));
    input.torrents = {lantern()};
    input.dismissed = 1;
    const QJsonObject answer = parse(Answers::errors(input));
    QCOMPARE(answer.value(QStringLiteral("command")).toString(), QStringLiteral("errors"));
    QCOMPARE(answer.value(QStringLiteral("dismissed")).toInt(), 1);
    QVERIFY(answer.value(QStringLiteral("server"))
                .toObject()
                .value(QStringLiteral("problem"))
                .isNull());
    const QJsonObject t = answer.value(QStringLiteral("torrents")).toArray().at(0).toObject();
    QCOMPARE(t.value(QStringLiteral("kind")).toString(), QStringLiteral("error"));
    QCOMPARE(t.value(QStringLiteral("cause")).toString(), QStringLiteral("noAccess"));
    QCOMPARE(t.value(QStringLiteral("fact")).toString(), QStringLiteral("no access"));
    QCOMPARE(t.value(QStringLiteral("since")).toString(), QStringLiteral("2026-11-12T14:03:11Z"));
    QCOMPARE(t.value(QStringLiteral("state")).toString(), QStringLiteral("stopped"));
    QCOMPARE(t.value(QStringLiteral("savePath")).toString(), QStringLiteral("/data/music"));
    const QJsonObject fix = t.value(QStringLiteral("fix")).toObject();
    QCOMPARE(fix.value(QStringLiteral("words")).toString(), QStringLiteral("move…"));
    QCOMPARE(fix.value(QStringLiteral("do")).toString(), QStringLiteral("move"));
    QCOMPARE(fix.value(QStringLiteral("needs")).toArray(), QJsonArray{QStringLiteral("--to")});
    const QJsonArray log = t.value(QStringLiteral("log")).toArray();
    QCOMPARE(log.size(), 3);
    QCOMPARE(log.at(0).toObject().value(QStringLiteral("id")).toInt(), 4118);
    const QJsonObject newest = log.at(2).toObject();
    QCOMPARE(newest.value(QStringLiteral("id")).toInt(), 4120);
    QCOMPARE(newest.value(QStringLiteral("level")).toString(), QStringLiteral("critical"));
    QCOMPARE(newest.value(QStringLiteral("at")).toString(), QStringLiteral("2026-11-12T14:03:11Z"));
    QCOMPARE(log.at(1).toObject().value(QStringLiteral("level")).toString(),
             QStringLiteral("warning"));
}

// A failure keeps no cause key: its line gives one (ProblemMatch::describe).
void TestAgentAnswers::failureMoveHasACause()
{
    Answers::ErrorTorrent arcade;
    arcade.hash = QStringLiteral("8be2d41700000000000000000000000000000000");
    arcade.name = QStringLiteral("Arcade Set 01");
    arcade.failureLine = QStringLiteral(
        "Failed to move torrent. Torrent: \"Arcade Set 01\". Source: \"/data/incoming\". "
        "Destination: \"/mnt/fast/games\". Reason: \"8be2d4 storage move failed. file_rename "
        "(/data/incoming/Arcade Set 01): Permission denied\"");
    arcade.fact = QStringLiteral("didn't move");
    arcade.fixWords = QStringLiteral("move…");
    arcade.fixAction = QStringLiteral("failure:location");
    arcade.phase = omaseed::core::TorrentPhase::Seeding;
    Answers::ErrorsInput input = errorsOn(QStringLiteral("qbittorrent"));
    input.torrents = {arcade};
    const QJsonObject t = firstTorrent(input);
    QCOMPARE(t.value(QStringLiteral("kind")).toString(), QStringLiteral("failure"));
    QCOMPARE(t.value(QStringLiteral("cause")).toString(), QStringLiteral("noAccess"));
    QCOMPARE(t.value(QStringLiteral("state")).toString(), QStringLiteral("seeding"));
    QVERIFY(t.value(QStringLiteral("since")).isNull());
    QCOMPARE(t.value(QStringLiteral("log")).toArray(), QJsonArray());

    // A line that says nothing omaseed knows: no cause.
    input.torrents[0].failureLine = QStringLiteral("Something else");
    QCOMPARE(firstTorrent(input).value(QStringLiteral("cause")).toString(), QString());
}

void TestAgentAnswers::stuckMove()
{
    Answers::ErrorTorrent torrent = lantern();
    torrent.stuckMove = true;
    torrent.problemCause = QStringLiteral("stuckMove");
    torrent.phase = omaseed::core::TorrentPhase::Seeding;
    Answers::ErrorsInput input = errorsOn(QStringLiteral("qbittorrent"));
    input.torrents = {torrent};
    const QJsonObject t = firstTorrent(input);
    QCOMPARE(t.value(QStringLiteral("kind")).toString(), QStringLiteral("stuck-move"));
    QCOMPARE(t.value(QStringLiteral("cause")).toString(), QStringLiteral("stuckMove"));
}

void TestAgentAnswers::missingFiles()
{
    Answers::ErrorTorrent flagged = lantern();
    flagged.missingFiles = true;
    flagged.problemCause = QStringLiteral("missingFiles");
    flagged.fixAction = QStringLiteral("problem:recheck");
    // Missing by its phase alone, before the log said anything.
    Answers::ErrorTorrent byPhase;
    byPhase.hash = QStringLiteral("aa00000000000000000000000000000000000000");
    byPhase.name = QStringLiteral("gone");
    byPhase.phase = omaseed::core::TorrentPhase::Missing;
    Answers::ErrorsInput input = errorsOn(QStringLiteral("qbittorrent"));
    input.torrents = {flagged, byPhase};
    const QJsonArray torrents =
        parse(Answers::errors(input)).value(QStringLiteral("torrents")).toArray();
    QCOMPARE(torrents.at(0).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("missing-files"));
    QCOMPARE(torrents.at(0)
                 .toObject()
                 .value(QStringLiteral("fix"))
                 .toObject()
                 .value(QStringLiteral("do"))
                 .toString(),
             QStringLiteral("recheck"));
    QCOMPARE(torrents.at(1).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("missing-files"));
    QCOMPARE(torrents.at(1).toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("missing"));
}

// C25 K24: on Transmission the sentence is the engine's own text, and there is no log.
void TestAgentAnswers::transmissionEngineText()
{
    Answers::ErrorTorrent torrent = lantern();
    torrent.sentence = QStringLiteral("Transmission says: No data found! Ensure your drives are "
                                      "connected.");
    torrent.fixAction = QStringLiteral("problem:start");
    torrent.fixWords = QStringLiteral("retry");
    Answers::ErrorsInput input = errorsOn(QStringLiteral("transmission"));
    input.torrents = {torrent};
    const QJsonObject t = firstTorrent(input);
    QCOMPARE(t.value(QStringLiteral("sentence")).toString(), torrent.sentence);
    QCOMPARE(t.value(QStringLiteral("log")).toArray(), QJsonArray());
    QCOMPARE(t.value(QStringLiteral("fix")).toObject().value(QStringLiteral("do")).toString(),
             QStringLiteral("retry"));
}

// Deluge pauses a torrent in error (C25 K24): stopped, started again where it is.
void TestAgentAnswers::delugePaused()
{
    Answers::ErrorTorrent torrent = lantern();
    torrent.fixAction = QStringLiteral("problem:startHere");
    torrent.fixWords = QStringLiteral("start it here");
    Answers::ErrorsInput input = errorsOn(QStringLiteral("deluge"));
    input.torrents = {torrent};
    const QJsonObject t = firstTorrent(input);
    QCOMPARE(t.value(QStringLiteral("kind")).toString(), QStringLiteral("error"));
    QCOMPARE(t.value(QStringLiteral("state")).toString(), QStringLiteral("stopped"));
    QCOMPARE(t.value(QStringLiteral("log")).toArray(), QJsonArray());
    const QJsonObject fix = t.value(QStringLiteral("fix")).toObject();
    QCOMPARE(fix.value(QStringLiteral("do")).toString(), QStringLiteral("start-here"));
    QCOMPARE(fix.value(QStringLiteral("needs")).toArray(), QJsonArray());
}

// Every id RowWords::fixWords knows, spelled as `do`'s actions (docs/62 V9).
void TestAgentAnswers::fixDoMapping()
{
    const QList<std::pair<QString, QString>> ids{
        {QStringLiteral("start"), QStringLiteral("retry")},
        {QStringLiteral("startHere"), QStringLiteral("start-here")},
        {QStringLiteral("location"), QStringLiteral("move")},
        {QStringLiteral("recheck"), QStringLiteral("recheck")},
        {QStringLiteral("addAgain"), QString()},
        {QStringLiteral("folder"), QString()},
        {QString(), QString()},
    };
    for (const auto &[id, action] : ids) {
        for (const QString &block : {QStringLiteral("problem:"), QStringLiteral("failure:")}) {
            const Answers::FixDo fix = Answers::fixDo(id.isEmpty() ? QString() : block + id);
            QCOMPARE(fix.action, action);
            QCOMPARE(fix.needs,
                     id == QLatin1String("location") ? QStringList{QStringLiteral("--to")}
                                                     : QStringList());
        }
    }
}

// C45 V7: a passkey in a sentence or a log line never leaves.
void TestAgentAnswers::urlsMasked()
{
    Answers::ErrorTorrent torrent = lantern();
    torrent.sentence = QStringLiteral("Tracker http://t.example:2710/a.php?pk=s3cr3tk3y failed.");
    torrent.log = {{1, kAt, 4, QStringLiteral("announce to udp://t.example:6969/s3cr3tk3y")}};
    Answers::ErrorsInput input = errorsOn(QStringLiteral("qbittorrent"));
    input.torrents = {torrent};
    const QByteArray bytes = Answers::errors(input);
    QVERIFY(!bytes.contains("s3cr3tk3y"));
    QVERIFY(bytes.contains("t.example:2710"));
}

// DECISIONS C45 V10 (design 13): warnings and worse, the last 50, oldest first.
void TestAgentAnswers::logDefaultsWarningFifty()
{
    const QJsonObject answer = parse(Answers::log(logBoard()));
    QCOMPARE(answer.value(QStringLiteral("command")).toString(), QStringLiteral("log"));
    QCOMPARE(answer.value(QStringLiteral("level")).toString(), QStringLiteral("warning"));
    QCOMPARE(answer.value(QStringLiteral("matched")).toInt(), 20);
    const QList<int> ids = idsOf(answer);
    QCOMPARE(ids.size(), 20);
    QCOMPARE(ids.constFirst(), 0);
    QCOMPARE(ids.constLast(), 95);
    QVERIFY(!answer.value(QStringLiteral("restarted")).toBool());
    QVERIFY(answer.value(QStringLiteral("server")).isObject());
    const QJsonObject first = answer.value(QStringLiteral("lines")).toArray().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("level")).toString(), QStringLiteral("critical"));
    QCOMPARE(first.value(QStringLiteral("at")).toString(), QStringLiteral("2026-11-11T09:02:13Z"));
    QVERIFY(first.value(QStringLiteral("torrent")).isNull());

    // The last N of those.
    Answers::LogInput three = logBoard();
    three.options.lines = 3;
    const QJsonObject last = parse(Answers::log(three));
    QCOMPARE(idsOf(last), QList<int>({85, 90, 95}));
    QCOMPARE(last.value(QStringLiteral("matched")).toInt(), 20);
}

void TestAgentAnswers::logLevelAndWorse()
{
    Answers::LogInput input = logBoard();
    input.options.level = QStringLiteral("info");
    QCOMPARE(parse(Answers::log(input)).value(QStringLiteral("matched")).toInt(), 60);
    input.options.level = QStringLiteral("critical");
    const QJsonObject critical = parse(Answers::log(input));
    QCOMPARE(idsOf(critical), QList<int>({0, 25, 50, 75}));
    for (const QJsonValue &line : critical.value(QStringLiteral("lines")).toArray())
        QCOMPARE(line.toObject().value(QStringLiteral("level")).toString(),
                 QStringLiteral("critical"));
    input.options.level = QStringLiteral("normal");
    const QJsonObject every = parse(Answers::log(input));
    QCOMPARE(every.value(QStringLiteral("matched")).toInt(), 100);
    QCOMPARE(idsOf(every).constFirst(), 50); // the last 50
    QCOMPARE(every.value(QStringLiteral("level")).toString(), QStringLiteral("normal"));
}

void TestAgentAnswers::logSinceFromId()
{
    Answers::LogInput input = logBoard();
    input.options.since = 50;
    const QJsonObject answer = parse(Answers::log(input));
    QCOMPARE(idsOf(answer), QList<int>({50, 55, 60, 65, 70, 75, 80, 85, 90, 95}));
    QCOMPARE(answer.value(QStringLiteral("matched")).toInt(), 10);
    QVERIFY(!answer.value(QStringLiteral("restarted")).toBool());
    // The newest + 1 is nothing new, not a restart.
    input.options.since = 100;
    const QJsonObject none = parse(Answers::log(input));
    QVERIFY(idsOf(none).isEmpty());
    QVERIFY(!none.value(QStringLiteral("restarted")).toBool());
    QCOMPARE(none.value(QStringLiteral("next")).toInt(), 100);
}

void TestAgentAnswers::logSinceBeyondIsRestarted()
{
    // qBittorrent numbers from 0 again after a restart: a since past its newest starts over.
    Answers::LogInput input = logBoard();
    input.options.since = 4121;
    const QJsonObject answer = parse(Answers::log(input));
    QVERIFY(answer.value(QStringLiteral("restarted")).toBool());
    QCOMPARE(answer.value(QStringLiteral("matched")).toInt(), 20);
    QCOMPARE(idsOf(answer).constFirst(), 0);
    QCOMPARE(answer.value(QStringLiteral("next")).toInt(), 100);
}

void TestAgentAnswers::logTorrentFilter()
{
    // C3 D13: each line's torrent, by its quoted name or its hash prefix.
    Answers::LogInput input = logBoard();
    const QJsonArray all = parse(Answers::log(input)).value(QStringLiteral("lines")).toArray();
    const auto torrentOf = [&](int id) {
        for (const QJsonValue &line : all)
            if (line.toObject().value(QStringLiteral("id")).toInt() == id)
                return line.toObject().value(QStringLiteral("torrent"));
        return QJsonValue(QJsonValue::Undefined);
    };
    QCOMPARE(torrentOf(45).toString(), kArcade);
    QCOMPARE(torrentOf(50).toString(), kLantern);
    QVERIFY(torrentOf(55).isNull());

    const Answers::TorrentMatch arcade =
        Answers::matchTorrent(input.library, QStringLiteral("8BE2D417"), QStringLiteral("attic"));
    QVERIFY(arcade.code.isEmpty());
    QCOMPARE(arcade.torrent.hash, kArcade);
    input.torrent = arcade.torrent;
    QJsonObject answer = parse(Answers::log(input));
    QCOMPARE(idsOf(answer), QList<int>({45}));
    QCOMPARE(answer.value(QStringLiteral("matched")).toInt(), 1);
    input.torrent = Answers::matchTorrent(input.library, kLantern, QStringLiteral("attic")).torrent;
    answer = parse(Answers::log(input));
    QCOMPARE(idsOf(answer), QList<int>({50}));
    QCOMPARE(answer.value(QStringLiteral("lines"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("torrent"))
                 .toString(),
             kLantern);

    // Shorter than 8, or more than one, is ambiguous; none is unknown-torrent.
    const Answers::TorrentMatch shortOne =
        Answers::matchTorrent(input.library, QStringLiteral("8be2"), QStringLiteral("attic"));
    QCOMPARE(shortOne.code, QStringLiteral("ambiguous"));
    QCOMPARE(shortOne.message,
             QStringLiteral("8be2 matches 1 torrent on attic: give at least 8 characters of the "
                            "hash."));
    QList<Answers::LogTorrent> twins = input.library;
    twins.append({QStringLiteral("8be2d41799999999999999999999999999999999"),
                  QStringLiteral("Arcade Set 02"),
                  QStringLiteral("8be2d4")});
    const Answers::TorrentMatch two =
        Answers::matchTorrent(twins, QStringLiteral("8be2d417"), QStringLiteral("attic"));
    QCOMPARE(two.code, QStringLiteral("ambiguous"));
    QVERIFY(two.message.startsWith(QStringLiteral("8be2d417 matches 2 torrents on attic")));
    const Answers::TorrentMatch none =
        Answers::matchTorrent(input.library, QStringLiteral("deadbeef"), QStringLiteral("attic"));
    QCOMPARE(none.code, QStringLiteral("unknown-torrent"));
    QCOMPARE(none.message, QStringLiteral("No torrent deadbeef on attic."));
}

void TestAgentAnswers::logNextAfterNewest()
{
    QCOMPARE(parse(Answers::log(logBoard())).value(QStringLiteral("next")).toInt(), 100);
    // An empty log: next is the since asked, or 0.
    Answers::LogInput empty = logBoard();
    empty.log.clear();
    QJsonObject answer = parse(Answers::log(empty));
    QCOMPARE(answer.value(QStringLiteral("next")).toInt(), 0);
    QCOMPARE(answer.value(QStringLiteral("lines")).toArray().size(), 0);
    QVERIFY(answer.value(QStringLiteral("oldest")).isNull());
    QVERIFY(!answer.value(QStringLiteral("fromStart")).toBool());
    empty.options.since = 30;
    answer = parse(Answers::log(empty));
    QCOMPARE(answer.value(QStringLiteral("next")).toInt(), 30);
    QVERIFY(!answer.value(QStringLiteral("restarted")).toBool());
}

void TestAgentAnswers::logFromStartAndOldest()
{
    // DECISIONS C45: the default read is problems-only, which never sees line 0: unknown.
    QJsonObject answer = parse(Answers::log(logBoard()));
    QVERIFY(answer.contains(QStringLiteral("fromStart")));
    QVERIFY(answer.value(QStringLiteral("fromStart")).isNull());
    QCOMPARE(answer.value(QStringLiteral("oldest")).toString(),
             QStringLiteral("2026-11-11T09:02:13Z"));
    // Every level: the read sees line 0.
    Answers::LogInput input = logBoard();
    input.options.level = QStringLiteral("normal");
    answer = parse(Answers::log(input));
    QCOMPARE(answer.value(QStringLiteral("fromStart")), QJsonValue(true));
    // qBittorrent dropped its first lines: the memory starts at the oldest it keeps.
    input.log.remove(0, 10);
    answer = parse(Answers::log(input));
    QCOMPARE(answer.value(QStringLiteral("fromStart")), QJsonValue(false));
    QCOMPARE(answer.value(QStringLiteral("oldest")).toString(),
             QStringLiteral("2026-11-11T09:12:13Z"));
}

// DECISIONS C45 V11 (design 14): the move under way, from its MoveJob.
void TestAgentAnswers::movesRunningFields()
{
    omaseed::core::MoveJob job;
    QVERIFY(job.fromJson(gamesJournal(QStringLiteral("Running"))));
    Answers::MovesInput input;
    input.at = kAt;
    input.omaseed = QStringLiteral("0.14.0");
    input.server = attic().server;
    input.running = {Answers::runningOf(
        job, QStringLiteral("Moving games"), QStringLiteral("http://t.example/ann/s3cr3t ok"))};
    const QJsonObject answer = parse(Answers::moves(input));
    QCOMPARE(answer.value(QStringLiteral("command")).toString(), QStringLiteral("moves"));
    QCOMPARE(answer.value(QStringLiteral("landed")).toArray().size(), 0);
    QCOMPARE(answer.value(QStringLiteral("kept")).toInt(), 0);
    const QJsonObject move = answer.value(QStringLiteral("running")).toArray().at(0).toObject();
    const QDateTime started(QDate(2026, 11, 12), QTime(13, 40, 5), QTimeZone::UTC);
    QCOMPARE(move.value(QStringLiteral("id")).toString(),
             QStringLiteral("j-") + QString::number(started.toSecsSinceEpoch(), 36));
    QCOMPARE(move.value(QStringLiteral("kind")).toString(), QStringLiteral("folder"));
    QCOMPARE(move.value(QStringLiteral("title")).toString(), QStringLiteral("games"));
    QCOMPARE(move.value(QStringLiteral("to")).toString(), QStringLiteral("/mnt/fast/games"));
    QCOMPARE(move.value(QStringLiteral("started")).toString(),
             QStringLiteral("2026-11-12T13:40:05Z"));
    QCOMPARE(move.value(QStringLiteral("state")).toString(), QStringLiteral("running"));
    QVERIFY(move.value(QStringLiteral("pausedFor")).isNull());
    QCOMPARE(move.value(QStringLiteral("headline")).toString(), QStringLiteral("Moving games"));
    QVERIFY(!move.value(QStringLiteral("line")).toString().contains(QStringLiteral("s3cr3t")));
    const QJsonObject torrents = move.value(QStringLiteral("torrents")).toObject();
    QCOMPARE(torrents.value(QStringLiteral("of")).toInt(), 12);
    QCOMPARE(torrents.value(QStringLiteral("there")).toInt(), 5);
    QCOMPARE(torrents.value(QStringLiteral("failed")).toInt(), 0);
    QCOMPARE(torrents.value(QStringLiteral("leftOut")).toInt(), 1);
    const QJsonObject bytes = move.value(QStringLiteral("bytes")).toObject();
    QCOMPARE(bytes.value(QStringLiteral("of")).toInteger(), qint64(48) << 30);
    QCOMPARE(bytes.value(QStringLiteral("there")).toInteger(), qint64(20) << 30);
    const QJsonObject now = move.value(QStringLiteral("now")).toObject();
    QCOMPARE(now.value(QStringLiteral("name")).toString(), QStringLiteral("Arcade Set 02"));
    QVERIFY(now.value(QStringLiteral("hash")).toString().startsWith(QStringLiteral("c41e09aa")));
    QCOMPARE(move.value(QStringLiteral("queued")).toArray(),
             QJsonArray{QStringLiteral("3 torrents")});
}

void TestAgentAnswers::movesPausedFor()
{
    using Reason = omaseed::core::MoveJob::PauseReason;
    // Every reason has its word; None is null.
    const QList<std::pair<Reason, QString>> words{
        {Reason::None, QString()},
        {Reason::User, QStringLiteral("by-you")},
        {Reason::Quit, QStringLiteral("quit")},
        {Reason::FirstFailed, QStringLiteral("first-failed")},
        {Reason::Refused, QStringLiteral("refused")},
        {Reason::NoSpace, QStringLiteral("no-space")},
        {Reason::NearlyFull, QStringLiteral("nearly-full")},
        {Reason::Stuck, QStringLiteral("stuck")},
    };
    for (const auto &[reason, word] : words)
        QCOMPARE(Answers::pausedForName(reason), word);
    const QMetaEnum meta = QMetaEnum::fromType<Reason>();
    QCOMPARE(meta.keyCount(), int(words.size()));

    // A journal's paused job says why; one paused to quit runs again when read (J5).
    for (const char *key : {"User", "NearlyFull", "Stuck"}) {
        omaseed::core::MoveJob job;
        QVERIFY(job.fromJson(gamesJournal(QStringLiteral("Paused"), QString::fromLatin1(key))));
        const Answers::RunningMove move = Answers::runningOf(job, {}, {});
        QCOMPARE(move.state, QStringLiteral("paused"));
        QCOMPARE(move.pausedFor, Answers::pausedForName(Reason(meta.keyToValue(key))));
    }
    omaseed::core::MoveJob quit;
    QVERIFY(quit.fromJson(gamesJournal(QStringLiteral("Paused"), QStringLiteral("Quit"))));
    QCOMPARE(Answers::runningOf(quit, {}, {}).state, QStringLiteral("running"));
    QVERIFY(Answers::runningOf(quit, {}, {}).pausedFor.isEmpty());

    using State = omaseed::core::MoveJob::State;
    QCOMPARE(Answers::moveStateName(State::Pausing), QStringLiteral("pausing"));
    QCOMPARE(Answers::moveStateName(State::Ending), QStringLiteral("ending"));
    QCOMPARE(Answers::moveStateName(State::Carrying), QStringLiteral("carrying"));
    QVERIFY(Answers::moveStateName(State::Done).isEmpty());
}

void TestAgentAnswers::landedBackFromCheck()
{
    // C18 D7: MoveHistory::check's counts, per torrent.
    const QJsonObject answer = movesAnswer(twoMoves(), &kSavePaths);
    QCOMPARE(answer.value(QStringLiteral("kept")).toInt(), 2);
    const QJsonArray landed = answer.value(QStringLiteral("landed")).toArray();
    QCOMPARE(landed.size(), 2);
    QCOMPARE(landed.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("m-1b20")); // newest first
    const QJsonObject older = landed.at(1).toObject();
    QCOMPARE(older.value(QStringLiteral("title")).toString(), QStringLiteral("5 torrents"));
    QCOMPARE(older.value(QStringLiteral("to")).toString(), QStringLiteral("/data/archive"));
    QCOMPARE(older.value(QStringLiteral("finished")).toString(),
             QStringLiteral("2026-11-11T21:10:19Z"));
    QCOMPARE(older.value(QStringLiteral("started")).toString(),
             QStringLiteral("2026-11-11T21:08:51Z"));
    const QJsonArray torrents = older.value(QStringLiteral("torrents")).toArray();
    QCOMPARE(torrents.size(), 5);
    const QJsonObject first = torrents.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Track h1"));
    QCOMPARE(first.value(QStringLiteral("from")).toString(), QStringLiteral("/data/incoming"));
    QCOMPARE(first.value(QStringLiteral("to")).toString(), QStringLiteral("/data/archive"));
    const QJsonObject back = older.value(QStringLiteral("back")).toObject();
    QCOMPARE(back.value(QStringLiteral("goBack")).toInt(), 2);
    QCOMPARE(back.value(QStringLiteral("movedAgain")).toInt(), 1);
    QCOMPARE(back.value(QStringLiteral("movedOutside")).toInt(), 1);
    QCOMPARE(back.value(QStringLiteral("gone")).toInt(), 1);
    // The newest one: its only torrent goes back.
    const QJsonObject newest = landed.at(0).toObject().value(QStringLiteral("back")).toObject();
    QCOMPARE(newest.value(QStringLiteral("goBack")).toInt(), 1);
    QCOMPARE(newest.value(QStringLiteral("movedAgainBy")).toArray().size(), 0);
}

void TestAgentAnswers::landedMovedAgainBy()
{
    const QJsonObject answer = movesAnswer(twoMoves(), &kSavePaths);
    const QJsonArray by = answer.value(QStringLiteral("landed"))
                              .toArray()
                              .at(1)
                              .toObject()
                              .value(QStringLiteral("back"))
                              .toObject()
                              .value(QStringLiteral("movedAgainBy"))
                              .toArray();
    QCOMPARE(by.size(), 1);
    QCOMPARE(by.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("m-1b20"));
    QCOMPARE(by.at(0).toObject().value(QStringLiteral("finished")).toString(),
             QStringLiteral("2026-11-12T12:10:19Z"));
}

void TestAgentAnswers::anotherServerBackNull()
{
    // C45 V11: another server's library isn't here: no move back, the records as kept.
    const QJsonObject answer = movesAnswer(twoMoves(), nullptr, 1);
    QCOMPARE(answer.value(QStringLiteral("kept")).toInt(), 2);
    const QJsonArray landed = answer.value(QStringLiteral("landed")).toArray();
    QCOMPARE(landed.size(), 1);
    QVERIFY(landed.at(0).toObject().value(QStringLiteral("back")).isNull());
    QCOMPARE(landed.at(0).toObject().value(QStringLiteral("torrents")).toArray().size(), 1);
}

QTEST_GUILESS_MAIN(TestAgentAnswers)
#include "tst_agentanswers.moc"
