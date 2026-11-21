// DECISIONS C45 V1, V2, V4, V13 and C37 U10: the words' table, their arguments, the help page
// and each word's page, the exit map and the failure object.

#include "core/AgentGate.hpp"
#include "core/AgentWords.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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
    void checkAndSkinAreWordsNow();
    void doIsAWordNow();
    void doParses_data();
    void doParses();
    void doUsage_data();
    void doUsage();
    void skinSubwords();
    void positionalsCounted_data();
    void positionalsCounted();
    void exitForSkinCodes();
    void helpPageHasTheSkinsBlock();
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
    void skillIsAWord();
    void skillTakesAPageOrLinkWords();
    void skillTakesOnlyOne();
    void anUnknownPageIsUsage();
    void helpPageNamesSkillLink();
    void wordPagesPointAtTheLibraryPage();
    void skillHasAWordPage();
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

void TestAgentWords::checkAndSkinAreWordsNow()
{
    // C37 U3, U5, U6 (#223): check and skin left the reserved list for the table.
    for (const char *word : {"check", "skin"}) {
        const QString w = QString::fromLatin1(word);
        QVERIFY(AgentWords::isWord(w));
        QVERIFY(!AgentWords::isReserved(w));
        QCOMPARE(AgentWords::parse({QStringLiteral("help"), w}).kind, Kind::Page);
        QCOMPARE(AgentWords::parse({w, QStringLiteral("--help")}).page, w);
        QVERIFY(!AgentWords::wordPage(w).isEmpty());
    }
    // Only the first argument counts (C37 U10): ./check and Check are files.
    QVERIFY(!AgentWords::isWord(QStringLiteral("./check")));
    QVERIFY(!AgentWords::isWord(QStringLiteral("Skin")));
    const auto check = AgentWords::parse(
        {QStringLiteral("check"), QStringLiteral("warm"), QStringLiteral("--theme=paper-light")});
    QCOMPARE(check.kind, Kind::Local);
    QCOMPARE(check.command(), QStringLiteral("check"));
    QCOMPARE(check.positionals, QStringList{QStringLiteral("warm")});
    QCOMPARE(check.options.value(QStringLiteral("theme")).toString(),
             QStringLiteral("paper-light"));
}

void TestAgentWords::doIsAWordNow()
{
    // C45 V15 (#292): do left the reserved list for the table; nothing is reserved now.
    const QString w = QStringLiteral("do");
    QVERIFY(AgentWords::isWord(w));
    QVERIFY(!AgentWords::isReserved(w));
    QCOMPARE(AgentWords::parse({QStringLiteral("help"), w}).kind, Kind::Page);
    QCOMPARE(AgentWords::parse({w, QStringLiteral("--help")}).page, w);
    QVERIFY(AgentWords::wordPage(w).startsWith(QStringLiteral("Usage: omaseed do <action>")));
    QCOMPARE(AgentWords::method(w), QStringLiteral("Do"));
    // It waits for the ask (2 minutes, V17) and then the server (assumed: + 30 s).
    QCOMPARE(AgentWords::kAskTimeoutMs, 150000);
    QVERIFY(!AgentWords::isWord(QStringLiteral("./do")));
}

