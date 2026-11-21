// #296 (DECISIONS C50 GT4): an agent's plan read and checked for its shape before the app
// checks each line: what is usage, a word do never runs kept for the app to refuse, a line's
// options, the ask's groups by action and folder, and the plan's last line.

#include "core/AgentPlan.hpp"

#include <QJsonDocument>
#include <QTest>

namespace AgentPlan = omaseed::core::AgentPlan;

class TestAgentPlan : public QObject
{
    Q_OBJECT

private slots:
    void reads();
    void usage_data();
    void usage();
    void neverWordIsALine();
    void options();
    void groupsByActionAndFolder();
    void outcomeWords();
};

void TestAgentPlan::reads()
{
    const AgentPlan::Plan plan = AgentPlan::parse(QStringLiteral(R"({
        "v": 1, "server": "attic", "lines": [
        {"action": "recheck", "torrents": ["3F9A1C0E", "a20c55d1", "3f9a1c0e"]},
        {"action": "locate", "torrents": ["71ad02bc"], "to": "/data/shelf-b"},
        {"action": "move-back", "move": "0a1b2c3d"},
        {"action": "pause"}]})"));
    QVERIFY2(plan.usage.isEmpty(), qPrintable(plan.usage));
    QCOMPARE(plan.server, QStringLiteral("attic"));
    QCOMPARE(plan.lines.size(), 4);
    // Lower-cased, a repeat dropped.
    QCOMPARE(plan.lines.at(0).torrents, (QStringList{"3f9a1c0e", "a20c55d1"}));
    QCOMPARE(plan.lines.at(1).to, QStringLiteral("/data/shelf-b"));
    QCOMPARE(plan.lines.at(2).move, QStringLiteral("0a1b2c3d"));
    QVERIFY(plan.lines.at(3).torrents.isEmpty());
    // No v and no server: still a plan.
    QVERIFY(AgentPlan::parse(
                QStringLiteral(R"({"lines":[{"action":"retry","torrents":["3f9a1c0e"]}]})"))
                .usage.isEmpty());
}

void TestAgentPlan::usage_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("words");

    QTest::newRow("not JSON") << "recheck 3f9a1c0e" << "takes a JSON plan";
    QTest::newRow("a list") << R"([{"action":"recheck"}])" << "takes a JSON plan";
    QTest::newRow("no lines") << R"({"v":1})" << "takes a JSON plan";
    QTest::newRow("empty lines") << R"({"lines":[]})" << "takes a JSON plan";
    QTest::newRow("v 2") << R"({"v":2,"lines":[{"action":"pause"}]})" << "version 1";
    QTest::newRow("other key") << R"({"lines":[{"action":"pause"}],"force":true})"
                               << "a plan doesn't take \"force\"";
    QTest::newRow("line not object") << R"({"lines":["recheck"]})" << "line 1: a line is an object";
    QTest::newRow("no action") << R"({"lines":[{"torrents":["3f9a1c0e"]}]})"
                               << "line 1: a line names its action";
    QTest::newRow("unknown action")
        << R"({"lines":[{"action":"pause"},{"action":"rechek","torrents":["3f9a1c0e"]}]})"
        << "line 2: do doesn't take \"rechek\"";
    QTest::newRow("short prefix") << R"({"lines":[{"action":"recheck","torrents":["3f9a1c"]}]})"
                                  << "line 1: “3f9a1c” isn't a torrent's hash";
    QTest::newRow("a name") << R"({"lines":[{"action":"recheck","torrents":["Big Buck Bunny"]}]})"
                            << "isn't a torrent's hash";
    QTest::newRow("no torrents") << R"({"lines":[{"action":"recheck"}]})"
                                 << "line 1: recheck takes its torrents' hashes";
    QTest::newRow("locate without to")
        << R"({"lines":[{"action":"locate","torrents":["3f9a1c0e"]}]})" << "locate takes \"to\"";
    QTest::newRow("to on recheck")
        << R"({"lines":[{"action":"recheck","torrents":["3f9a1c0e"],"to":"/x"}]})"
        << "recheck takes no \"to\"";
    QTest::newRow("pause with torrents")
        << R"({"lines":[{"action":"pause","torrents":["3f9a1c0e"]}]})" << "pause takes no torrents";
    QTest::newRow("move-back without move") << R"({"lines":[{"action":"move-back"}]})"
                                            << "move-back takes \"move\"";
    QTest::newRow("delete-files key")
        << R"({"lines":[{"action":"remove","torrents":["3f9a1c0e"],"deleteFiles":true}]})"
        << "a line doesn't take \"deleteFiles\"";
}

