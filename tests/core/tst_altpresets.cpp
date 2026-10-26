// AltPresets (DECISIONS C32 D8 a) without a server: the presets per server, Settings' keys
// (the turtle at rest is the server's set, a saved copy while a burst runs), the burst's
// length, and a start that finds a burst whose set someone else changed. The writes are
// tst_altpresets_live's, against the playground.

#include "core/AltPresets.hpp"
#include "core/GlobalState.hpp"
#include "core/ServerPreferences.hpp"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

class TestAltPresets : public QObject
{
    Q_OBJECT

private slots:
    void turtleIsTheServersSetAtRest();
    void burstKeysStayHereByServer();
    void aRunningBurstKeepsTheTurtleAside();
    void aSetSomeoneElseWroteIsForgotten();
};

namespace {

QVariantMap serverValues(qint64 altDl, qint64 altUp)
{
    return {{QStringLiteral("alt_dl_limit"), altDl},
            {QStringLiteral("alt_up_limit"), altUp},
            {QStringLiteral("dl_limit"), 0},
            {QStringLiteral("up_limit"), 4 * 1024 * 1024}};
}

QJsonObject runningStore(qint64 dl, qint64 up)
{
    return {{QStringLiteral("public"),
             QJsonObject{{QStringLiteral("running"),
                          QJsonObject{{QStringLiteral("turtleDl"), 300 * 1024},
                                      {QStringLiteral("turtleUp"), 50 * 1024},
                                      {QStringLiteral("dl"), dl},
                                      {QStringLiteral("up"), up},
                                      {QStringLiteral("until"), QString()}}}}}};
}

} // namespace

void TestAltPresets::turtleIsTheServersSetAtRest()
{
    ServerPreferences prefs(nullptr);
    GlobalState state;
    AltPresets presets;
    presets.setPreferences(&prefs);
    presets.setGlobalState(&state);
    QVERIFY(!presets.isLoaded());
    QVERIFY(!presets.has(QStringLiteral("burst_up_limit")));
    QVERIFY(!presets.pick(QStringLiteral("burst"))); // no server, no client

    presets.setServer(QStringLiteral("public"));
    prefs.setOffline(serverValues(-1, 50 * 1024));
    QVERIFY(presets.isLoaded());
    QCOMPARE(presets.current(), QStringLiteral("off"));
    QCOMPARE(presets.turtleDl(), 0); // -1, no limit
    QCOMPARE(presets.turtleUp(), 50 * 1024);
    QCOMPARE(presets.values().value(QStringLiteral("alt_up_limit")).toLongLong(), 50 * 1024);
    // Burst's defaults: ↓ no limit, ↑ 2 MB/s, for an hour; A turns turtle on first.
    QCOMPARE(presets.burstDl(), 0);
    QCOMPARE(presets.burstUp(), AltPresets::kDefaultBurstUp);
    QCOMPARE(presets.burstMinutes(), 60);
    QCOMPARE(presets.lastPick(), QStringLiteral("turtle"));
    QVERIFY(!presets.burstOn());
    QVERIFY(presets.owns({{QStringLiteral("alt_dl_limit"), 1}}));
    QVERIFY(!presets.owns({{QStringLiteral("dl_limit"), 1}}));

    // At rest the turtle's keys go to the server's preferences.
    QSignalSpy posted(&prefs, &ServerPreferences::posted);
    presets.apply({{QStringLiteral("alt_dl_limit"), 200 * 1024}});
    QCOMPARE(posted.count(), 1);
    QCOMPARE(presets.turtleDl(), 200 * 1024);
}

void TestAltPresets::burstKeysStayHereByServer()
{
    ServerPreferences prefs(nullptr);
    prefs.setOffline(serverValues(300 * 1024, 50 * 1024));
    GlobalState state;
    AltPresets presets;
    presets.setPreferences(&prefs);
    presets.setGlobalState(&state);
    presets.setServer(QStringLiteral("public"));
    QSignalSpy stored(&presets, &AltPresets::storeChanged);
    QSignalSpy posted(&prefs, &ServerPreferences::posted);

    presets.apply(
        {{QStringLiteral("burst_up_limit"), 1024 * 1024}, {QStringLiteral("burst_minutes"), 30}});
    QCOMPARE(posted.count(), 0);
    QCOMPARE(stored.count(), 1);
    QCOMPARE(presets.burstUp(), 1024 * 1024);
    QCOMPARE(presets.burstMinutes(), 30);
    // Lengths are the three the choice offers; anything else is the hour.
    presets.setBurstMinutes(45);
    QCOMPARE(presets.burstMinutes(), 60);
    presets.setBurstMinutes(0);
    QCOMPARE(presets.burstMinutes(), 0);

    // Another server has its own.
    presets.setServer(QStringLiteral("private"));
    QCOMPARE(presets.burstUp(), AltPresets::kDefaultBurstUp);
    presets.setServer(QStringLiteral("public"));
    QCOMPARE(presets.burstUp(), 1024 * 1024);

    // And it comes back from state.json.
    AltPresets again;
    again.setStore(presets.store());
    again.setServer(QStringLiteral("public"));
    QCOMPARE(again.burstUp(), 1024 * 1024);
    QCOMPARE(again.burstMinutes(), 0);
}

void TestAltPresets::aRunningBurstKeepsTheTurtleAside()
{
    ServerPreferences prefs(nullptr);
    // The set holds burst's limits.
    prefs.setOffline(serverValues(0, 2 * 1024 * 1024));
    GlobalState state;
    AltPresets presets;
    presets.setPreferences(&prefs);
    presets.setGlobalState(&state);
    presets.setStore(runningStore(0, 2 * 1024 * 1024));
    presets.setServer(QStringLiteral("public"));
    QVERIFY(presets.burstOn());
    QVERIFY(!presets.burstUntil().isValid());           // until it is stopped
    QCOMPARE(presets.current(), QStringLiteral("off")); // the server isn't on them (yet)
    QCOMPARE(presets.turtleDl(), 300 * 1024);
    QCOMPARE(presets.values().value(QStringLiteral("alt_dl_limit")).toLongLong(), 300 * 1024);

    // Settings' turtle rows write the saved copy, not the server.
    QSignalSpy posted(&prefs, &ServerPreferences::posted);
    presets.apply({{QStringLiteral("alt_up_limit"), 64 * 1024}});
    QCOMPARE(posted.count(), 0);
    QCOMPARE(presets.turtleUp(), 64 * 1024);
}

void TestAltPresets::aSetSomeoneElseWroteIsForgotten()
{
    ServerPreferences prefs(nullptr);
    prefs.setOffline(serverValues(123 * 1024, 2 * 1024 * 1024));
    GlobalState state;
    state.setConnected(true);
    AltPresets presets;
    presets.setPreferences(&prefs);
    presets.setGlobalState(&state);
    QSignalSpy stored(&presets, &AltPresets::storeChanged);
    presets.setStore(runningStore(0, 2 * 1024 * 1024));
    presets.setServer(QStringLiteral("public"));
    // The set isn't burst's any more: nothing to write back, the record goes.
    QVERIFY(!presets.burstOn());
    QCOMPARE(stored.count(), 1);
    QCOMPARE(presets.turtleDl(), 123 * 1024);
}

QTEST_MAIN(TestAltPresets)
#include "tst_altpresets.moc"
