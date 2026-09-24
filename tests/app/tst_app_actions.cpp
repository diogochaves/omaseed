// T4.3 — App's torrent actions (src/ui/app/App.hpp, the "// --- T4.3: torrent actions ---"
// block) against the :8081 playground. QSKIPs the whole file when the playground does not
// answer, the same guard tst_qbtclient_actions_live.cpp uses. `App` is wired to the
// playground the way tst_app.cpp's environmentOverrideReachesThePlayground() does
// (OMASEED_SERVER_URL, a throwaway XDG_CONFIG_HOME so no server is persisted).
//
// fixture-one/fixture-two/fixture-three (scripts/dev/test-server.sh seed) are stopped magnets
// this file borrows and puts back: every category/tag/location/limit/name change made to them
// is undone in cleanupTestCase(), and `scripts/dev/test-server.sh reset` repairs anything a
// failed run left behind. The delete test only ever deletes a torrent this file added itself.

#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include "LiveServer.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <functional>

using omaseed::app::App;
using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::TorrentsInfoQuery;

namespace {

const QString kFixtureOne = QStringLiteral("4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d");
const QString kFixtureTwo = QStringLiteral("b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0");

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace

class TestAppActions : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void stopStartTogglesTheFixture();
    void categorySetAndUnset();
    void tagsAddAndRemove();
    void locationRoundTrips();
    void limitsRoundTripThroughTorrentsInfo();
    void renameAndRenameBack();
    void deleteRemovesOnlyWhatThisTestAdded();
    void deleteFilesDefaultPersists();
    void perTorrentOptionsRoundTrip();

private:
    QJsonObject torrentInfo(const QString &hash);
    QJsonObject pollUntil(const QString &hash,
                          const std::function<bool(const QJsonObject &)> &predicate,
                          int timeoutMs = 8000);

    QTemporaryDir m_config;
    std::unique_ptr<App> m_app;
    QUrl m_playground;
    QString m_addedHash;
};

QJsonObject TestAppActions::torrentInfo(const QString &hash)
{
    TorrentsInfoQuery query;
    query.hashes = {hash};
    const QbtResult<QJsonArray> result = await(this, m_app->client()->torrentsInfo(query));
    if (result.isError() || result.value().isEmpty())
        return {};
    return result.value().at(0).toObject();
}