void TestAgentWords::doParses_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("options");
    QTest::addColumn<bool>("json");

    // Prefixes are kept as given (lower case); the app matches them (V15).
    QTest::newRow("recheck") << QStringList{"recheck", "3F9A1C0E"}
                             << R"({"action":"recheck","torrents":["3f9a1c0e"]})" << false;
    QTest::newRow("two torrents, --json")
        << QStringList{"retry", "3f9a1c0e", "--json", "deadbeefcafe"}
        << R"({"action":"retry","torrents":["3f9a1c0e","deadbeefcafe"]})" << true;
    QTest::newRow("start-here dry run")
        << QStringList{"start-here", "--dry-run", "3f9a1c0e"}
        << R"({"action":"start-here","dryRun":true,"torrents":["3f9a1c0e"]})" << false;
    QTest::newRow("pause, no torrents") << QStringList{"pause"} << R"({"action":"pause"})" << false;
    QTest::newRow("resume --server") << QStringList{"resume", "--server=attic"}
                                     << R"({"action":"resume","server":"attic"})" << false;
    QTest::newRow("move --to")
        << QStringList{"move", "3f9a1c0e", "--to", "/data/music-new"}
        << R"({"action":"move","to":"/data/music-new","torrents":["3f9a1c0e"]})" << false;
    QTest::newRow("locate --to=")
        << QStringList{"locate", "3f9a1c0e", "--to=/data/shelf-b"}
        << R"({"action":"locate","to":"/data/shelf-b","torrents":["3f9a1c0e"]})" << false;
    QTest::newRow("move-back") << QStringList{"move-back", "j-5e02"}
                               << R"({"action":"move-back","move":"j-5e02"})" << false;
    QTest::newRow("--list") << QStringList{"--list"} << R"({"list":true})" << false;
    QTest::newRow("--list --json") << QStringList{"--list", "--json"} << R"({"list":true})" << true;
    // C50 GT4: the CLI reads the file (or stdin) and sends its text (PLAN Q13).
    QTest::newRow("--plan FILE") << QStringList{"--plan", "plan.json"}
                                 << R"({"planFile":"plan.json"})" << false;
    QTest::newRow("--plan - --dry-run --json") << QStringList{"--plan", "-", "--dry-run", "--json"}
                                               << R"({"planFile":"-","dryRun":true})" << true;
    QTest::newRow("--plan= --server") << QStringList{"--plan=p.json", "--server", "attic"}
                                      << R"({"planFile":"p.json","server":"attic"})" << false;
    // C45 V14: never words reach the app, which answers never (PLAN Q4).
    QTest::newRow("delete") << QStringList{"delete", "3f9a1c0e"}
                            << R"({"action":"delete","torrents":["3f9a1c0e"]})" << false;
    QTest::newRow("rename, no torrent")
        << QStringList{"rename"} << R"({"action":"rename","torrents":[]})" << false;
}

void TestAgentWords::doParses()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, options);
    QFETCH(bool, json);
    const auto request = AgentWords::parse(QStringList{QStringLiteral("do")} + arguments);
    QVERIFY2(request.kind == Kind::Call, qPrintable(request.message));
    QCOMPARE(request.word, QStringLiteral("do"));
    QCOMPARE(request.command(), QStringLiteral("do"));
    QCOMPARE(request.json, json);
    QCOMPARE(request.options, QJsonDocument::fromJson(options.toUtf8()).object());
}

void TestAgentWords::doUsage_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("message");

    QTest::newRow("nothing") << QStringList{}
                             << "omaseed: do takes an action and its torrents: omaseed do recheck "
                                "3f9a1c0e. omaseed do --list says what it may do.";
    QTest::newRow("unknown action")
        << QStringList{"rechek", "3f9a1c0e"}
        << "omaseed: do doesn't take \"rechek\". omaseed help do says what it takes.";
    QTest::newRow("no torrents") << QStringList{"recheck"}
                                 << "omaseed: do recheck takes the torrents' hashes, or 8 or more "
                                    "of their first characters: omaseed errors gives them.";
    // C50 GT1: no wording of a request turns it into a delete.
    QTest::newRow("--delete-files")
        << QStringList{"remove", "9c40e7a2", "--delete-files"}
        << "omaseed do never deletes files: remove takes the torrent off the list and nothing "
           "else.";
    QTest::newRow("--delete-files on delete")
        << QStringList{"delete", "9c40e7a2", "--delete-files"}
        << "omaseed do never deletes files: remove takes the torrent off the list and nothing "
           "else.";
    QTest::newRow("--to on recheck")
        << QStringList{"recheck", "3f9a1c0e", "--to", "/x"}
        << "omaseed: do doesn't take \"--to\". omaseed help do says what it takes.";
    QTest::newRow("move without --to") << QStringList{"move", "3f9a1c0e"}
                                       << "omaseed: do move takes --to FOLDER. omaseed help do "
                                          "says how.";
    QTest::newRow("pause with a hash")
        << QStringList{"pause", "3f9a1c0e"}
        << "omaseed: do pause takes no torrents: it holds the whole server, as the window's "
           "pause does.";
    QTest::newRow("move-back two") << QStringList{"move-back", "j-1", "j-2"}
                                   << "omaseed: do move-back takes one move's id: omaseed moves "
                                      "lists them.";
    QTest::newRow("--list with an action")
        << QStringList{"--list", "recheck"}
        << "omaseed: do doesn't take \"recheck\". omaseed help do says what it takes.";
    QTest::newRow("--list --dry-run")
        << QStringList{"--list", "--dry-run"}
        << "omaseed: do doesn't take \"--dry-run\". omaseed help do says what it takes.";
    // C50 GT4: a plan names its own actions and folders.
    QTest::newRow("--plan with an action")
        << QStringList{"--plan", "plan.json", "recheck"}
        << "omaseed: do doesn't take \"recheck\". omaseed help do says what it takes.";
    QTest::newRow("--plan with --to")
        << QStringList{"--plan", "plan.json", "--to", "/x"}
        << "omaseed: do doesn't take \"--to\". omaseed help do says what it takes.";
    QTest::newRow("--plan without a file")
        << QStringList{"--plan"}
        << "omaseed: do doesn't take \"--plan\". omaseed help do says what it takes.";
    QTest::newRow("--list --plan")
        << QStringList{"--list", "--plan", "-"}
        << "omaseed: do doesn't take \"--plan\". omaseed help do says what it takes.";
    QTest::newRow("unknown option")
        << QStringList{"recheck", "3f9a1c0e", "--force"}
        << "omaseed: do doesn't take \"--force\". omaseed help do says what it takes.";
    QTest::newRow("--to twice") << QStringList{"move", "3f9a1c0e", "--to", "/a", "--to", "/b"}
                                << "omaseed: do doesn't take \"--to\". omaseed help do says what "
                                   "it takes.";
}

