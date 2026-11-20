// DECISIONS C45 V1, V2, V4, V13 and C37 U10: the words' table, their arguments, the help page
// and each word's page, the exit map and the failure object.

#include "core/AgentWords.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

using namespace omaseed::core;
using Kind = AgentWords::Request::Kind;

class TestAgentWords : public QObject
{
    Q_OBJECT

private slots:
    void aWordIsAWordOnlyFirst();
    void aPathNamedLikeAWordIsNot();
    void magnetsAndUrlsAreNot();
    void reservedWordsSayNotYet();
    void optionsBothForms();
    void unknownOptionIsUsage_data();
    void unknownOptionIsUsage();
    void numbersOutOfRangeAreUsage_data();
    void numbersOutOfRangeAreUsage();
    void helpAndPages();
    void helpPageSplicesAboveOptions();
    void wordPagesHaveUsageAndExit();
    void exitForEveryCode_data();
    void exitForEveryCode();
    void failureObjectShape();
};

void TestAgentWords::aWordIsAWordOnlyFirst()
{
    for (const char *word : {"help", "status", "errors", "log", "moves", "do", "check", "skin"})
        QVERIFY2(AgentWords::isWord(QString::fromLatin1(word)), word);
    // Only the first argument is ever asked (main.cpp); a word anywhere else is a link.
    QVERIFY(!AgentWords::isWord(QStringLiteral("--hidden")));
    QVERIFY(!AgentWords::isWord(QString()));
    QVERIFY(!AgentWords::isWord(QStringLiteral("Status")));
    QVERIFY(!AgentWords::isWord(QStringLiteral("status ")));
    const auto request = AgentWords::parse({QStringLiteral("status")});
    QCOMPARE(request.kind, Kind::Call);
    QCOMPARE(AgentWords::method(request.word), QStringLiteral("Status"));
    QVERIFY(request.options.isEmpty());
    QVERIFY(!request.json);
    QCOMPARE(AgentWords::method(QStringLiteral("errors")), QStringLiteral("Errors"));
    QCOMPARE(AgentWords::method(QStringLiteral("log")), QStringLiteral("Log"));
    QCOMPARE(AgentWords::method(QStringLiteral("moves")), QStringLiteral("Moves"));
    QCOMPARE(AgentWords::method(QStringLiteral("help")), QString());
}

void TestAgentWords::aPathNamedLikeAWordIsNot()
{
    for (const char *path : {"./status", "/x/status", "status.torrent", "errors/", "~/log"})
        QVERIFY2(!AgentWords::isWord(QString::fromLatin1(path)), path);
}

void TestAgentWords::magnetsAndUrlsAreNot()
{
    for (const char *link : {"magnet:?xt=urn:btih:3f9a1c0e",
                             "https://example.org/status",
                             "file:///tmp/moves",
                             "-h",
                             "--help",
                             "--json"})
        QVERIFY2(!AgentWords::isWord(QString::fromLatin1(link)), link);
}

void TestAgentWords::reservedWordsSayNotYet()
{
    for (const char *word : {"do", "check", "skin"}) {
        const QString w = QString::fromLatin1(word);
        QVERIFY(AgentWords::isReserved(w));
        const auto request =
            AgentWords::parse({w, QStringLiteral("anything"), QStringLiteral("--json")});
        QCOMPARE(request.kind, Kind::Usage);
        QVERIFY(request.json);
        QCOMPARE(request.message,
                 QStringLiteral("omaseed: %1 isn't in this omaseed yet. omaseed --help lists the "
                                "words it has.")
                     .arg(w));
        QCOMPARE(AgentWords::parse({QStringLiteral("help"), w}).kind, Kind::Usage);
    }
    QVERIFY(!AgentWords::isReserved(QStringLiteral("status")));
}

