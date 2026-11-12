#include "core/OmarchyMenuBlock.hpp"
#include "core/OmarchyMenuFile.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::OmarchyMenuBlock;
using omaseed::core::OmarchyMenuFile;
using Servers = QList<OmarchyMenuBlock::Server>;

// docs/DECISIONS.md C36 M2–M11, board 53 §4–§5: the rows of Trigger › Torrents as omaseed
// writes them, the servers part with one, two and three servers, a name that would break the
// JSON or the shell, and the block through OmarchyMenuFile and back.
class TestOmarchyMenuBlock : public QObject
{
    Q_OBJECT

private slots:
    void templateIsTheDataFile();
    void rowsAndTheirTests();
    void serversPart_data();
    void serversPart();
    void namesAreEscaped_data();
    void namesAreEscaped();
    void checkedTicksTheCurrentServer();
    void stateTestsReadTheStatusFile();
    void roundTripThroughTheMenuFile();
};

namespace {

using Entry = std::pair<QString, QJsonObject>;

// The block's entries in order. Each line of it is one entry (or, in the template, the
// placeholder comment), so each is read as JSON on its own.
QList<Entry> entriesOf(const QString &block)
{
    QList<Entry> out;
    for (QString line : block.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("//")))
            continue;
        if (line.endsWith(QLatin1Char(',')))
            line.chop(1);
        QJsonParseError error{};
        const QJsonDocument doc =
            QJsonDocument::fromJson((QLatin1Char('{') + line + QLatin1Char('}')).toUtf8(), &error);
        if (!doc.isObject() || doc.object().size() != 1) {
            out.append({QStringLiteral("<invalid: %1: %2>").arg(error.errorString(), line), {}});
            continue;
        }
        const QString key = doc.object().keys().constFirst();
        out.append({key, doc.object().value(key).toObject()});
    }
    return out;
}

QStringList keysOf(const QList<Entry> &entries)
{
    QStringList keys;
    for (const Entry &e : entries)
        keys << e.first;
    return keys;
}

QJsonObject find(const QList<Entry> &entries, const QString &key)
{
    for (const Entry &e : entries)
        if (e.first == key)
            return e.second;
    return {};
}

const QStringList kBefore = {
    QStringLiteral("trigger.torrents"),
    QStringLiteral("trigger.torrents.open"),
    QStringLiteral("trigger.torrents.add"),
    QStringLiteral("trigger.torrents.search"),
    QStringLiteral("trigger.torrents.speeds"),
    QStringLiteral("trigger.torrents.speeds.normal"),
    QStringLiteral("trigger.torrents.speeds.turtle"),
    QStringLiteral("trigger.torrents.speeds.burst"),
    QStringLiteral("trigger.torrents.pause"),
    QStringLiteral("trigger.torrents.resume"),
};
const QString kSettings = QStringLiteral("trigger.torrents.settings");
const QString kBus = QStringLiteral(
    "busctl --user call io.github.diogochaves.omaseed /io/github/diogochaves/omaseed "
    "io.github.diogochaves.omaseed.Status ");
const QString kRunning = QStringLiteral("busctl --user status io.github.diogochaves.omaseed");

// Runs `command` in bash, the way Omarchy's menu runs a row (bash -lc, without the login).
QString bash(const QString &command, const QProcessEnvironment &env, int *exitCode = nullptr)
{
    QProcess p;
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("bash"), {QStringLiteral("-c"), command});
    p.waitForFinished();
    if (exitCode)
        *exitCode = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