void TestAgentWords::doUsage()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, message);
    const auto request =
        AgentWords::parse(QStringList{QStringLiteral("do")} + arguments + QStringList{"--json"});
    QCOMPARE(request.kind, Kind::Usage);
    QVERIFY(request.json);
    QCOMPARE(request.message, message);
}

void TestAgentWords::skinSubwords()
{
    auto request = AgentWords::parse({QStringLiteral("skin"), QStringLiteral("list")});
    QCOMPARE(request.kind, Kind::Local);
    QCOMPARE(request.word, QStringLiteral("skin"));
    QCOMPARE(request.subword, QStringLiteral("list"));
    QCOMPARE(request.command(), QStringLiteral("skin list"));
    QVERIFY(request.positionals.isEmpty());

    request = AgentWords::parse({QStringLiteral("skin"),
                                 QStringLiteral("clone"),
                                 QStringLiteral("console"),
                                 QStringLiteral("warm.v2"),
                                 QStringLiteral("--json")});
    QCOMPARE(request.kind, Kind::Local);
    QVERIFY(request.json);
    QCOMPARE(request.command(), QStringLiteral("skin clone"));
    QCOMPARE(request.positionals,
             (QStringList{QStringLiteral("console"), QStringLiteral("warm.v2")}));

    // use asks the running omaseed (C37 U6), with the name as its option.
    request = AgentWords::parse({QStringLiteral("skin"),
                                 QStringLiteral("--json"),
                                 QStringLiteral("use"),
                                 QStringLiteral("warm")});
    QCOMPARE(request.kind, Kind::Call);
    QCOMPARE(request.command(), QStringLiteral("skin use"));
    QCOMPARE(AgentWords::method(request.command()), QStringLiteral("UseSkin"));
    QCOMPARE(request.options, (QJsonObject{{QStringLiteral("name"), QStringLiteral("warm")}}));

    const QString takes =
        QStringLiteral("omaseed: skin takes list, clone or use. omaseed help skin says how.");
    for (const QStringList &arguments : {QStringList{"skin"},
                                         QStringList{"skin", "remove", "warm"},
                                         QStringList{"skin", "--json"}}) {
        request = AgentWords::parse(arguments);
        QCOMPARE(request.kind, Kind::Usage);
        QCOMPARE(request.message, takes);
    }
    // skin's subwords take --json only.
    request = AgentWords::parse({QStringLiteral("skin"),
                                 QStringLiteral("list"),
                                 QStringLiteral("--theme"),
                                 QStringLiteral("x")});
    QCOMPARE(request.kind, Kind::Usage);
}

