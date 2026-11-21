// DECISIONS C50 GT3, GT8 (C45 V14 as amended): the gate's lines, their defaults, the decision
// per action and level, the section's round trip and `do --list` as data.

#include "core/AgentGate.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

using namespace omaseed::core;
using Decision = AgentGate::Decision;
using Kind = AgentGate::Kind;

class TestAgentGate : public QObject
{
    Q_OBJECT

private slots:
    void defaultsReadOnly();
    void kindsOfActions();
    void decidePerLevel_data();
    void decidePerLevel();
    void movesAndRemoveNeverAllow();
    void readsOffTurnsEveryActionOff();
    void unknownLevelIsTheDefault();
    void roundTrip();
    void listLinesAsTheyStand();
};

void TestAgentGate::defaultsReadOnly()
{
    const AgentGate gate;
    QVERIFY(gate.reads());
    QCOMPARE(gate.readsLevel(), QStringLiteral("on"));
    for (const Kind kind : {Kind::Fixes, Kind::Moves, Kind::Remove})
        QCOMPARE(gate.level(kind), QStringLiteral("off"));
    QCOMPARE(gate.decide(QStringLiteral("recheck")), Decision::Off);
    QCOMPARE(gate.decide(QStringLiteral("move")), Decision::Off);
    QCOMPARE(gate.decide(QStringLiteral("remove")), Decision::Off);
    // An empty section is the default too.
    QCOMPARE(AgentGate::fromJson({}), gate);
}

void TestAgentGate::kindsOfActions()
{
    for (const char *fix :
         {"retry", "start-here", "recheck", "reannounce", "pause", "resume", "dismiss", "locate"})
        QCOMPARE(AgentGate::kindOf(QString::fromLatin1(fix)), Kind::Fixes);
    QCOMPARE(AgentGate::kindOf(QStringLiteral("move")), Kind::Moves);
    QCOMPARE(AgentGate::kindOf(QStringLiteral("move-back")), Kind::Moves);
    QCOMPARE(AgentGate::kindOf(QStringLiteral("remove")), Kind::Remove);
    for (const char *never : {"delete", "rename", "category", "tags", "add", "quit", "", "Recheck"})
        QCOMPARE(AgentGate::kindOf(QString::fromLatin1(never)), Kind::None);
    QCOMPARE(AgentGate::levelsOf(Kind::Fixes),
             (QStringList{QStringLiteral("off"), QStringLiteral("ask"), QStringLiteral("allow")}));
    QCOMPARE(AgentGate::levelsOf(Kind::Moves),
             (QStringList{QStringLiteral("off"), QStringLiteral("ask")}));
    QCOMPARE(AgentGate::levelsOf(Kind::Remove), AgentGate::levelsOf(Kind::Moves));
}

void TestAgentGate::decidePerLevel_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("level");
    QTest::addColumn<QString>("action");
    QTest::addColumn<int>("decision");
    QTest::newRow("fixes off") << "fixes" << "off" << "recheck" << int(Decision::Off);
    QTest::newRow("fixes ask") << "fixes" << "ask" << "locate" << int(Decision::Ask);
    QTest::newRow("fixes allow") << "fixes" << "allow" << "retry" << int(Decision::Allow);
    QTest::newRow("moves off") << "moves" << "off" << "move" << int(Decision::Off);
    QTest::newRow("moves ask") << "moves" << "ask" << "move-back" << int(Decision::Ask);
    QTest::newRow("remove off") << "remove" << "off" << "remove" << int(Decision::Off);
    QTest::newRow("remove ask") << "remove" << "ask" << "remove" << int(Decision::Ask);
    QTest::newRow("delete, fixes allow") << "fixes" << "allow" << "delete" << int(Decision::Never);
    QTest::newRow("rename, moves ask") << "moves" << "ask" << "rename" << int(Decision::Never);
}

void TestAgentGate::decidePerLevel()
{
    QFETCH(QString, kind);
    QFETCH(QString, level);
    QFETCH(QString, action);
    QFETCH(int, decision);
    const AgentGate gate = AgentGate::fromJson(QJsonObject{{kind, level}});
    QCOMPARE(int(gate.decide(action)), decision);
}

void TestAgentGate::movesAndRemoveNeverAllow()
{
    // C50 GT3: a hand-edited "allow" reads as ask; a setter asked for allow sets ask.
    const AgentGate edited = AgentGate::fromJson(
        QJsonObject{{QStringLiteral("moves"), "allow"}, {QStringLiteral("remove"), "allow"}});
    QCOMPARE(edited.level(Kind::Moves), QStringLiteral("ask"));
    QCOMPARE(edited.level(Kind::Remove), QStringLiteral("ask"));
    QCOMPARE(edited.decide(QStringLiteral("move")), Decision::Ask);
    QCOMPARE(edited.decide(QStringLiteral("remove")), Decision::Ask);
    AgentGate gate;
    QVERIFY(gate.setLevel(Kind::Moves, QStringLiteral("allow")));
    QCOMPARE(gate.level(Kind::Moves), QStringLiteral("ask"));
    QCOMPARE(gate.toJson().value(QStringLiteral("moves")).toString(), QStringLiteral("ask"));
    QVERIFY(!gate.setLevel(Kind::Moves, QStringLiteral("ask")));
    QVERIFY(!gate.setLevel(Kind::None, QStringLiteral("allow")));
}

