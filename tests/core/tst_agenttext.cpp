// DECISIONS C45 V8, V9, V12 (#163): status's and errors' JSON as a person reads it, docs/62 §4's
// mock line for line; the parts that are 0 left out, the unreachable server's retry, colour only
// when asked, and the free text wrapped at the width.

#include "core/AgentAnswers.hpp"
#include "core/AgentText.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Answers = omaseed::core::AgentAnswers;
namespace Text = omaseed::core::AgentText;

namespace {

/// "today" for every case: 12 Nov 2026, 14:05:40 here.
const QDateTime kNow(QDate(2026, 11, 12), QTime(14, 5, 40));

Answers::StatusInput attic()
{
    Answers::StatusInput input;
    input.at = kNow;
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
    current.seen = QDateTime(QDate(2026, 11, 12), QTime(14, 1));
    Answers::ListedServer seedbox;
    seedbox.id = QStringLiteral("{77c1}");
    seedbox.name = QStringLiteral("seedbox");
    seedbox.engine = QStringLiteral("transmission");
    seedbox.state = QStringLiteral("ok");
    seedbox.count = 38;
    seedbox.seen = QDateTime(QDate(2026, 11, 11), QTime(21, 10, 2));
    input.servers = {current, seedbox};
    return input;
}

/// The lines, made from the JSON as the CLI makes them.
QString lines(const Answers::StatusInput &input, bool colour = false, int width = 80)
{
    const QJsonObject answer = QJsonDocument::fromJson(Answers::status(input)).object();
    return Text::lines(answer, Text::TextStyle{colour, width, kNow});
}

/// docs/62 V9's mock: the error and the failure, one dismissed.
Answers::ErrorsInput board()
{
    Answers::ErrorsInput input;
    input.at = kNow;
    input.omaseed = QStringLiteral("0.14.0");
    input.server = attic().server;
    Answers::ErrorTorrent lantern;
    lantern.hash = QStringLiteral("3f9a1c0e00000000000000000000000000000000");
    lantern.name = QStringLiteral("Lantern Festival Field Recordings");
    lantern.problem = true;
    lantern.problemCause = QStringLiteral("noAccess");
    lantern.fact = QStringLiteral("no access");
    lantern.sentence =
        QStringLiteral("qBittorrent can't write to /data/music/Lantern Festival Field Recordings.");
    lantern.fixWords = QStringLiteral("move…");
    lantern.fixAction = QStringLiteral("problem:location");
    lantern.since = QDateTime(QDate(2026, 11, 12), QTime(14, 3, 11));
    lantern.phase = omaseed::core::TorrentPhase::Stopped;
    lantern.log = {{4120,
                    lantern.since,
                    8,
                    QStringLiteral("File error alert. Torrent: \"Lantern Festival Field "
                                   "Recordings\". … Reason: \"Permission denied\"")}};
    Answers::ErrorTorrent arcade;
    arcade.hash = QStringLiteral("8be2d41700000000000000000000000000000000");
    arcade.name = QStringLiteral("Arcade Set 01");
    arcade.failureLine = QStringLiteral("Failed to move torrent.");
    arcade.fact = QStringLiteral("didn't move");
    arcade.sentence =
        QStringLiteral("The move to /mnt/fast/games stopped: no space left on the drive.");
    arcade.fixWords = QStringLiteral("retry");
    arcade.fixAction = QStringLiteral("failure:start");
    arcade.since = QDateTime(QDate(2026, 11, 11), QTime(21, 10, 40));
    arcade.phase = omaseed::core::TorrentPhase::Seeding;
    input.torrents = {lantern, arcade};
    input.dismissed = 1;
    return input;
}

QString lines(const Answers::ErrorsInput &input, bool colour = false, int width = 80)
{
    const QJsonObject answer = QJsonDocument::fromJson(Answers::errors(input)).object();
    return Text::lines(answer, Text::TextStyle{colour, width, kNow});
}

} // namespace

class TestAgentText : public QObject
{
    Q_OBJECT

private slots:
    void statusLinesAsTheBoard();
    void zeroPartsLeftOut();
    void unreachableSaysRetry();
    void noColourWhenOff();
    void wrapsAtWidth();
    void errorsAsTheBoard();
    void nothingNeedsYou();
    void dismissedLine();
};