void TestAgentWords::positionalsCounted_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QString>("message");
    const QString takes =
        QStringLiteral("omaseed: skin takes list, clone or use. omaseed help skin says how.");
    QTest::newRow("check alone")
        << QStringList{"check"}
        << QStringLiteral("omaseed: check takes a skin's name: omaseed check warm. omaseed help "
                          "check says how.");
    QTest::newRow("check two")
        << QStringList{"check", "warm", "paper"}
        << QStringLiteral(
               "omaseed: check doesn't take \"paper\". omaseed help check says what it takes.");
    QTest::newRow("list one") << QStringList{"skin", "list", "warm"}
                              << QStringLiteral("omaseed: skin list doesn't take \"warm\". "
                                                "omaseed help skin says what it takes.");
    QTest::newRow("clone one") << QStringList{"skin", "clone", "console"} << takes;
    QTest::newRow("clone three")
        << QStringList{"skin", "clone", "console", "warm", "paper"}
        << QStringLiteral("omaseed: skin clone doesn't take \"paper\". omaseed help skin says "
                          "what it takes.");
    QTest::newRow("use none") << QStringList{"skin", "use"} << takes;
    QTest::newRow("a positional elsewhere")
        << QStringList{"status", "warm"}
        << QStringLiteral(
               "omaseed: status doesn't take \"warm\". omaseed help status says what it takes.");
}

void TestAgentWords::positionalsCounted()
{
    QFETCH(QStringList, arguments);
    QFETCH(QString, message);
    const auto request = AgentWords::parse(arguments);
    QCOMPARE(request.kind, Kind::Usage);
    QCOMPARE(request.message, message);
}

void TestAgentWords::exitForSkinCodes()
{
    // C37 U3, U5, U6 in C45 V13's table.
    QCOMPARE(AgentWords::exitFor(QStringLiteral("unreadable")), 1);
    for (const char *code : {"unknown-skin", "bad-name", "skin-exists", "no-theme"})
        QCOMPARE(AgentWords::exitFor(QString::fromLatin1(code)), 2);
    QCOMPARE(AgentWords::exitFor(QStringLiteral("not-written")), 5);
}

void TestAgentWords::helpPageHasTheSkinsBlock()
{
    const QString page = AgentWords::helpPage(
        QStringLiteral("Usage: omaseed [options]\nomaseed — torrents.\n\nOptions:\n  -h\n"));
    const QStringList lines = page.split(QLatin1Char('\n'));
    const qsizetype library =
        lines.indexOf(QStringLiteral("Your library, from the running omaseed:"));
    const qsizetype skins =
        lines.indexOf(QStringLiteral("Your skins, in ~/.config/omaseed/skins/:"));
    const qsizetype every =
        lines.indexOf(QStringLiteral("Every word takes --json. omaseed help <word> says more."));
    QVERIFY(library > 0);
    QVERIFY(skins > library);
    QVERIFY(every > skins);
    QCOMPARE(lines.mid(skins + 1, 4),
             (QStringList{
                 QStringLiteral("  skin list    every skin, where it lives, whether it reads"),
                 QStringLiteral("  skin clone   start one from a copy: omaseed skin clone console "
                                "warm"),
                 QStringLiteral("  check        what a skin reads and drops, light and dark "
                                "pictures, no display"),
                 QStringLiteral("  skin use     draw it in the open window, and keep it")}));
    QCOMPARE(lines.at(skins + 5), QString());
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

    // C50 GT7: `help agents [--for AGENT]` is the CLI's own, its agent checked.
    request = AgentWords::parse({QStringLiteral("help"), QStringLiteral("agents")});
    QCOMPARE(request.kind, Kind::Local);
    QCOMPARE(request.command(), QStringLiteral("help agents"));
    QVERIFY(request.options.isEmpty());
    for (const QStringList &given : {QStringList{"help", "agents", "--for", "codex", "--json"},
                                     QStringList{"help", "agents", "--json", "--for=codex"}}) {
        request = AgentWords::parse(given);
        QCOMPARE(request.kind, Kind::Local);
        QVERIFY(request.json);
        QCOMPARE(request.options.value(QStringLiteral("for")).toString(), QStringLiteral("codex"));
    }
    for (const QStringList &given : {QStringList{"help", "agents", "--for", "cursor"},
                                     QStringList{"help", "agents", "--for"},
                                     QStringList{"help", "agents", "--for", "pi", "--for", "pi"},
                                     QStringList{"help", "agents", "claude"},
                                     QStringList{"help", "agents", "--server", "attic"}}) {
        request = AgentWords::parse(given);
        QCOMPARE(request.kind, Kind::Usage);
        QCOMPARE(request.message,
                 QStringLiteral("omaseed: help agents takes --for claude|codex|pi|gemini. "
                                "omaseed help help says how."));
    }
    request = AgentWords::parse(
        {QStringLiteral("help"), QStringLiteral("agents"), QStringLiteral("--help")});
    QCOMPARE(request.kind, Kind::Page);
    QCOMPARE(request.page, QStringLiteral("help"));
    QVERIFY(AgentWords::wordPage(QStringLiteral("help"))
                .contains(QStringLiteral("omaseed help agents [--for claude|codex|pi|gemini]")));
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
    QVERIFY(page.contains(
        QStringLiteral("Coding agents: omaseed skill link, or omaseed skill to read the guide.")));
    // Qt's options and arguments as they are, after the block.
    QVERIFY(page.endsWith(QStringLiteral(
        "Options:\n  -h, --help   Displays help on commandline options.\n\nArguments:\n"
        "  uris         magnet: links or .torrent files to add.\n")));
    QVERIFY(!page.contains(QStringLiteral("/usr/bin/omaseed")));
    // do's two lines (C45 V15, docs/62 §2).
    QVERIFY(page.contains(
        QStringLiteral("\n  do         a fix the window offers, if you let agents act\n"
                       "             (Settings › General › Agents; moves always ask you)\n")));
}