QJsonObject TestAppActions::pollUntil(const QString &hash,
                                      const std::function<bool(const QJsonObject &)> &predicate,
                                      int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    QJsonObject last;
    while (clock.elapsed() < timeoutMs) {
        last = torrentInfo(hash);
        if (predicate(last))
            return last;
        QEventLoop loop;
        QTimer::singleShot(150, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return last;
}

void TestAppActions::initTestCase()
{
    m_playground = LiveServer::playground();
    if (!answers(m_playground))
        QSKIP("the :8081 playground does not answer: scripts/dev/test-server.sh start && "
              "scripts/dev/test-server.sh seed");

    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    qputenv("OMASEED_SERVER_URL", m_playground.toString().toUtf8());
    qunsetenv("OMASEED_API_KEY");
    qunsetenv("OMASEED_USERNAME");
    qunsetenv("OMASEED_PASSWORD");

    m_app = std::make_unique<App>();
    QSignalSpy firstSync(m_app->engine(), &omaseed::core::SyncEngine::firstSyncFinished);
    m_app->start();
    QVERIFY(m_app->hasServer());
    QVERIFY2(waitFor([&firstSync]() { return firstSync.count() > 0; }, 10000),
             "no first sync from the playground within 10 s");

    if (m_app->torrents()->indexOfHash(kFixtureOne) < 0 ||
        m_app->torrents()->indexOfHash(kFixtureTwo) < 0)
        QSKIP("fixture-one/fixture-two are missing: scripts/dev/test-server.sh seed");
}

void TestAppActions::cleanupTestCase()
{
    if (!m_app)
        return;

    // docs/12 G4: every option back off on fixture-two (the toggles only flip, so each is
    // sent only when the server still has it on).
    const QJsonObject options = torrentInfo(kFixtureTwo);
    if (options.value(QStringLiteral("seq_dl")).toBool())
        await(this, m_app->client()->torrentsToggleSequentialDownload({kFixtureTwo}));
    if (options.value(QStringLiteral("f_l_piece_prio")).toBool())
        await(this, m_app->client()->torrentsToggleFirstLastPiecePrio({kFixtureTwo}));
    await(this, m_app->client()->torrentsSetSuperSeeding({kFixtureTwo}, false));

    // Undo whatever this file changed on the two fixtures it borrowed, and remove the one
    // torrent it added itself.
    await(this, m_app->client()->torrentsSetCategory({kFixtureOne}, QStringLiteral("fixtures")));
    await(this, m_app->client()->torrentsRemoveTags({kFixtureOne}, {QStringLiteral("t43")}));
    // Taking a tag off a torrent leaves the tag itself defined on the server.
    await(this, m_app->client()->torrentsDeleteTags({QStringLiteral("t43")}));
    await(this,
          m_app->client()->torrentsSetLocation(
              {kFixtureOne},
              QStringLiteral("/home/chaves/.local/share/omaseed-test/downloads/fixtures")));
    await(this, m_app->client()->torrentsSetShareLimits({kFixtureTwo}, -2, -2, -2));
    await(this, m_app->client()->torrentsStop({kFixtureOne, kFixtureTwo}));

    if (!m_addedHash.isEmpty())
        await(this, m_app->client()->torrentsDelete({m_addedHash}, true));

    m_app->engine()->stop();
}

void TestAppActions::stopStartTogglesTheFixture()
{
    // fixture-one starts stopped (test-server.sh seed): `stopStart` on it starts it, a second
    // call — now that it is running — stops it again.
    QJsonObject info = pollUntil(kFixtureOne, [](const QJsonObject &) { return true; });
    QVERIFY2(!info.isEmpty(), "fixture-one is missing");
    QVERIFY2(info.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("stopped")),
             "fixture-one was not stopped at the start of this test");

    m_app->stopStart({kFixtureOne});
    info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return !t.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("stopped"));
    });
    QVERIFY2(!info.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("stopped")),
             "stopStart did not start the stopped fixture");

    // `stopStart()` decides from App's own (synced) model, not from this test's own direct
    // poll above, so the second call must wait for App's next sync to have caught up too —
    // otherwise it would still see the fixture as stopped and start it again instead.
    QVERIFY2(waitFor(
                 [this]() {
                     return m_app->torrents()->indexOfHash(kFixtureOne) >= 0 &&
                            m_app->torrents()->torrent(kFixtureOne).stateText() !=
                                QStringLiteral("stopped");
                 },
                 8000),
             "App's own model never caught up with the fixture starting");

    m_app->stopStart({kFixtureOne});
    info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return t.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("stopped"));
    });
    QVERIFY2(info.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("stopped")),
             "stopStart did not stop the now-running fixture");
}

void TestAppActions::categorySetAndUnset()
{
    m_app->setCategory({kFixtureOne}, QString());
    QJsonObject info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return t.value(QStringLiteral("category")).toString().isEmpty();
    });
    QCOMPARE(info.value(QStringLiteral("category")).toString(), QString());

    m_app->setCategory({kFixtureOne}, QStringLiteral("fixtures"));
    info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return t.value(QStringLiteral("category")).toString() == QStringLiteral("fixtures");
    });
    QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral("fixtures"));
}

void TestAppActions::tagsAddAndRemove()
{
    m_app->addTags({kFixtureOne}, {QStringLiteral("t43")});
    QJsonObject info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return t.value(QStringLiteral("tags")).toString().contains(QStringLiteral("t43"));
    });
    QVERIFY2(info.value(QStringLiteral("tags")).toString().contains(QStringLiteral("t43")),
             "addTags did not reach the fixture");

    m_app->removeTags({kFixtureOne}, {QStringLiteral("t43")});
    info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return !t.value(QStringLiteral("tags")).toString().contains(QStringLiteral("t43"));
    });
    QVERIFY2(!info.value(QStringLiteral("tags")).toString().contains(QStringLiteral("t43")),
             "removeTags did not reach the fixture");
}

void TestAppActions::locationRoundTrips()
{
    const QString path = QStringLiteral("/home/chaves/.local/share/omaseed-test/downloads");
    m_app->setLocation({kFixtureOne}, path);
    const QJsonObject info = pollUntil(kFixtureOne, [&path](const QJsonObject &t) {
        return t.value(QStringLiteral("save_path")).toString() == path;
    });
    QCOMPARE(info.value(QStringLiteral("save_path")).toString(), path);
}

