// AltPresets (DECISIONS C32 D8 a) against the :8081 playground: burst written over the one
// alternative set and turtle written back after, by a pick, by a start that finds a burst a
// crash left running, and at quit. The playground's set, mode and schedule are put back.

#include "core/AltPresets.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/ServerPreferences.hpp"

#include "LiveServer.hpp"

#include <QEventLoop>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

using namespace omaseed::core;

namespace {

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

template<typename T>
QbtResult<T> wait(QbtReply<T> *reply)
{
    QbtResult<T> out;
    QEventLoop loop;
    reply->then(&loop, [&out, &loop](const QbtResult<T> &result) {
        out = result;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return out;
}

constexpr qint64 kTurtleDl = 300 * 1024;
constexpr qint64 kTurtleUp = 50 * 1024;

} // namespace

class TestAltPresetsLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void burstIsWrittenOverTheSetAndTurtleBack();
    void aStartEndsABurstACrashLeft();
    void aStartLeavesABurstUntilItsTime();
    void quitWritesTurtleBack();

private:
    /// The playground's alternative set and mode, as read now.
    QJsonObject serverSet();
    bool serverMode();
    void makePresets(AltPresets &presets, const QJsonObject &store = {});

    QbtClient m_client;
    ServerPreferences *m_prefs = nullptr;
    GlobalState m_state;
    QJsonObject m_before;
    bool m_modeBefore = false;
};

QJsonObject TestAltPresetsLive::serverSet()
{
    const auto prefs = wait(m_client.appPreferences());
    if (prefs.isError())
        return {};
    return {{QStringLiteral("dl"), prefs.value().value(QStringLiteral("alt_dl_limit"))},
            {QStringLiteral("up"), prefs.value().value(QStringLiteral("alt_up_limit"))}};
}

bool TestAltPresetsLive::serverMode()
{
    const auto mode = wait(m_client.transferSpeedLimitsMode());
    return mode.isValid() && mode.value();
}

void TestAltPresetsLive::makePresets(AltPresets &presets, const QJsonObject &store)
{
    presets.setClient(&m_client);
    presets.setPreferences(m_prefs);
    presets.setGlobalState(&m_state);
    presets.setStore(store);
    presets.setServer(QStringLiteral("live"));
}

void TestAltPresetsLive::initTestCase()
{
    const QUrl url = LiveServer::playground();
    if (!answers(url))
        QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");
    m_client.setBaseUrl(url);
    m_prefs = new ServerPreferences(&m_client, this);

    const auto prefs = wait(m_client.appPreferences());
    QVERIFY(prefs.isValid());
    m_before = {
        {QStringLiteral("alt_dl_limit"), prefs.value().value(QStringLiteral("alt_dl_limit"))},
        {QStringLiteral("alt_up_limit"), prefs.value().value(QStringLiteral("alt_up_limit"))},
        {QStringLiteral("scheduler_enabled"),
         prefs.value().value(QStringLiteral("scheduler_enabled"))}};
    m_modeBefore = serverMode();

    // A known turtle, the schedule off (so an ended burst turns the limits off), mode off.
    QVERIFY(wait(m_client.appSetPreferences({{QStringLiteral("alt_dl_limit"), kTurtleDl},
                                             {QStringLiteral("alt_up_limit"), kTurtleUp},
                                             {QStringLiteral("scheduler_enabled"), false}}))
                .isValid());
    QVERIFY(wait(m_client.transferSetSpeedLimitsMode(false)).isValid());
    m_state.setConnected(true);
    m_prefs->reload();
    QTRY_VERIFY_WITH_TIMEOUT(m_prefs->isLoaded(), 10000);
}

void TestAltPresetsLive::cleanupTestCase()
{
    if (m_before.isEmpty())
        return;
    wait(m_client.appSetPreferences(m_before));
    wait(m_client.transferSetSpeedLimitsMode(m_modeBefore));
}

void TestAltPresetsLive::burstIsWrittenOverTheSetAndTurtleBack()
{
    AltPresets presets;
    makePresets(presets);
    QSignalSpy stored(&presets, &AltPresets::storeChanged);
    QCOMPARE(presets.turtleDl(), kTurtleDl);
    QCOMPARE(presets.burstUp(), AltPresets::kDefaultBurstUp);

    QVERIFY(presets.pick(QStringLiteral("burst")));
    // The turtle is in the store before anything lands on the server.
    QVERIFY(stored.count() >= 1);
    const QJsonObject running = presets.store()
                                    .value(QStringLiteral("live"))
                                    .toObject()
                                    .value(QStringLiteral("running"))
                                    .toObject();
    QCOMPARE(running.value(QStringLiteral("turtleDl")).toInteger(), kTurtleDl);
    QCOMPARE(running.value(QStringLiteral("turtleUp")).toInteger(), kTurtleUp);
    QVERIFY(presets.burstOn());
    QVERIFY(presets.burstUntil() > QDateTime::currentDateTime().addSecs(59 * 60));

    QTRY_COMPARE_WITH_TIMEOUT(
        serverSet().value(QStringLiteral("up")).toInteger(), AltPresets::kDefaultBurstUp, 10000);
    QCOMPARE(serverSet().value(QStringLiteral("dl")).toInteger(), 0);
    QVERIFY(serverMode());
    // Settings' turtle rows read the saved copy meanwhile.
    QTRY_VERIFY_WITH_TIMEOUT(m_prefs->pendingKeys().isEmpty(), 10000);
    QCOMPARE(presets.values().value(QStringLiteral("alt_dl_limit")).toLongLong(), kTurtleDl);

    QVERIFY(presets.pick(QStringLiteral("off")));
    QTRY_COMPARE_WITH_TIMEOUT(
        serverSet().value(QStringLiteral("dl")).toInteger(), kTurtleDl, 10000);
    QCOMPARE(serverSet().value(QStringLiteral("up")).toInteger(), kTurtleUp);
    QVERIFY(!serverMode());
    QTRY_VERIFY_WITH_TIMEOUT(!presets.burstOn(), 10000);
    QCOMPARE(presets.lastPick(), QStringLiteral("burst"));

    // Turtle from a burst keeps the limits on, as turtle.
    QVERIFY(presets.pick(QStringLiteral("burst")));
    QTRY_COMPARE_WITH_TIMEOUT(serverSet().value(QStringLiteral("dl")).toInteger(), 0, 10000);
    QVERIFY(presets.pick(QStringLiteral("turtle")));
    QTRY_COMPARE_WITH_TIMEOUT(
        serverSet().value(QStringLiteral("dl")).toInteger(), kTurtleDl, 10000);
    QVERIFY(serverMode());
    QTRY_VERIFY_WITH_TIMEOUT(!presets.burstOn(), 10000);
    QVERIFY(wait(m_client.transferSetSpeedLimitsMode(false)).isValid());
}

void TestAltPresetsLive::aStartEndsABurstACrashLeft()
{
    QJsonObject left;
    {
        AltPresets crashed;
        makePresets(crashed);
        QVERIFY(crashed.pick(QStringLiteral("burst")));
        QTRY_COMPARE_WITH_TIMEOUT(serverSet().value(QStringLiteral("dl")).toInteger(), 0, 10000);
        left = crashed.store();
        // Gone without a word: the destructor sends nothing.
    }
    // Its hour has passed by the next start.
    QJsonObject entry = left.value(QStringLiteral("live")).toObject();
    QJsonObject running = entry.value(QStringLiteral("running")).toObject();
    running.insert(QStringLiteral("until"),
                   QDateTime::currentDateTime().addSecs(-60).toString(Qt::ISODate));
    entry.insert(QStringLiteral("running"), running);
    left.insert(QStringLiteral("live"), entry);

    m_prefs->reload();
    QTRY_VERIFY_WITH_TIMEOUT(m_prefs->pendingKeys().isEmpty() && !m_prefs->isLoading(), 10000);
    AltPresets next;
    makePresets(next, left);
    QTRY_COMPARE_WITH_TIMEOUT(
        serverSet().value(QStringLiteral("dl")).toInteger(), kTurtleDl, 10000);
    QCOMPARE(serverSet().value(QStringLiteral("up")).toInteger(), kTurtleUp);
    QTRY_VERIFY_WITH_TIMEOUT(!next.burstOn(), 10000);
    QVERIFY(!serverMode());
}

void TestAltPresetsLive::aStartLeavesABurstUntilItsTime()
{
    QJsonObject left;
    {
        AltPresets crashed;
        makePresets(crashed);
        QVERIFY(crashed.pick(QStringLiteral("burst")));
        QTRY_COMPARE_WITH_TIMEOUT(serverSet().value(QStringLiteral("dl")).toInteger(), 0, 10000);
        left = crashed.store();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!m_prefs->isLoading(), 10000);
    m_prefs->reload();
    QTRY_VERIFY_WITH_TIMEOUT(!m_prefs->isLoading(), 10000);
    AltPresets next;
    makePresets(next, left);
    QVERIFY(next.burstOn());
    QTest::qWait(500);
    QCOMPARE(serverSet().value(QStringLiteral("dl")).toInteger(), 0);
    QVERIFY(next.burstOn());

    // A set someone else wrote meanwhile is theirs: forgotten, nothing written.
    QVERIFY(
        wait(m_client.appSetPreferences({{QStringLiteral("alt_dl_limit"), 123 * 1024}})).isValid());
    m_prefs->reload();
    QTRY_VERIFY_WITH_TIMEOUT(!m_prefs->isLoading(), 10000);
    AltPresets third;
    makePresets(third, next.store());
    QTRY_VERIFY_WITH_TIMEOUT(!third.burstOn(), 10000);
    QCOMPARE(serverSet().value(QStringLiteral("dl")).toInteger(), 123 * 1024);

    QVERIFY(wait(m_client.appSetPreferences({{QStringLiteral("alt_dl_limit"), kTurtleDl},
                                             {QStringLiteral("alt_up_limit"), kTurtleUp}}))
                .isValid());
    QVERIFY(wait(m_client.transferSetSpeedLimitsMode(false)).isValid());
    m_prefs->reload();
    QTRY_VERIFY_WITH_TIMEOUT(!m_prefs->isLoading(), 10000);
}

void TestAltPresetsLive::quitWritesTurtleBack()
{
    AltPresets presets;
    makePresets(presets);
    QVERIFY(presets.pick(QStringLiteral("burst")));
    QTRY_COMPARE_WITH_TIMEOUT(serverSet().value(QStringLiteral("dl")).toInteger(), 0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_prefs->isLoading(), 10000);

    presets.endBurstAndWait(5000);
    // Back when it returns: the quit can go on.
    QCOMPARE(serverSet().value(QStringLiteral("dl")).toInteger(), kTurtleDl);
    QCOMPARE(serverSet().value(QStringLiteral("up")).toInteger(), kTurtleUp);
    QVERIFY(!serverMode());
    QVERIFY(!presets.burstOn());
}

QTEST_MAIN(TestAltPresetsLive)
#include "tst_altpresets_live.moc"