void TestAgentWords::wordPagesHaveUsageAndExit()
{
    for (const char *w :
         {"help", "status", "errors", "log", "moves", "check", "skin", "skill", "do"}) {
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
                 "  --json          one JSON object; omaseed skill library describes its fields\n"
                 "Exit: 0 answered (whatever it found) · 2 usage · 3 omaseed isn't running ·\n"
                 "      4 that server can't answer now\n"));
    // do's page names every action of every kind (#297: locate and remove were missing),
    // and points at the wall (C50 GT7).
    const QString doPage = AgentWords::wordPage(QStringLiteral("do"));
    for (const auto kind : {AgentGate::Kind::Fixes,
                            AgentGate::Kind::Moves,
                            AgentGate::Kind::Remove})
        for (const QString &action : AgentGate::actionsOf(kind))
            QVERIFY2(doPage.contains(QRegularExpression(
                         QStringLiteral("[ ,]%1[ ,;:]").arg(QRegularExpression::escape(action)))),
                     qPrintable(action));
    QVERIFY(doPage.contains(QStringLiteral("omaseed help agents")));
    QVERIFY(AgentWords::wordPage(QStringLiteral("nope")).isEmpty());
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
    for (const char *code :
         {"not-allowed", "never", "refused", "no-answer", "not-removable", "not-found"})
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

// DECISIONS C48 (docs/65 §5): the word skill. Its pages come from AgentGuide::pages(), which
// core can't read, so the parser takes them.
static const QStringList kPages{
    QStringLiteral("library"), QStringLiteral("skins"), QStringLiteral("files")};

void TestAgentWords::skillIsAWord()
{
    QVERIFY(AgentWords::isWord(QStringLiteral("skill")));
    QVERIFY(!AgentWords::isReserved(QStringLiteral("skill")));
    QVERIFY(!AgentWords::isWord(QStringLiteral("./skill")));
    QVERIFY(!AgentWords::isWord(QStringLiteral("--skill")));
    const auto request = AgentWords::parse({QStringLiteral("skill")}, kPages);
    QCOMPARE(request.kind, Kind::Local);
    QCOMPARE(request.word, QStringLiteral("skill"));
    QCOMPARE(request.command(), QStringLiteral("skill"));
    QVERIFY(request.subword.isEmpty());
    QVERIFY(!request.json);
    QVERIFY(AgentWords::isSkillPrint(request));
    QCOMPARE(AgentWords::parse({QStringLiteral("help"), QStringLiteral("skill")}).page,
             QStringLiteral("skill"));
    QCOMPARE(AgentWords::parse({QStringLiteral("skill"), QStringLiteral("--help")}, kPages).kind,
             Kind::Page);
}

