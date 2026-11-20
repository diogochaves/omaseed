// DECISIONS C45 V4, V6, V7, V8 (#163): what the running omaseed answers, from plain inputs:
// the envelope, the options a method takes, --server's match, and status's JSON.

#include "core/AgentAnswers.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QTEST_GUILESS_MAIN(TestAgentAnswers)
#include "tst_agentanswers.moc"