void TestAgentWords::optionsBothForms()
{
    const auto spaced = AgentWords::parse({QStringLiteral("log"),
                                           QStringLiteral("--level"),
                                           QStringLiteral("info"),
                                           QStringLiteral("--lines"),
                                           QStringLiteral("3"),
                                           QStringLiteral("--since"),
                                           QStringLiteral("4120"),
                                           QStringLiteral("--torrent"),
                                           QStringLiteral("3F9A1C0E"),
                                           QStringLiteral("--server"),
                                           QStringLiteral("attic"),
                                           QStringLiteral("--json")});
    const auto equals = AgentWords::parse({QStringLiteral("log"),
                                           QStringLiteral("--json"),
                                           QStringLiteral("--level=info"),
                                           QStringLiteral("--lines=3"),
                                           QStringLiteral("--since=4120"),
                                           QStringLiteral("--torrent=3f9a1c0e"),
                                           QStringLiteral("--server=attic")});
    for (const auto &request : {spaced, equals}) {
        QCOMPARE(request.kind, Kind::Call);
        QVERIFY(request.json);
        const QJsonObject expected{{QStringLiteral("level"), QStringLiteral("info")},
                                   {QStringLiteral("lines"), 3},
                                   {QStringLiteral("since"), 4120},
                                   {QStringLiteral("torrent"), QStringLiteral("3f9a1c0e")},
                                   {QStringLiteral("server"), QStringLiteral("attic")}};
        QCOMPARE(request.options, expected);
    }
    const auto moves = AgentWords::parse(
        {QStringLiteral("moves"), QStringLiteral("--landed"), QStringLiteral("20")});
    QCOMPARE(moves.kind, Kind::Call);
    QCOMPARE(moves.options.value(QStringLiteral("landed")).toInt(), 20);
    const auto errors =
        AgentWords::parse({QStringLiteral("errors"), QStringLiteral("--server={0b9e}")});
    QCOMPARE(errors.options.value(QStringLiteral("server")).toString(), QStringLiteral("{0b9e}"));
}

void TestAgentWords::unknownOptionIsUsage_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("arg");
    QTest::newRow("status takes no server")
        << QStringList{"status", "--server", "attic"} << "--server";
    QTest::newRow("errors takes no level")
        << QStringList{"errors", "--level=info"} << "--level=info";
    QTest::newRow("a positional") << QStringList{"errors", "attic"} << "attic";
    QTest::newRow("a short option") << QStringList{"moves", "-l", "3"} << "-l";
    QTest::newRow("a value missing") << QStringList{"log", "--lines"} << "--lines";
    QTest::newRow("a value that is an option")
        << QStringList{"log", "--server", "--json"} << "--server";
    QTest::newRow("given twice") << QStringList{"errors", "--server", "a", "--server=b"}
                                 << "--server=b";
    QTest::newRow("json with a value") << QStringList{"status", "--json=yes"} << "--json=yes";
    QTest::newRow("an unknown level") << QStringList{"log", "--level", "loud"} << "--level loud";
    QTest::newRow("an empty server") << QStringList{"errors", "--server="} << "--server=";
    QTest::newRow("help takes no options") << QStringList{"help", "--json"} << "--json";
}

void TestAgentWords::unknownOptionIsUsage()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, arg);
    const auto request = AgentWords::parse(arguments);
    QCOMPARE(request.kind, Kind::Usage);
    QCOMPARE(request.message,
             QStringLiteral("omaseed: %1 doesn't take \"%2\". omaseed help %1 says what it takes.")
                 .arg(arguments.first(), arg));
    QCOMPARE(request.json, arguments.contains(QStringLiteral("--json")));
}

void TestAgentWords::numbersOutOfRangeAreUsage_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("lines 0") << QStringList{"log", "--lines", "0"} << false;
    QTest::newRow("lines 1") << QStringList{"log", "--lines", "1"} << true;
    QTest::newRow("lines 2000") << QStringList{"log", "--lines=2000"} << true;
    QTest::newRow("lines 2001") << QStringList{"log", "--lines=2001"} << false;
    QTest::newRow("lines abc") << QStringList{"log", "--lines=abc"} << false;
    QTest::newRow("lines 2.5") << QStringList{"log", "--lines=2.5"} << false;
    QTest::newRow("landed 0") << QStringList{"moves", "--landed=0"} << false;
    QTest::newRow("landed 20") << QStringList{"moves", "--landed=20"} << true;
    QTest::newRow("landed 21") << QStringList{"moves", "--landed=21"} << false;
    QTest::newRow("since 0") << QStringList{"log", "--since=0"} << true;
    QTest::newRow("since -1") << QStringList{"log", "--since=-1"} << false;
}

void TestAgentWords::numbersOutOfRangeAreUsage()
{
    QFETCH(QStringList, arguments);
    QFETCH(bool, accepted);
    QCOMPARE(AgentWords::parse(arguments).kind, accepted ? Kind::Call : Kind::Usage);
}