void TestAgentWords::skillTakesAPageOrLinkWords()
{
    for (const QString &page : kPages) {
        const auto request =
            AgentWords::parse({QStringLiteral("skill"), page, QStringLiteral("--json")}, kPages);
        QCOMPARE(request.kind, Kind::Local);
        QCOMPARE(request.subword, page);
        QCOMPARE(request.command(), QStringLiteral("skill ") + page);
        QVERIFY(request.json);
        QVERIFY(AgentWords::isSkillPrint(request));
    }
    for (const char *w : {"link", "unlink", "status"}) {
        const QString word = QString::fromLatin1(w);
        const auto request = AgentWords::parse({QStringLiteral("skill"), word}, kPages);
        QCOMPARE(request.kind, Kind::Local);
        QCOMPARE(request.command(), QStringLiteral("skill ") + word);
        QVERIFY(!AgentWords::isSkillPrint(request));
    }
    // A page that is only a file in ref/ is a page (C49's layouts): the list decides.
    const auto layouts = AgentWords::parse({QStringLiteral("skill"), QStringLiteral("layouts")},
                                           kPages + QStringList{"layouts"});
    QCOMPARE(layouts.kind, Kind::Local);
    QCOMPARE(layouts.subword, QStringLiteral("layouts"));
}

void TestAgentWords::skillTakesOnlyOne()
{
    auto request = AgentWords::parse(
        {QStringLiteral("skill"), QStringLiteral("library"), QStringLiteral("skins")}, kPages);
    QCOMPARE(request.kind, Kind::Usage);
    QCOMPARE(request.message,
             QStringLiteral("omaseed: skill library doesn't take \"skins\". omaseed help skill "
                            "library says what it takes."));
    request = AgentWords::parse(
        {QStringLiteral("skill"), QStringLiteral("--server"), QStringLiteral("x")}, kPages);
    QCOMPARE(request.kind, Kind::Usage);
    request = AgentWords::parse({QStringLiteral("skill"),
                                 QStringLiteral("link"),
                                 QStringLiteral("--json"),
                                 QStringLiteral("now")},
                                kPages);
    QCOMPARE(request.kind, Kind::Usage);
    QVERIFY(request.json);
}

void TestAgentWords::anUnknownPageIsUsage()
{
    const QString takes = QStringLiteral(
        "omaseed: skill takes a page (library, skins, files), link, unlink or status. omaseed "
        "help skill says how.");
    for (const char *w : {"layouts", "Library", "library.md", "ref/skins.md", "map"}) {
        const auto request =
            AgentWords::parse({QStringLiteral("skill"), QString::fromLatin1(w)}, kPages);
        QCOMPARE(request.kind, Kind::Usage);
        QCOMPARE(request.message, takes);
    }
}

void TestAgentWords::helpPageNamesSkillLink()
{
    const QString page = AgentWords::helpPage(
        QStringLiteral("Usage: omaseed [options]\nomaseed — torrents.\n\nOptions:\n  -h\n"));
    const QStringList lines = page.split(QLatin1Char('\n'));
    const qsizetype every =
        lines.indexOf(QStringLiteral("Every word takes --json. omaseed help <word> says more."));
    QVERIFY(every > 0);
    // AS8: the block's last line, the link first.
    QCOMPARE(lines.at(every + 1),
             QStringLiteral("Coding agents: omaseed skill link, or omaseed skill to read the "
                            "guide."));
    QCOMPARE(lines.at(every + 2), QString());
    QVERIFY(!page.contains(QStringLiteral("--skill")));
}

void TestAgentWords::wordPagesPointAtTheLibraryPage()
{
    for (const char *w : {"status", "errors", "log", "moves", "check", "skin"}) {
        const QString page = AgentWords::wordPage(QString::fromLatin1(w));
        QVERIFY2(page.contains(QStringLiteral(
                     "  --json          one JSON object; omaseed skill library describes its "
                     "fields\n")),
                 w);
        QVERIFY2(!page.contains(QStringLiteral("--skill")), w);
    }
}

void TestAgentWords::skillHasAWordPage()
{
    const QString page = AgentWords::wordPage(QStringLiteral("skill"));
    QVERIFY(page.startsWith(QStringLiteral("Usage: omaseed skill [page] [--json]\n")));
    for (const char *w : {"link", "unlink", "status", "--json"})
        QVERIFY2(page.contains(QStringLiteral("  %1 ").arg(QString::fromLatin1(w))), w);
    QVERIFY(page.contains(QStringLiteral("No one answers")));
    QVERIFY(page.endsWith(QStringLiteral("Exit: 0 done · 2 usage · 5 not written\n")));
    for (const QString &line : page.split(QLatin1Char('\n')))
        QVERIFY2(line.size() <= 80, qPrintable(line));
}

QTEST_GUILESS_MAIN(TestAgentWords)
#include "tst_agentwords.moc"