void TestAgentText::statusLinesAsTheBoard()
{
    QCOMPARE(lines(attic()),
             QStringLiteral("attic · qBittorrent 5.3.0 · connected · window hidden\n"
                            "  ↓ 4.21 MB/s   ↑ 812 KB/s   alternative limits off\n"
                            "  142 torrents: 2 need you · 6 arriving · 3 landed today\n"
                            "                118 giving back · 13 resting\n"
                            "  1 move running: games → /mnt/fast/games, 5 of 12 there\n"
                            "  also seedbox · Transmission · 38 torrents, seen yesterday 21:10\n"
                            "omaseed errors says what needs you.\n"));

    // The limits' pick, a held pause, a window shown, a server down when last tried.
    Answers::StatusInput input = attic();
    input.windowShown = true;
    input.altPick = QStringLiteral("turtle");
    input.paused = 12;
    input.servers[1].state = QStringLiteral("down");
    const QString text = lines(input);
    QVERIFY2(text.startsWith(QStringLiteral("attic · qBittorrent 5.3.0 · connected\n")),
             qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("   alternative limits on (turtle)\n")));
    QVERIFY(text.contains(
        QStringLiteral("  12 paused by Pause Torrents: Resume Torrents starts them again\n")));
    QVERIFY2(text.contains(QStringLiteral(
                 "  also seedbox · Transmission · unreachable when last tried, yesterday 21:10\n")),
             qPrintable(text));
}

void TestAgentText::zeroPartsLeftOut()
{
    Answers::StatusInput input = attic();
    input.all = 1;
    input.needsYou = 0;
    input.arriving = 1;
    input.landedToday = 0;
    input.givingBack = 0;
    input.resting = 0;
    input.movesRunning = 0;
    input.moveNow.reset();
    input.servers.removeLast();
    QCOMPARE(lines(input),
             QStringLiteral("attic · qBittorrent 5.3.0 · connected · window hidden\n"
                            "  ↓ 4.21 MB/s   ↑ 812 KB/s   alternative limits off\n"
                            "  1 torrent: 1 arriving\n"));
    // Only the calm ones: on the first line, no pointer at errors.
    input.arriving = 0;
    input.givingBack = 1;
    QVERIFY(lines(input).endsWith(QStringLiteral("  1 torrent: 1 giving back\n")));
    input.givingBack = 0;
    input.all = 0;
    QVERIFY(lines(input).endsWith(QStringLiteral("  0 torrents\n")));
    input.needsYou = 1;
    input.all = 1;
    QVERIFY(lines(input).contains(QStringLiteral("  1 torrent: 1 needs you\n")));
}

void TestAgentText::unreachableSaysRetry()
{
    Answers::StatusInput input = attic();
    input.server.reachable = false;
    input.server.connection = QStringLiteral("disconnected");
    input.server.version.clear();
    input.server.problem = Answers::Problem{
        QStringLiteral("attic is unreachable"),
        QStringLiteral("Connection refused at 192.168.1.20:8080. Is qBittorrent running?"),
        12};
    input.servers[0].state = QStringLiteral("down");
    input.servers[0].count = 142;
    input.windowShown = true;
    input.movesRunning = 0;
    input.moveNow.reset();
    input.servers.removeLast();
    QCOMPARE(lines(input),
             QStringLiteral("attic · qBittorrent · unreachable\n"
                            "  Connection refused at 192.168.1.20:8080. Is qBittorrent running?\n"
                            "  omaseed tries again in 12 s.\n"
                            "  142 torrents when last seen, 14:01\n"));
}

void TestAgentText::noColourWhenOff()
{
    QVERIFY(!lines(attic(), false).contains(QLatin1Char('\x1b')));
    const QString coloured = lines(attic(), true);
    QVERIFY(coloured.contains(QStringLiteral("\x1b[32mconnected\x1b[0m")));
    QVERIFY(coloured.contains(QStringLiteral("\x1b[34m1 move running\x1b[0m")));
    QVERIFY(coloured.contains(QStringLiteral("\x1b[1mattic\x1b[0m")));
}