void TestOmarchyMenuBlock::templateIsTheDataFile()
{
    // The built-in template is data/omarchy-menu.jsonc, marker to marker: one file for the
    // app and the hand copy.
    QFile f(QStringLiteral(OMASEED_DATA_DIR "/omarchy-menu.jsonc"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString data = QString::fromUtf8(f.readAll());
    const QString inner = OmarchyMenuBlock::templateText();
    QVERIFY(!inner.isEmpty());
    QVERIFY(data.contains(inner));
    QVERIFY(inner.contains(OmarchyMenuBlock::serversPlaceholder()));
    QVERIFY(!inner.contains(QString::fromUtf8(OmarchyMenuFile::openMarkerPrefix())));
    QVERIFY(!inner.contains(QString::fromUtf8(OmarchyMenuFile::closeMarker())));
}

void TestOmarchyMenuBlock::rowsAndTheirTests()
{
    const QString block = OmarchyMenuBlock::text({});
    QVERIFY(!block.contains(OmarchyMenuBlock::serversPlaceholder()));
    const QList<Entry> rows = entriesOf(block);
    QCOMPARE(keysOf(rows), QStringList(kBefore) << kSettings);

    // M1: the submenu and its alias.
    const QJsonObject top = find(rows, QStringLiteral("trigger.torrents"));
    QCOMPARE(top.value(QStringLiteral("label")).toString(), QStringLiteral("Torrents"));
    QVERIFY(top.value(QStringLiteral("aliases")).toArray().contains(QStringLiteral("torrents")));

    // M3, M4: the window's rows run the launch option (M5) and always show.
    const QList<std::pair<QString, QString>> window = {
        {QStringLiteral("open"), QStringLiteral("uwsm-app -- omaseed")},
        {QStringLiteral("add"), QStringLiteral("uwsm-app -- omaseed --open add")},
        {QStringLiteral("search"), QStringLiteral("uwsm-app -- omaseed --open search")},
        {QStringLiteral("settings"), QStringLiteral("uwsm-app -- omaseed --open settings")},
    };
    for (const auto &[id, action] : window) {
        const QJsonObject row = find(rows, QStringLiteral("trigger.torrents.") + id);
        QCOMPARE(row.value(QStringLiteral("action")).toString(), action);
        QVERIFY2(!row.contains(QStringLiteral("when")), qPrintable(id));
        QVERIFY(!row.value(QStringLiteral("icon")).toString().isEmpty());
    }

    // M6–M8: Speeds › while omaseed runs and the server answers; each pick ✓ on alt.pick.
    const QString speedsWhen = find(rows, QStringLiteral("trigger.torrents.speeds"))
                                   .value(QStringLiteral("when"))
                                   .toString();
    QVERIFY(speedsWhen.startsWith(kRunning + QStringLiteral(" && ")));
    QVERIFY(speedsWhen.contains(QStringLiteral(".server.reachable")));
    const QList<std::pair<QString, QString>> picks = {
        {QStringLiteral("normal"), QStringLiteral("off")},
        {QStringLiteral("turtle"), QStringLiteral("turtle")},
        {QStringLiteral("burst"), QStringLiteral("burst")},
    };
    for (const auto &[id, pick] : picks) {
        const QJsonObject row = find(rows, QStringLiteral("trigger.torrents.speeds.") + id);
        QCOMPARE(row.value(QStringLiteral("action")).toString(),
                 kBus + QStringLiteral("PickAltSpeed s ") + pick);
        QVERIFY(row.value(QStringLiteral("checked"))
                    .toString()
                    .contains(QStringLiteral(".alt.pick == \"%1\"").arg(pick)));
    }

    // M10: one of the pair, by paused.count, while the server answers.
    const QJsonObject pause = find(rows, QStringLiteral("trigger.torrents.pause"));
    const QJsonObject resume = find(rows, QStringLiteral("trigger.torrents.resume"));
    QCOMPARE(pause.value(QStringLiteral("action")).toString(),
             kBus + QStringLiteral("Invoke s pause"));
    QCOMPARE(resume.value(QStringLiteral("action")).toString(),
             kBus + QStringLiteral("Invoke s resume"));
    for (const QJsonObject &row : {pause, resume}) {
        const QString when = row.value(QStringLiteral("when")).toString();
        QVERIFY(when.startsWith(kRunning + QStringLiteral(" && ")));
        QVERIFY(when.contains(QStringLiteral(".server.reachable")));
    }
    QVERIFY(pause.value(QStringLiteral("when"))
                .toString()
                .contains(QStringLiteral(".paused.count == 0")));
    QVERIFY(resume.value(QStringLiteral("when"))
                .toString()
                .contains(QStringLiteral(".paused.count > 0")));

    // No Quit, no Add from Clipboard (M2).
    QVERIFY(!block.contains(QStringLiteral("--quit")));
}

void TestOmarchyMenuBlock::serversPart_data()
{
    QTest::addColumn<int>("count");
    QTest::newRow("one server: no Servers ›") << 1;
    QTest::newRow("two") << 2;
    QTest::newRow("three") << 3;
}

void TestOmarchyMenuBlock::serversPart()
{
    QFETCH(int, count);
    Servers servers;
    for (int i = 0; i < count; ++i)
        servers.append({QStringLiteral("id-%1").arg(i), QStringLiteral("server %1").arg(i)});
    const QList<Entry> rows = entriesOf(OmarchyMenuBlock::text(servers));

    QStringList expected = kBefore;
    if (count >= 2) {
        expected << QStringLiteral("trigger.torrents.servers");
        for (int i = 1; i <= count; ++i)
            expected << QStringLiteral("trigger.torrents.servers.%1").arg(i);
    }
    expected << kSettings;
    QCOMPARE(keysOf(rows), expected);
    if (count < 2)
        return;

    // M11: while omaseed runs (not hidden by an unreachable server), ✓ from the status file.
    const QString when = find(rows, QStringLiteral("trigger.torrents.servers"))
                             .value(QStringLiteral("when"))
                             .toString();
    QVERIFY(when.startsWith(kRunning + QStringLiteral(" && ")));
    QVERIFY(!when.contains(QStringLiteral("reachable")));
    for (int i = 0; i < count; ++i) {
        const QJsonObject row =
            find(rows, QStringLiteral("trigger.torrents.servers.%1").arg(i + 1));
        QCOMPARE(row.value(QStringLiteral("label")).toString(), QStringLiteral("server %1").arg(i));
        QCOMPARE(row.value(QStringLiteral("action")).toString(),
                 kBus + QStringLiteral("SwitchServer s 'id-%1'").arg(i));
        QVERIFY(
            row.value(QStringLiteral("checked")).toString().contains(QStringLiteral(".current")));
    }
}

void TestOmarchyMenuBlock::namesAreEscaped_data()
{
    QTest::addColumn<QString>("value");
    QTest::newRow("double quotes") << QStringLiteral("the \"big\" one");
    QTest::newRow("dollar") << QStringLiteral("$HOME $(id) ${x}");
    QTest::newRow("backtick") << QStringLiteral("`id` box");
    QTest::newRow("backslash") << QStringLiteral("back\\slash \\n \\\"");
    QTest::newRow("single quote") << QStringLiteral("Diogo's box '' '");
    QTest::newRow("everything") << QStringLiteral("a'b\"c$d`e\\f; rm -rf x && |g");
    QTest::newRow("non-ascii and newline") << QStringLiteral("caixa · ção\nnext");
}

void TestOmarchyMenuBlock::namesAreEscaped()
{
    QFETCH(QString, value);
    // The same text as a name and as an id: the label survives JSON, the id survives JSON and
    // then the shell.
    const Servers servers = {{value, value}, {QStringLiteral("other"), QStringLiteral("other")}};
    const QString block = OmarchyMenuBlock::text(servers);
    const QList<Entry> rows = entriesOf(block);
    QCOMPARE(keysOf(rows).size(), kBefore.size() + 4);
    const QJsonObject row = find(rows, QStringLiteral("trigger.torrents.servers.1"));
    QCOMPARE(row.value(QStringLiteral("label")).toString(), value);

    const QString action = row.value(QStringLiteral("action")).toString();
    const QString prefix = kBus + QStringLiteral("SwitchServer s ");
    QVERIFY(action.startsWith(prefix));
    int code = -1;
    const QString word = bash(QStringLiteral("printf %s ") + action.mid(prefix.size()),
                              QProcessEnvironment::systemEnvironment(),
                              &code);
    QCOMPARE(code, 0);
    QCOMPARE(word, value);
}

void TestOmarchyMenuBlock::checkedTicksTheCurrentServer()
{
    // The ✓ runs for real: jq on a status file, as Omarchy's menu does when it opens.
    if (QStandardPaths::findExecutable(QStringLiteral("jq")).isEmpty())
        QSKIP("jq is not installed");
    const QString odd = QStringLiteral("o'dd \"$x\" `y` \\z");
    const Servers servers = {{QStringLiteral("a"), QStringLiteral("A")},
                             {odd, QStringLiteral("B")}};
    const QList<Entry> rows = entriesOf(OmarchyMenuBlock::text(servers));

    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    QVERIFY(QDir(runtime.path()).mkpath(QStringLiteral("omaseed")));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime.path());
    const auto writeStatus = [&](const QString &current) {
        QJsonArray list;
        for (const auto &s : servers)
            list.append(QJsonObject{{QStringLiteral("id"), s.id},
                                    {QStringLiteral("current"), s.id == current}});
        QFile f(runtime.path() + QStringLiteral("/omaseed/status.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("servers"), list}}).toJson());
    };
    const auto ticked = [&](int row) {
        int code = -1;
        bash(find(rows, QStringLiteral("trigger.torrents.servers.%1").arg(row))
                 .value(QStringLiteral("checked"))
                 .toString(),
             env,
             &code);
        return code == 0;
    };

    writeStatus(QStringLiteral("a"));
    QVERIFY(ticked(1));
    QVERIFY(!ticked(2));
    writeStatus(odd);
    QVERIFY(!ticked(1));
    QVERIFY(ticked(2));
}

void TestOmarchyMenuBlock::stateTestsReadTheStatusFile()
{
    // The template's `when` (past its "omaseed runs" half) and `checked`, run with jq on status
    // files as StatusWriter writes them: server.reachable, alt.pick, paused.count.
    if (QStandardPaths::findExecutable(QStringLiteral("jq")).isEmpty())
        QSKIP("jq is not installed");
    const QList<Entry> rows = entriesOf(OmarchyMenuBlock::text({}));
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    QVERIFY(QDir(runtime.path()).mkpath(QStringLiteral("omaseed")));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime.path());
    const auto status = [&](bool reachable, const QString &pick, int paused) {
        QFile f(runtime.path() + QStringLiteral("/omaseed/status.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(
                    QJsonObject{
                        {QStringLiteral("server"),
                         QJsonObject{{QStringLiteral("reachable"), reachable}}},
                        {QStringLiteral("alt"), QJsonObject{{QStringLiteral("pick"), pick}}},
                        {QStringLiteral("paused"), QJsonObject{{QStringLiteral("count"), paused}}},
                    })
                    .toJson());
    };
    const auto holds = [&](const QString &id, const QString &test) {
        QString command =
            find(rows, QStringLiteral("trigger.torrents.") + id).value(test).toString();
        const QString running = kRunning + QStringLiteral(" && ");
        if (command.startsWith(running))
            command = command.mid(running.size());
        int code = -1;
        bash(command, env, &code);
        return code == 0;
    };
    const QString when = QStringLiteral("when");
    const QString checked = QStringLiteral("checked");

    status(true, QStringLiteral("turtle"), 0);
    QVERIFY(holds(QStringLiteral("speeds"), when));
    QVERIFY(holds(QStringLiteral("pause"), when));
    QVERIFY(!holds(QStringLiteral("resume"), when));
    QVERIFY(!holds(QStringLiteral("speeds.normal"), checked));
    QVERIFY(holds(QStringLiteral("speeds.turtle"), checked));
    QVERIFY(!holds(QStringLiteral("speeds.burst"), checked));

    status(true, QStringLiteral("off"), 3);
    QVERIFY(!holds(QStringLiteral("pause"), when));
    QVERIFY(holds(QStringLiteral("resume"), when));
    QVERIFY(holds(QStringLiteral("speeds.normal"), checked));

    status(false, QStringLiteral("burst"), 3); // the server doesn't answer: all three hide
    QVERIFY(!holds(QStringLiteral("speeds"), when));
    QVERIFY(!holds(QStringLiteral("pause"), when));
    QVERIFY(!holds(QStringLiteral("resume"), when));
    QVERIFY(holds(QStringLiteral("speeds.burst"), checked));
}

void TestOmarchyMenuBlock::roundTripThroughTheMenuFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString menu = dir.path() + QStringLiteral("/menu.jsonc");
    const QString user = dir.path() + QStringLiteral("/user.jsonc");
    QFile m(menu);
    QVERIFY(m.open(QIODevice::WriteOnly));
    m.write("{\n  \"trigger\": {\"label\":\"Trigger\"},\n}\n");
    m.close();
    const QByteArray stock = "{\n  // mine\n  \"mine\": {\"label\":\"Mine\"},\n}\n";
    QFile u(user);
    QVERIFY(u.open(QIODevice::WriteOnly));
    u.write(stock);
    u.close();
    const auto contents = [&] {
        QFile f(user);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };

    OmarchyMenuFile file(user, menu);
    const Servers three = {{QStringLiteral("1"), QStringLiteral("one")},
                           {QStringLiteral("2"), QStringLiteral("t\"wo'")},
                           {QStringLiteral("3"), QStringLiteral("three")}};
    QString error;
    QVERIFY2(file.add(OmarchyMenuBlock::text(three), &error), qPrintable(error));
    QCOMPARE(file.state(), OmarchyMenuFile::State::Block);
    QVERIFY(contents().contains(OmarchyMenuBlock::text(three).toUtf8()));

    // The server list changed: the block is written again in place (M11).
    const Servers one = {three.constFirst()};
    QVERIFY2(file.rewrite(OmarchyMenuBlock::text(one), &error), qPrintable(error));
    QVERIFY(!contents().contains("trigger.torrents.servers"));
    QVERIFY(contents().contains(OmarchyMenuBlock::text(one).toUtf8()));

    QVERIFY2(file.remove(&error), qPrintable(error));
    QCOMPARE(contents(), stock);
}

QTEST_GUILESS_MAIN(TestOmarchyMenuBlock)

#include "tst_omarchymenublock.moc"