void TestAgentGate::readsOffTurnsEveryActionOff()
{
    AgentGate gate = AgentGate::fromJson(QJsonObject{{QStringLiteral("fixes"), "allow"},
                                                     {QStringLiteral("moves"), "ask"},
                                                     {QStringLiteral("remove"), "ask"}});
    QVERIFY(gate.setReads(false));
    QVERIFY(!gate.setReads(false));
    QCOMPARE(gate.readsLevel(), QStringLiteral("off"));
    for (const char *action : {"recheck", "move", "remove"})
        QCOMPARE(gate.decide(QString::fromLatin1(action)), Decision::Off);
    // C50 GT8: the acting lines keep their level, greyed, for when reads are back on.
    QCOMPARE(gate.level(Kind::Fixes), QStringLiteral("allow"));
    QCOMPARE(gate.decide(QStringLiteral("delete")), Decision::Never);
    QVERIFY(gate.setReads(true));
    QCOMPARE(gate.decide(QStringLiteral("recheck")), Decision::Allow);
}

void TestAgentGate::unknownLevelIsTheDefault()
{
    const AgentGate gate = AgentGate::fromJson(QJsonObject{{QStringLiteral("reads"), "maybe"},
                                                           {QStringLiteral("fixes"), "always"},
                                                           {QStringLiteral("moves"), 3},
                                                           {QStringLiteral("remove"), "ASK"}});
    QVERIFY(gate.reads());
    QCOMPARE(gate.level(Kind::Fixes), QStringLiteral("off"));
    QCOMPARE(gate.level(Kind::Moves), QStringLiteral("off"));
    QCOMPARE(gate.level(Kind::Remove), QStringLiteral("off"));
    AgentGate set;
    QVERIFY(set.setLevel(Kind::Fixes, QStringLiteral("ask")));
    QVERIFY(set.setLevel(Kind::Fixes, QStringLiteral("whatever")));
    QCOMPARE(set.level(Kind::Fixes), QStringLiteral("off"));
}

void TestAgentGate::roundTrip()
{
    AgentGate gate;
    gate.setReads(false);
    gate.setLevel(Kind::Fixes, QStringLiteral("allow"));
    gate.setLevel(Kind::Remove, QStringLiteral("ask"));
    const QDateTime at(QDate(2026, 11, 21), QTime(15, 20), QTimeZone::UTC);
    gate.setLast(QStringLiteral("plan · 34 of 35 done"), at);
    const QJsonObject section = gate.toJson();
    QCOMPARE(section.value(QStringLiteral("reads")).toString(), QStringLiteral("off"));
    QCOMPARE(section.value(QStringLiteral("fixes")).toString(), QStringLiteral("allow"));
    QCOMPARE(section.value(QStringLiteral("moves")).toString(), QStringLiteral("off"));
    QCOMPARE(section.value(QStringLiteral("remove")).toString(), QStringLiteral("ask"));
    const QJsonObject last = section.value(QStringLiteral("last")).toObject();
    QCOMPARE(last.value(QStringLiteral("text")).toString(), QStringLiteral("plan · 34 of 35 done"));
    QCOMPARE(last.value(QStringLiteral("at")).toString(), QStringLiteral("2026-11-21T15:20:00Z"));
    const AgentGate back = AgentGate::fromJson(section);
    QCOMPARE(back.lastAt(), at);
    QCOMPARE(back, gate);
    QVERIFY(!AgentGate().toJson().contains(QStringLiteral("last")));
}

void TestAgentGate::listLinesAsTheyStand()
{
    AgentGate gate;
    gate.setLevel(Kind::Fixes, QStringLiteral("allow"));
    gate.setLevel(Kind::Moves, QStringLiteral("ask"));
    const QJsonArray lines = gate.listLines();
    QCOMPARE(lines.size(), 4);
    const QStringList kinds{QStringLiteral("reads"),
                            QStringLiteral("fixes"),
                            QStringLiteral("moves"),
                            QStringLiteral("remove")};
    const QStringList levels{QStringLiteral("on"),
                             QStringLiteral("allow"),
                             QStringLiteral("ask"),
                             QStringLiteral("off")};
    for (int i = 0; i < 4; ++i) {
        const QJsonObject line = lines.at(i).toObject();
        QCOMPARE(line.value(QStringLiteral("kind")).toString(), kinds.at(i));
        QCOMPARE(line.value(QStringLiteral("level")).toString(), levels.at(i));
    }
    QCOMPARE(lines.at(1).toObject().value(QStringLiteral("actions")).toArray().size(), 8);
    QCOMPARE(lines.at(2).toObject().value(QStringLiteral("levels")).toArray().size(), 2);
    QVERIFY(AgentGate::neverActions().contains(QStringLiteral("delete")));
}

QTEST_GUILESS_MAIN(TestAgentGate)
#include "tst_agentgate.moc"