void TestAppActions::limitsRoundTripThroughTorrentsInfo()
{
    // dl/ul in KB/s-ish bytes/s, ratio 1.5, seeding time 100 minutes (the API's own unit —
    // App::setLimits() passes it straight through, see the class comment there).
    m_app->setLimits({kFixtureTwo}, 20480, 10240, 1.5, 100);
    // The speed limits and the share limits travel as two separate API calls, so the first
    // snapshot that carries one of them need not carry the other yet: wait for all four
    // (T4.8 — polling on `dl_limit` alone failed on a freshly reset playground).
    const QJsonObject info = pollUntil(kFixtureTwo, [](const QJsonObject &t) {
        return t.value(QStringLiteral("dl_limit")).toInteger() == 20480 &&
               t.value(QStringLiteral("up_limit")).toInteger() == 10240 &&
               qFuzzyCompare(t.value(QStringLiteral("ratio_limit")).toDouble(), 1.5) &&
               t.value(QStringLiteral("seeding_time_limit")).toInteger() == 100;
    });
    QCOMPARE(info.value(QStringLiteral("dl_limit")).toInteger(), qint64(20480));
    QCOMPARE(info.value(QStringLiteral("up_limit")).toInteger(), qint64(10240));
    QCOMPARE(info.value(QStringLiteral("ratio_limit")).toDouble(), 1.5);
    QCOMPARE(info.value(QStringLiteral("seeding_time_limit")).toInteger(), qint64(100));

    // 0 = unlimited for a speed, -2 = the global default for ratio/time.
    m_app->setLimits({kFixtureTwo}, 0, 0, -2, -2);
    const QJsonObject reset = pollUntil(kFixtureTwo, [](const QJsonObject &t) {
        return t.value(QStringLiteral("dl_limit")).toInteger() == 0 &&
               t.value(QStringLiteral("up_limit")).toInteger() == 0 &&
               qFuzzyCompare(t.value(QStringLiteral("ratio_limit")).toDouble(), -2.0);
    });
    QCOMPARE(reset.value(QStringLiteral("dl_limit")).toInteger(), qint64(0));
    QCOMPARE(reset.value(QStringLiteral("up_limit")).toInteger(), qint64(0));

    // T5.5 Q14: the action rides along by its wire name; the playground is 2.15.1, so App
    // sends it (and `supportsShareLimitAction` says so). Back to Default afterwards, so the
    // shared fixture is left as found even if the compare fails.
    QVERIFY(m_app->supportsShareLimitAction());
    m_app->setLimits({kFixtureTwo}, -1, -1, -2, -2, QStringLiteral("EnableSuperSeeding"));
    const QJsonObject acted = pollUntil(kFixtureTwo, [](const QJsonObject &t) {
        return t.value(QStringLiteral("share_limit_action")).toString() ==
               QStringLiteral("EnableSuperSeeding");
    });
    m_app->setLimits({kFixtureTwo}, -1, -1, -2, -2, QString());
    const QJsonObject defaulted = pollUntil(kFixtureTwo, [](const QJsonObject &t) {
        return t.value(QStringLiteral("share_limit_action")).toString() ==
               QStringLiteral("Default");
    });
    QCOMPARE(acted.value(QStringLiteral("share_limit_action")).toString(),
             QStringLiteral("EnableSuperSeeding"));
    QCOMPARE(defaulted.value(QStringLiteral("share_limit_action")).toString(),
             QStringLiteral("Default"));
    // The model's row map carries it too, for the sheet's prefill (once the poll caught up).
    QTRY_COMPARE_WITH_TIMEOUT(
        m_app->torrentRow(kFixtureTwo).value(QStringLiteral("shareLimitAction")).toString(),
        QStringLiteral("Default"),
        8000);
}

void TestAppActions::renameAndRenameBack()
{
    m_app->rename(kFixtureOne, QStringLiteral("fixture-one-t43"));
    QJsonObject info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return t.value(QStringLiteral("name")).toString() == QStringLiteral("fixture-one-t43");
    });
    QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("fixture-one-t43"));

    m_app->rename(kFixtureOne, QStringLiteral("fixture-one"));
    info = pollUntil(kFixtureOne, [](const QJsonObject &t) {
        return t.value(QStringLiteral("name")).toString() == QStringLiteral("fixture-one");
    });
    QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("fixture-one"));
}