void TestAgentWords::helpAndPages()
{
    // C45 design 3: `help`, `help WORD` and `WORD --help` print a page; anything else is usage.
    auto request = AgentWords::parse({QStringLiteral("help")});
    QCOMPARE(request.kind, Kind::Page);
    QCOMPARE(request.page, QString());
    request = AgentWords::parse({QStringLiteral("help"), QStringLiteral("errors")});
    QCOMPARE(request.kind, Kind::Page);
    QCOMPARE(request.page, QStringLiteral("errors"));
    for (const char *flag : {"--help", "-h"}) {
        request = AgentWords::parse(
            {QStringLiteral("errors"), QStringLiteral("--bogus"), QString::fromLatin1(flag)});
        QCOMPARE(request.kind, Kind::Page);
        QCOMPARE(request.page, QStringLiteral("errors"));
    }
    request = AgentWords::parse({QStringLiteral("help"), QStringLiteral("--help")});
    QCOMPARE(request.kind, Kind::Page);
    QCOMPARE(request.page, QStringLiteral("help"));
    request = AgentWords::parse({QStringLiteral("help"), QStringLiteral("torrents")});
    QCOMPARE(request.kind, Kind::Usage);
    QCOMPARE(request.message,
             QStringLiteral("omaseed: there's no word \"torrents\". omaseed --help lists them."));
    request = AgentWords::parse(
        {QStringLiteral("help"), QStringLiteral("errors"), QStringLiteral("log")});
    QCOMPARE(request.kind, Kind::Usage);
}

void TestAgentWords::helpPageSplicesAboveOptions()
{
    const QString qt = QStringLiteral(
        "Usage: /usr/bin/omaseed [options] [magnet: links or .torrent files...]\n"
        "omaseed — torrents for Omarchy, powered by qBittorrent, Transmission or Deluge.\n"
        "\n"
        "Options:\n"
        "  -h, --help   Displays help on commandline options.\n"
        "\n"
        "Arguments:\n"
        "  uris         magnet: links or .torrent files to add.\n");
    const QString page = AgentWords::helpPage(qt);
    const QStringList lines = page.split(QLatin1Char('\n'));
    QCOMPARE(lines.at(0),
             QStringLiteral("Usage: omaseed [options] [magnet: links or .torrent files...]"));
    QCOMPARE(lines.at(1), QStringLiteral("       omaseed <word> [arguments] [--json]"));
    QVERIFY(lines.at(2).startsWith(QStringLiteral("omaseed — torrents")));
    const qsizetype words =
        lines.indexOf(QStringLiteral("Your library, from the running omaseed:"));
    const qsizetype options = lines.indexOf(QStringLiteral("Options:"));
    QVERIFY(words > 2);
    QVERIFY(options > words);
    for (const char *word : {"status", "errors", "log", "moves"})
        QVERIFY(page.contains(QStringLiteral("\n  %1 ").arg(QString::fromLatin1(word))));
    QVERIFY(
        page.contains(QStringLiteral("Every word takes --json. omaseed help <word> says more.")));
    QVERIFY(page.contains(QStringLiteral("Coding agents: omaseed --skill prints your guide.")));
    // Qt's options and arguments as they are, after the block.
    QVERIFY(page.endsWith(QStringLiteral(
        "Options:\n  -h, --help   Displays help on commandline options.\n\nArguments:\n"
        "  uris         magnet: links or .torrent files to add.\n")));
    QVERIFY(!page.contains(QStringLiteral("/usr/bin/omaseed")));
    // The do lines and the Skins block wait for their words (C45 V20).
    QVERIFY(!page.contains(QStringLiteral("  do ")));
    QVERIFY(!page.contains(QStringLiteral("Skins")));
}

