// DECISIONS C50 GT7 (#297): `omaseed help agents`, each agent's deny rules for a host:port in
// its own syntax, the rules its syntax can't write well left out (and said), the quoting.

#include "core/AgentDeny.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace AgentDeny = omaseed::core::AgentDeny;

class TestAgentDeny : public QObject
{
    Q_OBJECT

private slots:
    void agents();
    void claudeLoopback();
    void claudeRemoteAndOutsideHome();
    void claudeQuotesAndGlobs();
    void codexPrefixesOnly();
    void geminiPolicies();
    void geminiPathItCantWrite();
    void piWallAlone();
    void unknownAddress();
    void json();
};

namespace {

AgentDeny::Target target(const QString &host = QStringLiteral("127.0.0.1"),
                         const QString &port = QStringLiteral("8081"))
{
    AgentDeny::Target t;
    t.host = host;
    t.port = port;
    t.serverName = QStringLiteral("attic");
    t.busName = QStringLiteral("io.github.diogochaves.omaseed");
    t.configDir = QStringLiteral("/home/ana/.config/omaseed");
    t.home = QStringLiteral("/home/ana");
    return t;
}

bool anyContains(const QStringList &list, const QString &part)
{
    return std::any_of(
        list.cbegin(), list.cend(), [&](const QString &each) { return each.contains(part); });
}

} // namespace

void TestAgentDeny::agents()
{
    QCOMPARE(AgentDeny::agents(), (QStringList{"claude", "codex", "pi", "gemini"}));
    // Anything else falls back to Claude Code's (the CLI refuses it as usage first).
    QCOMPARE(AgentDeny::rulesFor(QStringLiteral("x"), target()).agent, QStringLiteral("claude"));
}

void TestAgentDeny::claudeLoopback()
{
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("claude"), target());
    QCOMPARE(rules.where, QStringLiteral("~/.claude/settings.json"));
    QCOMPARE(rules.rules,
             (QStringList{"Bash(*127.0.0.1:8081*)",
                          "Bash(*localhost:8081*)",
                          "Bash(*busctl*io.github.diogochaves.omaseed*)",
                          "Bash(*gdbus*io.github.diogochaves.omaseed*)",
                          "Bash(*dbus-send*io.github.diogochaves.omaseed*)",
                          "Edit(~/.config/omaseed/*.json)",
                          "Edit(~/.config/omaseed/moves/**)",
                          "Edit(~/.config/omaseed/secrets/**)"}));
    // No rule takes skins/ or layouts/ (GT7), none is a WebFetch or a Write (never matched).
    QVERIFY(!anyContains(rules.rules, QStringLiteral("skins")));
    QVERIFY(!anyContains(rules.rules, QStringLiteral("WebFetch")));
    QVERIFY(!anyContains(rules.rules, QStringLiteral("Write(")));
    QVERIFY(anyContains(rules.omitted, QStringLiteral("No WebFetch rule")));
    QVERIFY(rules.text.startsWith(QStringLiteral("A wall, not a gate\n")));
    QVERIFY(rules.text.contains(QStringLiteral("call attic's API")));
    QVERIFY(rules.text.contains(QStringLiteral("    \"Bash(*localhost:8081*)\",\n")));
    QVERIFY(
        rules.text.contains(QStringLiteral("    \"Edit(~/.config/omaseed/secrets/**)\"\n  ] }")));
    QVERIFY(rules.text.contains(
        QStringLiteral("Codex · pi · Gemini CLI: omaseed help agents --for codex|pi|gemini\n")));
    QVERIFY(rules.text.endsWith(QStringLiteral(
        "These match command text. A sandbox with a network allow list is the wall.\n")));
    for (const QString &line : rules.text.split(QLatin1Char('\n')))
        QVERIFY2(line.size() <= 80 || line.startsWith(QLatin1Char(' ')), qPrintable(line));
}