void TestAppActions::deleteRemovesOnlyWhatThisTestAdded()
{
    const QString hash = QStringLiteral("aaaaaaaabbbbaaaaaaaabbbbaaaaaaaabbbbaaaa");
    QbtClient::AddOptions options;
    options.category = QStringLiteral("fixtures");
    options.stopped = true;
    const QbtResult<QByteArray> added =
        await(this,
              m_app->client()->torrentsAddUrls(
                  {QStringLiteral("magnet:?xt=urn:btih:%1&dn=t43-delete-me").arg(hash)}, options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    m_addedHash = hash;

    QJsonObject info = pollUntil(hash, [](const QJsonObject &t) { return !t.isEmpty(); });
    QVERIFY2(!info.isEmpty(), "the torrent this test added never showed up");

    m_app->remove({hash}, true);
    info = pollUntil(hash, [](const QJsonObject &t) { return t.isEmpty(); });
    QVERIFY2(info.isEmpty(), "remove() did not delete the torrent this test added");
    m_addedHash.clear();
}

void TestAppActions::deleteFilesDefaultPersists()
{
    QCOMPARE(m_app->deleteFilesDefault(), false); // the fresh XDG_CONFIG_HOME's default

    m_app->setDeleteFilesDefault(true);
    QCOMPARE(m_app->deleteFilesDefault(), true);

    App reopened;
    QCOMPARE(reopened.deleteFilesDefault(), true);

    m_app->setDeleteFilesDefault(false);
    App reopenedAgain;
    QCOMPARE(reopenedAgain.deleteFilesDefault(), false);
}

// docs/12 G4: the options General and the actions menu change, through App, on fixture-two.
// Automatic management is left out on purpose: turning it on moves the torrent to its
// category's save path, which a round trip would then have to put back by hand.
void TestAppActions::perTorrentOptionsRoundTrip()
{
    const auto appSees = [this](bool omaseed::core::Torrent::*field, bool on) {
        return waitFor(
            [this, field, on]() {
                return m_app->torrents()->indexOfHash(kFixtureTwo) >= 0 &&
                       m_app->torrents()->torrent(kFixtureTwo).*field == on;
            },
            8000);
    };
    const auto serverHas = [this](const char *key, bool on) {
        const QString name = QString::fromLatin1(key);
        return pollUntil(kFixtureTwo,
                         [&](const QJsonObject &t) { return t.value(name).toBool() == on; })
                   .value(name)
                   .toBool() == on;
    };

    QVERIFY2(appSees(&omaseed::core::Torrent::seqDl, false), "fixture-two starts sequential");
    m_app->setSequentialDownload({kFixtureTwo}, true);
    QVERIFY2(serverHas("seq_dl", true), "setSequentialDownload(true) did not reach the server");
    QVERIFY(appSees(&omaseed::core::Torrent::seqDl, true));
    // Asking for what it already is sends nothing: the toggle endpoint would flip it back.
    m_app->setSequentialDownload({kFixtureTwo}, true);
    QTest::qWait(600);
    QVERIFY2(serverHas("seq_dl", true), "a second set to the same value flipped it");
    m_app->setSequentialDownload({kFixtureTwo}, false);
    QVERIFY(serverHas("seq_dl", false));
    QVERIFY(appSees(&omaseed::core::Torrent::seqDl, false));

    m_app->setFirstLastPiecePrio({kFixtureTwo}, true);
    QVERIFY2(serverHas("f_l_piece_prio", true), "first/last did not reach the server");
    QVERIFY(appSees(&omaseed::core::Torrent::firstLastPiecePrio, true));
    m_app->setFirstLastPiecePrio({kFixtureTwo}, false);
    QVERIFY(serverHas("f_l_piece_prio", false));

    m_app->setSuperSeeding({kFixtureTwo}, true);
    QVERIFY2(serverHas("super_seeding", true), "super seeding did not reach the server");
    QVERIFY(appSees(&omaseed::core::Torrent::superSeeding, true));
    m_app->setSuperSeeding({kFixtureTwo}, false);
    QVERIFY(serverHas("super_seeding", false));
}

QTEST_MAIN(TestAppActions)
#include "tst_app_actions.moc"