void TestAgentWords::wordPagesHaveUsageAndExit()
{
    for (const char *w : {"help", "status", "errors", "log", "moves"}) {
        const QString word = QString::fromLatin1(w);
        const QString page = AgentWords::wordPage(word);
        QVERIFY2(page.startsWith(QStringLiteral("Usage: omaseed %1").arg(word)), w);
        QVERIFY2(page.contains(QStringLiteral("\nExit: 0 ")), w);
        QVERIFY2(page.endsWith(QLatin1Char('\n')), w);
        for (const QString &line : page.split(QLatin1Char('\n')))
            QVERIFY2(line.size() <= 80, qPrintable(line));
        if (word != QLatin1String("help"))
            QVERIFY2(page.contains(QStringLiteral("--json")), w);
    }
    // The board's errors page, word for word (docs/62 §2).
    QCOMPARE(AgentWords::wordPage(QStringLiteral("errors")),
             QStringLiteral(
                 "Usage: omaseed errors [--server NAME] [--json]\n"
                 "Every torrent that needs you, why, and the window's fix for it: the rail's\n"
                 "\"needs you\". The running omaseed answers, for the server it is on.\n"
                 "  --server NAME   a server by its name or id; only the one omaseed is on can\n"
                 "                  answer (omaseed status lists them)\n"
                 "  --json          one JSON object; omaseed --skill describes its fields\n"
                 "Exit: 0 answered (whatever it found) · 2 usage · 3 omaseed isn't running ·\n"
                 "      4 that server can't answer now\n"));
    QVERIFY(AgentWords::wordPage(QStringLiteral("do")).isEmpty());
    QVERIFY(AgentWords::wordPage(QStringLiteral("status"))
                .contains(QStringLiteral("whether the server answers or not")));
}

void TestAgentWords::exitForEveryCode_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("exit");
    QTest::newRow("answered") << "" << 0;
    for (const char *code : {"usage", "unknown-server", "ambiguous"})
        QTest::newRow(code) << code << 2;
    for (const char *code : {"not-running", "too-old", "no-bus", "no-reply"})
        QTest::newRow(code) << code << 3;
    for (const char *code :
         {"not-current", "unreachable", "no-log", "unknown-torrent", "first-sync"})
        QTest::newRow(code) << code << 4;
    for (const char *code : {"not-allowed", "never", "refused", "no-answer"})
        QTest::newRow(code) << code << 5;
    QTest::newRow("failed") << "failed" << 6;
    QTest::newRow("a code it doesn't know") << "zebra" << 1;
}

void TestAgentWords::exitForEveryCode()
{
    QFETCH(QString, code);
    QFETCH(int, exit);
    QCOMPARE(AgentWords::exitFor(code), exit);
}

void TestAgentWords::failureObjectShape()
{
    const QDateTime at(QDate(2026, 11, 12), QTime(14, 5, 40), QTimeZone::UTC);
    QCOMPARE(AgentWords::isoUtc(at).toString(), QStringLiteral("2026-11-12T14:05:40Z"));
    QCOMPARE(AgentWords::isoUtc(at.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(3600))).toString(),
             QStringLiteral("2026-11-12T14:05:40Z"));
    QVERIFY(AgentWords::isoUtc(QDateTime()).isNull());

    // The CLI's own: no omaseed key; v, command, at lead in that order.
    const QByteArray cli = AgentWords::failure(QStringLiteral("errors"),
                                               QStringLiteral("not-running"),
                                               QStringLiteral("omaseed isn't \"running\"."),
                                               at);
    QVERIFY2(cli.startsWith(R"({"v":1,"command":"errors","at":"2026-11-12T14:05:40Z","error":)"),
             cli.constData());
    QJsonParseError error;
    const QJsonObject object = QJsonDocument::fromJson(cli, &error).object();
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(!object.contains(QStringLiteral("omaseed")));
    const QJsonObject failure = object.value(QStringLiteral("error")).toObject();
    QCOMPARE(failure.value(QStringLiteral("code")).toString(), QStringLiteral("not-running"));
    QCOMPARE(failure.value(QStringLiteral("message")).toString(),
             QStringLiteral("omaseed isn't \"running\"."));

    // The app's: omaseed after at.
    const QByteArray app = AgentWords::failure(QStringLiteral("log"),
                                               QStringLiteral("no-log"),
                                               QStringLiteral("…"),
                                               at,
                                               QStringLiteral("0.14.0"));
    QVERIFY2(
        app.startsWith(
            R"({"v":1,"command":"log","at":"2026-11-12T14:05:40Z","omaseed":"0.14.0","error":)"),
        app.constData());
    QVERIFY(!app.contains('\n'));
}

QTEST_GUILESS_MAIN(TestAgentWords)
#include "tst_agentwords.moc"