void TestAgentDeny::claudeRemoteAndOutsideHome()
{
    AgentDeny::Target t = target(QStringLiteral("nas.lan"), QStringLiteral("8080"));
    t.configDir = QStringLiteral("/srv/conf/omaseed");
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("claude"), t);
    QCOMPARE(rules.rules.first(), QStringLiteral("Bash(*nas.lan:8080*)"));
    QVERIFY(!anyContains(rules.rules, QStringLiteral("localhost")));
    // `//` is the filesystem's root in Claude Code's path patterns; a lone `/` is the settings'.
    QVERIFY(rules.rules.contains(QStringLiteral("Edit(//srv/conf/omaseed/*.json)")));
}

void TestAgentDeny::claudeQuotesAndGlobs()
{
    AgentDeny::Target t = target();
    t.configDir = QStringLiteral("/home/ana/odd \"dir\"/omaseed");
    AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("claude"), t);
    QVERIFY(rules.rules.contains(QStringLiteral("Edit(~/odd \"dir\"/omaseed/*.json)")));
    // settings.json is JSON: the quote is escaped where it is printed.
    QVERIFY(rules.text.contains(QStringLiteral("\"Edit(~/odd \\\"dir\\\"/omaseed/*.json)\"")));

    t.configDir = QStringLiteral("/home/ana/conf[1]/omaseed");
    rules = AgentDeny::rulesFor(QStringLiteral("claude"), t);
    QVERIFY(!anyContains(rules.rules, QStringLiteral("Edit(")));
    QVERIFY(anyContains(rules.omitted, QStringLiteral("No Edit rule")));
    QVERIFY(rules.text.contains(QStringLiteral("No Edit rule")));
}

void TestAgentDeny::codexPrefixesOnly()
{
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("codex"), target());
    QCOMPARE(rules.where, QStringLiteral("~/.codex/rules/omaseed.rules"));
    QCOMPARE(rules.rules.size(), 3);
    for (const QString &rule : rules.rules) {
        QVERIFY(rule.startsWith(QStringLiteral("prefix_rule(\n    pattern = [")));
        QVERIFY(rule.contains(QStringLiteral("    decision = \"forbidden\",\n")));
        QVERIFY(rule.endsWith(QStringLiteral(")")));
    }
    QVERIFY(rules.rules.at(0).contains(QStringLiteral("[\"busctl\", \"--user\", [\"call\"")));
    QVERIFY(rules.rules.at(2).contains(QStringLiteral("\"--dest=io.github.diogochaves.omaseed\"")));
    // A prefix of whole words can't name an address inside a URL, nor a file edit.
    QVERIFY(!anyContains(rules.rules, QStringLiteral("8081")));
    QCOMPARE(rules.omitted.size(), 2);
    QVERIFY(rules.omitted.at(0).startsWith(QStringLiteral("No rule for attic's address")));
    QVERIFY(rules.omitted.at(1).startsWith(QStringLiteral("No rule for omaseed's files")));
}

void TestAgentDeny::geminiPolicies()
{
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("gemini"), target());
    QCOMPARE(rules.where, QStringLiteral("~/.gemini/policies/omaseed.toml"));
    QCOMPARE(rules.rules.size(), 3);
    QVERIFY(rules.rules.at(0).contains(
        QStringLiteral("commandRegex = '.*(127\\.0\\.0\\.1|localhost):8081(?![0-9])'")));
    QVERIFY(rules.rules.at(1).contains(QStringLiteral(
        "commandRegex = '.*(busctl|gdbus|dbus-send).*io\\.github\\.diogochaves\\.omaseed'")));
    QVERIFY(rules.rules.at(2).contains(QStringLiteral("toolName = [\"write_file\", \"replace\"]")));
    QVERIFY(rules.rules.at(2).contains(QStringLiteral(
        "argsPattern = '\"file_path\":\"[^\"]*\\.config/omaseed/(?!skins/|layouts/)'")));
    for (const QString &rule : rules.rules) {
        QVERIFY(rule.startsWith(QStringLiteral("[[rule]]\n")));
        QVERIFY(rule.contains(QStringLiteral("decision = \"deny\"\n")));
    }
    QVERIFY(rules.omitted.isEmpty());
}