void TestAgentText::wrapsAtWidth()
{
    QCOMPARE(
        Text::wrap(
            QStringLiteral("one two three four"), 10, QStringLiteral("  "), QStringLiteral("    ")),
        QStringLiteral("  one two\n    three\n    four\n"));
    // A word longer than the width stays whole on its own line.
    QCOMPARE(Text::wrap(QStringLiteral("a /a/very/long/path b"), 8, QString(), QString()),
             QStringLiteral("a\n/a/very/long/path\nb\n"));
    // An unreachable server's line wraps under itself.
    Answers::StatusInput input = attic();
    input.server.reachable = false;
    input.server.problem = Answers::Problem{
        QStringLiteral("attic is unreachable"),
        QStringLiteral("Connection refused at 192.168.1.20:8080. Is qBittorrent running?"),
        0};
    const QString text = lines(input, false, 40);
    QVERIFY2(text.contains(
                 QStringLiteral("\n  Connection refused at\n  192.168.1.20:8080. Is qBittorrent\n"
                                "  running?\n")),
             qPrintable(text));
    QCOMPARE(Text::failure(QStringLiteral("omaseed isn't running, so it can't answer."),
                           Text::TextStyle{false, 20, kNow}),
             QStringLiteral("omaseed isn't\nrunning, so it can't\nanswer.\n"));
}

// C45 V9: the board's mock, at its width: the head, each torrent's name and fact, the sentence
// under it, the time and the fix, the log line wrapped under itself, the dismissed one.
void TestAgentText::errorsAsTheBoard()
{
    QCOMPARE(lines(board(), false, 60),
             QStringLiteral("attic · 2 need you\n"
                            "\n"
                            "Lantern Festival Field Recordings  no access\n"
                            "  qBittorrent can't write to /data/music/Lantern Festival\n"
                            "  Field Recordings.\n"
                            "  since 14:03 · fix: move…\n"
                            "  log 14:03:11  File error alert. Torrent: \"Lantern Festival\n"
                            "                Field Recordings\". … Reason: \"Permission\n"
                            "                denied\"\n"
                            "\n"
                            "Arcade Set 01                      didn't move\n"
                            "  The move to /mnt/fast/games stopped: no space left on the\n"
                            "  drive.\n"
                            "  since yesterday 21:10 · fix: retry\n"
                            "\n"
                            "1 dismissed in the window is left out.\n"));

    // The colours: the names bold, an error's fact red, a failure's yellow.
    const QString painted = lines(board(), true, 60);
    QVERIFY(painted.contains(QStringLiteral("\x1b[1mArcade Set 01\x1b[0m")));
    QVERIFY(painted.contains(QStringLiteral("\x1b[31mno access\x1b[0m")));
    QVERIFY(painted.contains(QStringLiteral("\x1b[33mdidn't move\x1b[0m")));
    QVERIFY(painted.contains(QStringLiteral("\x1b[31m2 need you\x1b[0m")));
}

void TestAgentText::nothingNeedsYou()
{
    Answers::ErrorsInput input = board();
    input.torrents.clear();
    input.dismissed = 0;
    QCOMPARE(lines(input), QStringLiteral("attic · nothing needs you.\n"));
    QCOMPARE(lines(input, true),
             QStringLiteral("\x1b[1mattic\x1b[0m · \x1b[32mnothing needs you.\x1b[0m\n"));

    // Unreachable: its problem first, never "nothing needs you".
    input.server.reachable = false;
    input.server.problem = Answers::Problem{QStringLiteral("attic isn't answering"),
                                            QStringLiteral("Nothing answers at 192.168.1.20:8080."),
                                            12};
    QCOMPARE(lines(input),
             QStringLiteral("attic · unreachable\n"
                            "  Nothing answers at 192.168.1.20:8080.\n"
                            "  omaseed tries again in 12 s.\n"));
}

void TestAgentText::dismissedLine()
{
    Answers::ErrorsInput input = board();
    input.torrents.resize(1);
    input.dismissed = 3;
    const QString text = lines(input);
    QVERIFY2(text.startsWith(QStringLiteral("attic · 1 needs you\n")), qPrintable(text));
    QVERIFY(text.endsWith(QStringLiteral("\n\n3 dismissed in the window are left out.\n")));
    input.dismissed = 0;
    QVERIFY(!lines(input).contains(QStringLiteral("dismissed")));
}

QTEST_GUILESS_MAIN(TestAgentText)
#include "tst_agenttext.moc"