void TestAgentPlan::usage()
{
    QFETCH(QString, text);
    QFETCH(QString, words);
    const AgentPlan::Plan plan = AgentPlan::parse(text);
    QVERIFY2(plan.usage.contains(words), qPrintable(plan.usage));
    QVERIFY(plan.usage.startsWith(QStringLiteral("omaseed: do --plan")));
}

// C45 V14: a word do never runs is the app's to refuse (never, exit 5), not malformed.
void TestAgentPlan::neverWordIsALine()
{
    const AgentPlan::Plan plan = AgentPlan::parse(
        QStringLiteral(R"({"lines":[{"action":"delete","torrents":["3f9a1c0e"]}]})"));
    QVERIFY2(plan.usage.isEmpty(), qPrintable(plan.usage));
    QCOMPARE(plan.lines.constFirst().action, QStringLiteral("delete"));
}

void TestAgentPlan::options()
{
    AgentPlan::Line line;
    line.action = QStringLiteral("locate");
    line.torrents = {QStringLiteral("3f9a1c0e")};
    line.to = QStringLiteral("/data/b");
    QCOMPARE(
        AgentPlan::options(line, QStringLiteral("attic"), true),
        QJsonDocument::fromJson(
            R"({"action":"locate","torrents":["3f9a1c0e"],"to":"/data/b","server":"attic","dryRun":true})")
            .object());
    AgentPlan::Line back;
    back.action = QStringLiteral("move-back");
    back.move = QStringLiteral("0a1b2c3d");
    QCOMPARE(AgentPlan::options(back, QString(), false),
             QJsonDocument::fromJson(R"({"action":"move-back","move":"0a1b2c3d"})").object());
}

// GT4: asking lines by action and folder; allowed ones by action alone.
void TestAgentPlan::groupsByActionAndFolder()
{
    const AgentPlan::Plan plan = AgentPlan::parse(QStringLiteral(R"({"lines":[
        {"action":"recheck","torrents":["00000001"]},
        {"action":"locate","torrents":["00000002"],"to":"/a"},
        {"action":"locate","torrents":["00000003"],"to":"/b"},
        {"action":"move","torrents":["00000004"],"to":"/m"},
        {"action":"recheck","torrents":["00000005"]},
        {"action":"move","torrents":["00000006"],"to":"/n"},
        {"action":"move","torrents":["00000007"],"to":"/m"},
        {"action":"remove","torrents":["00000008"]}]})"));
    QVERIFY(plan.usage.isEmpty());
    const QList<bool> asks{false, false, false, true, false, true, true, true};
    const QList<AgentPlan::Group> groups = AgentPlan::groups(plan.lines, asks);
    QCOMPARE(groups.size(), 5);
    QCOMPARE(groups.at(0).action, QStringLiteral("recheck"));
    QCOMPARE(groups.at(0).lines, (QList<int>{0, 4}));
    QVERIFY(!groups.at(0).asks);
    QCOMPARE(groups.at(1).action, QStringLiteral("locate"));
    QCOMPARE(groups.at(1).lines, (QList<int>{1, 2})); // two folders, one allowed group
    QCOMPARE(groups.at(2).to, QStringLiteral("/m"));
    QCOMPARE(groups.at(2).lines, (QList<int>{3, 6}));
    QVERIFY(groups.at(2).asks);
    QCOMPARE(groups.at(3).to, QStringLiteral("/n"));
    QCOMPARE(groups.at(4).action, QStringLiteral("remove"));
}

void TestAgentPlan::outcomeWords()
{
    QCOMPARE(AgentPlan::outcome(34, 35), QStringLiteral("Done: 34 of 35 · by an agent"));
    QCOMPARE(AgentPlan::actionTitle(QStringLiteral("move-back")), QStringLiteral("Move back"));
    QCOMPARE(AgentPlan::actionTitle(QStringLiteral("recheck")), QStringLiteral("Recheck"));
}

QTEST_GUILESS_MAIN(TestAgentPlan)
#include "tst_agentplan.moc"