void TestAgentDeny::geminiPathItCantWrite()
{
    AgentDeny::Target t = target(QStringLiteral("nas.lan"), QStringLiteral("9091"));
    t.configDir = QStringLiteral("/home/ana/ana's/omaseed");
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("gemini"), t);
    QCOMPARE(rules.rules.size(), 2);
    QVERIFY(rules.rules.at(0).contains(QStringLiteral("'.*nas\\.lan:9091(?![0-9])'")));
    QVERIFY(anyContains(rules.omitted, QStringLiteral("No rule for omaseed's files")));
}

void TestAgentDeny::piWallAlone()
{
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("pi"), target());
    QVERIFY(rules.rules.isEmpty());
    QVERIFY(rules.where.isEmpty());
    QVERIFY(rules.text.contains(QStringLiteral("pi has no deny rules of its own")));
    QVERIFY(rules.text.contains(QStringLiteral("leaves out 127.0.0.1:8081")));
    QVERIFY(!rules.text.contains(QStringLiteral("These match command text")));
    QVERIFY(
        rules.text.endsWith(QStringLiteral("A sandbox with a network allow list is the wall.\n")));
}

void TestAgentDeny::unknownAddress()
{
    AgentDeny::Target t = target(QString(), QString());
    t.serverName.clear();
    t.unknownWhy = QStringLiteral("not-running");
    AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("claude"), t);
    QCOMPARE(rules.rules.first(), QStringLiteral("Bash(*<host>:<port>*)"));
    QVERIFY(rules.text.contains(QStringLiteral("call your torrent server's API")));
    QVERIFY(
        rules.text.contains(QStringLiteral("omaseed isn't running, so it can't say its server")));

    t.unknownWhy = QStringLiteral("not-allowed");
    rules = AgentDeny::rulesFor(QStringLiteral("gemini"), t);
    QVERIFY(rules.rules.first().contains(QStringLiteral("'.*<host>:<port>(?![0-9])'")));
    QVERIFY(rules.text.contains(QStringLiteral("omaseed didn't say its server's address")));

    // Codex names no address, so no placeholder line.
    rules = AgentDeny::rulesFor(QStringLiteral("codex"), t);
    QVERIFY(!rules.text.contains(QStringLiteral("<port>")));
}

void TestAgentDeny::json()
{
    const AgentDeny::Target t = target();
    const AgentDeny::Rules rules = AgentDeny::rulesFor(QStringLiteral("codex"), t);
    const QByteArray bytes =
        AgentDeny::json(rules, t, QDateTime(QDate(2026, 11, 21), QTime(9, 0), QTimeZone::UTC));
    QVERIFY(bytes.startsWith(R"({"v":1,"command":"help agents","at":"2026-11-21T09:00:00Z",)"
                             R"("agent":"codex","server":"127.0.0.1:8081",)"));
    const QJsonObject object = QJsonDocument::fromJson(bytes).object();
    QCOMPARE(object.value(QStringLiteral("where")).toString(), rules.where);
    QCOMPARE(object.value(QStringLiteral("rules")).toArray().size(), 3);
    QCOMPARE(object.value(QStringLiteral("omitted")).toArray().size(), 2);
    QCOMPARE(object.value(QStringLiteral("text")).toString(), rules.text);

    AgentDeny::Target none = target(QString(), QString());
    const QJsonObject unknown =
        QJsonDocument::fromJson(
            AgentDeny::json(AgentDeny::rulesFor(QStringLiteral("pi"), none), none, {}))
            .object();
    QVERIFY(unknown.value(QStringLiteral("server")).isNull());
    QVERIFY(unknown.value(QStringLiteral("where")).isNull());
}

QTEST_GUILESS_MAIN(TestAgentDeny)
#include "tst_agentdeny.moc"
