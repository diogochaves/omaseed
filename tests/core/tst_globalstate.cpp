// GlobalState: server_state merged key by key, the engine's own fields, and the formatted
// twins the hero binds to (spec §1: "↓ 4.21 MB/s", "1.8 TB free").

#include "QbtFeed.hpp"
#include "core/ApiError.hpp"
#include "core/GlobalState.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

class TestGlobalState : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAreDisconnected();
    void appliesTheRecordedServerState();
    void mergesOnlyThePresentKeys();
    void notifiesOnlyOnRealChanges();
    void engineFields();
    void resetClearsEverythingButTheName();
};

void TestGlobalState::defaultsAreDisconnected()
{
    GlobalState state;
    QCOMPARE(state.connected(), false);
    QCOMPARE(state.syncing(), false);
    QCOMPARE(state.connectionStatus(), QStringLiteral("disconnected"));
    QCOMPARE(state.refreshInterval(), 1500);
    QCOMPARE(state.dlSpeedText(), QStringLiteral("0"));
    QCOMPARE(state.lastSyncText(), QString());
}

void TestGlobalState::appliesTheRecordedServerState()
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/maindata/01-full.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject full = QJsonDocument::fromJson(file.readAll()).object();

    GlobalState state;
    qbtfeed::serverState(state, full.value(QStringLiteral("server_state")).toObject());

    QCOMPARE(state.connectionStatus(), QStringLiteral("firewalled"));
    QCOMPARE(state.queueing(), true);
    QCOMPARE(state.useAltSpeedLimits(), false);
    QCOMPARE(state.refreshInterval(), 1500);
    QVERIFY(state.freeSpaceOnDisk() > 0);
    QVERIFY(!state.freeSpaceText().isEmpty());
}

void TestGlobalState::mergesOnlyThePresentKeys()
{
    GlobalState state;
    qbtfeed::serverState(
        state,
        QJsonObject{{QStringLiteral("dl_info_speed"), 4414997},
                    {QStringLiteral("up_info_speed"), 831488},
                    {QStringLiteral("free_space_on_disk"), 1979120929996LL},
                    {QStringLiteral("connection_status"), QStringLiteral("connected")},
                    {QStringLiteral("dht_nodes"), 312},
                    {QStringLiteral("total_peer_connections"), 63}});

    QCOMPARE(state.dlSpeedText(), QStringLiteral("4.21 MB/s"));
    QCOMPARE(state.upSpeedText(), QStringLiteral("812 KB/s"));
    QCOMPARE(state.dlRateLimitText(), QStringLiteral("∞"));
    QCOMPARE(state.freeSpaceText(), QStringLiteral("1.80 TB"));
    QCOMPARE(state.dhtNodes(), 312);

    // A later poll that only carries the speeds must not wipe the rest.
    qbtfeed::serverState(state, QJsonObject{{QStringLiteral("dl_info_speed"), 0}});
    QCOMPARE(state.dlInfoSpeed(), 0);
    QCOMPARE(state.connectionStatus(), QStringLiteral("connected"));
    QCOMPARE(state.dhtNodes(), 312);
    QCOMPARE(state.totalPeerConnections(), 63);
}

void TestGlobalState::notifiesOnlyOnRealChanges()
{
    GlobalState state;
    QSignalSpy spy(&state, &GlobalState::dlInfoSpeedChanged);

    qbtfeed::serverState(state, QJsonObject{{QStringLiteral("dl_info_speed"), 1024}});
    QCOMPARE(spy.count(), 1);
    qbtfeed::serverState(state, QJsonObject{{QStringLiteral("dl_info_speed"), 1024}});
    QCOMPARE(spy.count(), 1);
    qbtfeed::serverState(state, QJsonObject{{QStringLiteral("dl_info_speed"), 2048}});
    QCOMPARE(spy.count(), 2);
}

void TestGlobalState::engineFields()
{
    GlobalState state;
    QSignalSpy connectedSpy(&state, &GlobalState::connectedChanged);

    state.setConnected(true);
    state.setSyncing(true);
    state.setServerName(QStringLiteral("home"));
    state.setServerVersions(QStringLiteral("v5.2.3"), QStringLiteral("2.15.1"));
    state.setRetryInSeconds(7);
    state.setLastError(ApiError(ApiError::Kind::Network, QStringLiteral("connection refused")));
    const QDateTime now = QDateTime::currentDateTime();
    state.setLastSyncTime(now);

    QCOMPARE(connectedSpy.count(), 1);
    QCOMPARE(state.serverName(), QStringLiteral("home"));
    QCOMPARE(state.serverVersion(), QStringLiteral("v5.2.3"));
    QCOMPARE(state.apiVersion(), QStringLiteral("2.15.1"));
    QCOMPARE(state.retryInSeconds(), 7);
    QVERIFY(state.lastError().contains(QStringLiteral("connection refused")));
    QCOMPARE(state.lastSyncText(), now.toString(QStringLiteral("HH:mm")));

    state.setLastError(ApiError());
    QCOMPARE(state.lastError(), QString());
}

void TestGlobalState::resetClearsEverythingButTheName()
{
    GlobalState state;
    state.setServerName(QStringLiteral("seedbox"));
    state.setConnected(true);
    qbtfeed::serverState(
        state,
        QJsonObject{{QStringLiteral("dl_info_speed"), 4414997},
                    {QStringLiteral("connection_status"), QStringLiteral("connected")}});

    state.reset();

    QCOMPARE(state.serverName(), QStringLiteral("seedbox"));
    QCOMPARE(state.connected(), false);
    QCOMPARE(state.dlInfoSpeed(), 0);
    QCOMPARE(state.connectionStatus(), QStringLiteral("disconnected"));
}

QTEST_MAIN(TestGlobalState)
#include "tst_globalstate.moc"
