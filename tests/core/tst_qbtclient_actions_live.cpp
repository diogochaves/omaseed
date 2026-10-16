// QbtClient torrent actions (T2.5) against the playground qBittorrent (QBT_TEST_URL, from
// ~/.config/omaseed/dev.env). Every action is verified through torrents/info, per the plan's
// acceptance criteria. QSKIPs when the server does not answer. Never talks to the real
// library server: only QBT_TEST_URL is used here.
//
// Everything this test adds (torrents, categories, tags, directories) is removed again in
// cleanupTestCase(), including on failure, so a run leaves the playground exactly as found.

#include "core/QbtClient.hpp"

#include "LiveServer.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <utility>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::Reply;
using omaseed::core::Result;
using omaseed::core::TorrentsInfoQuery;

namespace {

QString env(const char *name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

template<class T>
Result<T> await(QObject *context, Reply<T> *reply, int timeoutMs = 20000)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const Result<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace

class TestQbtClientActionsLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void addUrlsHonoursOptions();
    void startStopForceRecheckReannounce();
    void categoryAndTags();
    void locationAndSpeedLimits();
    void shareLimitsRoundTrip();
    void shareLimitActionRoundTrip();
    void renameAndToggles();
    void queuePriorityMoves();
    void addFileFixtureAndDeleteWithFiles();
    void deleteWithoutFilesKeepsData();

private:
    /// Polls torrents/info for `hash` until `predicate` is true or the deadline passes.
    /// Returns the last torrent object seen (possibly not satisfying predicate, if it timed
    /// out — callers still assert on it so the failure message is useful).
    QJsonObject pollUntil(const QString &hash,
                          const std::function<bool(const QJsonObject &)> &predicate,
                          int timeoutMs = 5000);
    QJsonObject torrentInfo(const QString &hash);
    /// Deletes `hash` if present and waits for it to actually disappear, then adds the file,
    /// retrying the add a couple of times on a 409: this fixture's hash is fixed, so a
    /// leftover from a previous run — or another agent's own use of the same fixture on this
    /// shared playground — can otherwise still be settling when the add lands.
    Result<QByteArray> reAddFile(const QString &hash,
                                 const QString &fileName,
                                 const QByteArray &bytes,
                                 const QbtClient::AddOptions &options);

    QUrl m_playground;
    std::unique_ptr<QbtClient> m_client;
    QString m_downloadsRoot;

    QStringList m_addedHashes;
    QStringList m_addedCategories;
    QStringList m_addedTags;
    QStringList m_addedDirs; // removed last-to-first
};

QJsonObject TestQbtClientActionsLive::torrentInfo(const QString &hash)
{
    TorrentsInfoQuery query;
    query.hashes = {hash};
    const Result<QJsonArray> result = await(this, m_client->torrentsInfo(query));
    if (result.isError() || result.value().isEmpty())
        return {};
    return result.value().at(0).toObject();
}

QJsonObject TestQbtClientActionsLive::pollUntil(
    const QString &hash, const std::function<bool(const QJsonObject &)> &predicate, int timeoutMs)
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

Result<QByteArray> TestQbtClientActionsLive::reAddFile(const QString &hash,
                                                       const QString &fileName,
                                                       const QByteArray &bytes,
                                                       const QbtClient::AddOptions &options)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        await(this, m_client->torrentsDelete(QStringList{hash}, false));
        pollUntil(hash, [](const QJsonObject &t) { return t.isEmpty(); }, 2000);

        const Result<QByteArray> added =
            await(this, m_client->torrentsAddFile(fileName, bytes, options));
        if (!added.isError() || added.error().kind() != ApiError::Kind::Conflict)
            return added;

        QEventLoop loop;
        QTimer::singleShot(300, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return await(this, m_client->torrentsAddFile(fileName, bytes, options));
}

void TestQbtClientActionsLive::initTestCase()
{
    m_playground = LiveServer::playground();
    if (!answers(m_playground))
        QSKIP("the playground does not answer: scripts/dev/test-server.sh start");

    m_client = std::make_unique<QbtClient>();
    m_client->setBaseUrl(m_playground);

    // The server's default save path, not one built from this process's HOME: the server
    // creates these directories, and it need not share a HOME with the test (an omabox box,
    // another user, a remote playground). Only the fallback matches test-server.sh.
    const Result<QJsonObject> prefs = await(this, m_client->appPreferences());
    if (!prefs.isError())
        m_downloadsRoot = prefs.value().value(QStringLiteral("save_path")).toString();
    while (m_downloadsRoot.size() > 1 && m_downloadsRoot.endsWith(QLatin1Char('/')))
        m_downloadsRoot.chop(1);
    if (m_downloadsRoot.isEmpty()) {
        const QString profile = env("OMASEED_TEST_PROFILE");
        m_downloadsRoot =
            (profile.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share/omaseed-test")
                               : profile) +
            QStringLiteral("/downloads");
    }
}

void TestQbtClientActionsLive::cleanupTestCase()
{
    if (!m_client)
        return;

    for (const QString &hash : std::as_const(m_addedHashes))
        await(this, m_client->torrentsDelete(QStringList{hash}, true));
    m_addedHashes.clear();

    if (!m_addedTags.isEmpty())
        await(this, m_client->torrentsDeleteTags(m_addedTags));
    if (!m_addedCategories.isEmpty())
        await(this, m_client->torrentsRemoveCategories(m_addedCategories));

    // removeRecursively(), never rmpath(): rmpath also deletes empty *parent* directories,
    // which would take the shared downloads root (not ours to remove) with it.
    for (auto it = m_addedDirs.crbegin(); it != m_addedDirs.crend(); ++it)
        QDir(*it).removeRecursively();
}

void TestQbtClientActionsLive::addUrlsHonoursOptions()
{
    const QString hash = QStringLiteral("aaaaaaaa1111aaaaaaaa1111aaaaaaaa1111aaaa");
    const QString savePath = m_downloadsRoot + QStringLiteral("/t25");
    m_addedDirs.append(savePath);
    m_addedCategories.append(QStringLiteral("t25"));
    // The tags an add creates outlive the torrent it created them for, so they belong in the
    // cleanup list too (T4.8: the playground had collected `a` and `b` from earlier runs).
    m_addedTags.append({QStringLiteral("a"), QStringLiteral("b")});

    QbtClient::AddOptions options;
    options.category = QStringLiteral("t25");
    options.tags = {QStringLiteral("a"), QStringLiteral("b")};
    options.savepath = savePath;
    options.stopped = true;
    options.sequentialDownload = true;
    options.rename = QStringLiteral("t25-probe");

    const Result<QByteArray> added =
        await(this,
              m_client->torrentsAddUrls(
                  QStringList{QStringLiteral("magnet:?xt=urn:btih:%1&dn=t25-source").arg(hash)},
                  options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    m_addedHashes.append(hash);

    const QJsonObject info = pollUntil(hash, [](const QJsonObject &t) { return !t.isEmpty(); });
    QVERIFY2(!info.isEmpty(), "added torrent never showed up in torrents/info");
    QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral("t25"));
    QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("t25-probe"));
    QCOMPARE(info.value(QStringLiteral("save_path")).toString(), savePath);
    QCOMPARE(info.value(QStringLiteral("seq_dl")).toBool(), true);
    QCOMPARE(info.value(QStringLiteral("state")).toString(), QStringLiteral("stoppedDL"));
    const QString tags = info.value(QStringLiteral("tags")).toString();
    QVERIFY2(tags.contains(QStringLiteral("a")) && tags.contains(QStringLiteral("b")),
             qPrintable(tags));
}

void TestQbtClientActionsLive::startStopForceRecheckReannounce()
{
    QVERIFY(!m_addedHashes.isEmpty());
    const QString hash = m_addedHashes.first();

    const Result<QByteArray> started = await(this, m_client->torrentsStart(QStringList{hash}));
    QVERIFY2(!started.isError(), qPrintable(started.error().toString()));
    // No peers for a fake infohash, so it stalls rather than completing — the point is that
    // it left the stopped state, not that it finishes.
    const QJsonObject running = pollUntil(hash, [](const QJsonObject &t) {
        return t.value(QStringLiteral("state")).toString() != QStringLiteral("stoppedDL");
    });
    QVERIFY2(running.value(QStringLiteral("state")).toString() != QStringLiteral("stoppedDL"),
             qPrintable(running.value(QStringLiteral("state")).toString()));

    const Result<QByteArray> stopped = await(this, m_client->torrentsStop(QStringList{hash}));
    QVERIFY2(!stopped.isError(), qPrintable(stopped.error().toString()));
    const QJsonObject stoppedInfo = pollUntil(hash, [](const QJsonObject &t) {
        return t.value(QStringLiteral("state")).toString() == QStringLiteral("stoppedDL");
    });
    QCOMPARE(stoppedInfo.value(QStringLiteral("state")).toString(), QStringLiteral("stoppedDL"));

    const Result<QByteArray> forced =
        await(this, m_client->torrentsSetForceStart(QStringList{hash}, true));
    QVERIFY2(!forced.isError(), qPrintable(forced.error().toString()));
    const QJsonObject forcedInfo = pollUntil(
        hash, [](const QJsonObject &t) { return t.value(QStringLiteral("force_start")).toBool(); });
    QCOMPARE(forcedInfo.value(QStringLiteral("force_start")).toBool(), true);

    const Result<QByteArray> unforced =
        await(this, m_client->torrentsSetForceStart(QStringList{hash}, false));
    QVERIFY2(!unforced.isError(), qPrintable(unforced.error().toString()));
    await(this, m_client->torrentsStop(QStringList{hash}));

    const Result<QByteArray> rechecked = await(this, m_client->torrentsRecheck(QStringList{hash}));
    QVERIFY2(!rechecked.isError(), qPrintable(rechecked.error().toString()));

    const Result<QByteArray> reannounced =
        await(this, m_client->torrentsReannounce(QStringList{hash}));
    QVERIFY2(!reannounced.isError(), qPrintable(reannounced.error().toString()));
}

void TestQbtClientActionsLive::categoryAndTags()
{
    QVERIFY(!m_addedHashes.isEmpty());
    const QString hash = m_addedHashes.first();

    const Result<QByteArray> cleared =
        await(this, m_client->torrentsSetCategory(QStringList{hash}, QString()));
    QVERIFY2(!cleared.isError(), qPrintable(cleared.error().toString()));
    QCOMPARE(torrentInfo(hash).value(QStringLiteral("category")).toString(), QString());

    const Result<QByteArray> restored =
        await(this, m_client->torrentsSetCategory(QStringList{hash}, QStringLiteral("t25")));
    QVERIFY2(!restored.isError(), qPrintable(restored.error().toString()));
    QCOMPARE(torrentInfo(hash).value(QStringLiteral("category")).toString(), QStringLiteral("t25"));

    const Result<QByteArray> removed = await(
        this, m_client->torrentsRemoveTags(QStringList{hash}, QStringList{QStringLiteral("a")}));
    QVERIFY2(!removed.isError(), qPrintable(removed.error().toString()));
    QVERIFY(
        !torrentInfo(hash).value(QStringLiteral("tags")).toString().contains(QStringLiteral("a")));

    const Result<QByteArray> readded =
        await(this, m_client->torrentsAddTags(QStringList{hash}, QStringList{QStringLiteral("a")}));
    QVERIFY2(!readded.isError(), qPrintable(readded.error().toString()));
    const QString tags = torrentInfo(hash).value(QStringLiteral("tags")).toString();
    QVERIFY2(tags.contains(QStringLiteral("a")), qPrintable(tags));

    m_addedTags.append(QStringLiteral("solo-tag"));
    const Result<QByteArray> created =
        await(this, m_client->torrentsCreateTags(QStringList{QStringLiteral("solo-tag")}));
    QVERIFY2(!created.isError(), qPrintable(created.error().toString()));
    const Result<QJsonArray> allTags = await(this, m_client->torrentsTags());
    QVERIFY2(!allTags.isError(), qPrintable(allTags.error().toString()));
    bool found = false;
    for (const QJsonValue &tag : allTags.value())
        found = found || tag.toString() == QStringLiteral("solo-tag");
    QVERIFY(found);
}

void TestQbtClientActionsLive::locationAndSpeedLimits()
{
    QVERIFY(!m_addedHashes.isEmpty());
    const QString hash = m_addedHashes.first();

    const QString newLocation = m_downloadsRoot + QStringLiteral("/t25-moved");
    m_addedDirs.append(newLocation);
    const Result<QByteArray> moved =
        await(this, m_client->torrentsSetLocation(QStringList{hash}, newLocation));
    QVERIFY2(!moved.isError(), qPrintable(moved.error().toString()));
    const QJsonObject movedInfo = pollUntil(hash, [&](const QJsonObject &t) {
        return t.value(QStringLiteral("save_path")).toString() == newLocation;
    });
    QCOMPARE(movedInfo.value(QStringLiteral("save_path")).toString(), newLocation);

    const Result<QByteArray> dl =
        await(this, m_client->torrentsSetDownloadLimit(QStringList{hash}, 4096));
    QVERIFY2(!dl.isError(), qPrintable(dl.error().toString()));
    const Result<QByteArray> up =
        await(this, m_client->torrentsSetUploadLimit(QStringList{hash}, 8192));
    QVERIFY2(!up.isError(), qPrintable(up.error().toString()));
    const QJsonObject limited = torrentInfo(hash);
    QCOMPARE(limited.value(QStringLiteral("dl_limit")).toInt(), 4096);
    QCOMPARE(limited.value(QStringLiteral("up_limit")).toInt(), 8192);

    // Back to unlimited so cleanup does not leave a throttled torrent behind.
    await(this, m_client->torrentsSetDownloadLimit(QStringList{hash}, 0));
    await(this, m_client->torrentsSetUploadLimit(QStringList{hash}, 0));
}

void TestQbtClientActionsLive::shareLimitsRoundTrip()
{
    QVERIFY(!m_addedHashes.isEmpty());
    const QString hash = m_addedHashes.first();

    const Result<QByteArray> set =
        await(this, m_client->torrentsSetShareLimits(QStringList{hash}, 2.5, 100, 50));
    QVERIFY2(!set.isError(), qPrintable(set.error().toString()));
    const QJsonObject info = torrentInfo(hash);
    QCOMPARE(info.value(QStringLiteral("ratio_limit")).toDouble(), 2.5);
    QCOMPARE(info.value(QStringLiteral("seeding_time_limit")).toInt(), 100);
    QCOMPARE(info.value(QStringLiteral("inactive_seeding_time_limit")).toInt(), 50);

    // Back to "use the global default" so cleanup leaves nothing configured.
    const Result<QByteArray> cleared =
        await(this, m_client->torrentsSetShareLimits(QStringList{hash}, -2, -2, -2));
    QVERIFY2(!cleared.isError(), qPrintable(cleared.error().toString()));
}

void TestQbtClientActionsLive::shareLimitActionRoundTrip()
{
    QVERIFY(!m_addedHashes.isEmpty());
    const QString hash = m_addedHashes.first();

    // T5.5 Q14: the playground (5.2.3, API 2.15.1) echoes the action back as
    // `share_limit_action` in torrents/info, which is what prefills the sheet's chip row.
    const Result<QByteArray> set =
        await(this,
              m_client->torrentsSetShareLimits(
                  QStringList{hash}, -2, -2, -2, QbtClient::ShareLimitAction::Stop));
    QVERIFY2(!set.isError(), qPrintable(set.error().toString()));
    const QJsonObject info = pollUntil(hash, [](const QJsonObject &t) {
        return t.value(QStringLiteral("share_limit_action")).toString() == QStringLiteral("Stop");
    });
    // Restore before asserting: cleanupTestCase() deletes the torrent anyway, but a failed
    // compare must not leave a fixture the other live tests read in a non-default state.
    const Result<QByteArray> restored =
        await(this,
              m_client->torrentsSetShareLimits(
                  QStringList{hash}, -2, -2, -2, QbtClient::ShareLimitAction::Default));
    QCOMPARE(info.value(QStringLiteral("share_limit_action")).toString(), QStringLiteral("Stop"));
    QVERIFY2(!restored.isError(), qPrintable(restored.error().toString()));
    const QJsonObject back = pollUntil(hash, [](const QJsonObject &t) {
        return t.value(QStringLiteral("share_limit_action")).toString() ==
               QStringLiteral("Default");
    });
    QCOMPARE(back.value(QStringLiteral("share_limit_action")).toString(),
             QStringLiteral("Default"));

    // The fact behind the client's default argument: 5.2.3 refuses the call without the
    // field, so "omit when Default" is a UI notion, not a wire one.
    const Result<QByteArray> withoutField =
        await(this, m_client->torrentsSetShareLimits(QStringList{hash}, -2, -2, -2, std::nullopt));
    QVERIFY(withoutField.isError());
    QCOMPARE(withoutField.error().httpStatus(), 400);
}

void TestQbtClientActionsLive::renameAndToggles()
{
    QVERIFY(!m_addedHashes.isEmpty());
    const QString hash = m_addedHashes.first();

    const Result<QByteArray> renamed =
        await(this, m_client->torrentsRename(hash, QStringLiteral("t25-renamed")));
    QVERIFY2(!renamed.isError(), qPrintable(renamed.error().toString()));
    QCOMPARE(torrentInfo(hash).value(QStringLiteral("name")).toString(),
             QStringLiteral("t25-renamed"));

    const bool seqBefore = torrentInfo(hash).value(QStringLiteral("seq_dl")).toBool();
    const bool flpBefore = torrentInfo(hash).value(QStringLiteral("f_l_piece_prio")).toBool();

    const Result<QByteArray> seqToggled =
        await(this, m_client->torrentsToggleSequentialDownload(QStringList{hash}));
    QVERIFY2(!seqToggled.isError(), qPrintable(seqToggled.error().toString()));
    QCOMPARE(torrentInfo(hash).value(QStringLiteral("seq_dl")).toBool(), !seqBefore);

    const Result<QByteArray> flpToggled =
        await(this, m_client->torrentsToggleFirstLastPiecePrio(QStringList{hash}));
    QVERIFY2(!flpToggled.isError(), qPrintable(flpToggled.error().toString()));
    QCOMPARE(torrentInfo(hash).value(QStringLiteral("f_l_piece_prio")).toBool(), !flpBefore);

    const Result<QByteArray> autoOff =
        await(this, m_client->torrentsSetAutoManagement(QStringList{hash}, false));
    QVERIFY2(!autoOff.isError(), qPrintable(autoOff.error().toString()));
    QCOMPARE(torrentInfo(hash).value(QStringLiteral("auto_tmm")).toBool(), false);
}

void TestQbtClientActionsLive::queuePriorityMoves()
{
    const QString hashOne = QStringLiteral("cccccccc2222cccccccc2222cccccccc2222cccc");
    const QString hashTwo = QStringLiteral("dddddddd3333dddddddd3333dddddddd3333dddd");
    QbtClient::AddOptions options;
    options.stopped = true;

    const Result<QByteArray> addedOne =
        await(this,
              m_client->torrentsAddUrls(
                  QStringList{QStringLiteral("magnet:?xt=urn:btih:%1&dn=queue-one").arg(hashOne)},
                  options));
    QVERIFY2(!addedOne.isError(), qPrintable(addedOne.error().toString()));
    m_addedHashes.append(hashOne);

    const Result<QByteArray> addedTwo =
        await(this,
              m_client->torrentsAddUrls(
                  QStringList{QStringLiteral("magnet:?xt=urn:btih:%1&dn=queue-two").arg(hashTwo)},
                  options));
    QVERIFY2(!addedTwo.isError(), qPrintable(addedTwo.error().toString()));
    m_addedHashes.append(hashTwo);

    pollUntil(hashOne, [](const QJsonObject &t) { return !t.isEmpty(); });
    pollUntil(hashTwo, [](const QJsonObject &t) { return !t.isEmpty(); });

    const int priorityBefore = torrentInfo(hashTwo).value(QStringLiteral("priority")).toInt();

    const Result<QByteArray> topped = await(this, m_client->torrentsTopPrio(QStringList{hashTwo}));
    QVERIFY2(!topped.isError(), qPrintable(topped.error().toString()));
    const QJsonObject afterTop = pollUntil(hashTwo, [&](const QJsonObject &t) {
        return t.value(QStringLiteral("priority")).toInt() < priorityBefore;
    });
    QVERIFY2(afterTop.value(QStringLiteral("priority")).toInt() < priorityBefore,
             "topPrio did not move the torrent up the queue");

    const int priorityAfterTop = afterTop.value(QStringLiteral("priority")).toInt();
    const Result<QByteArray> bottomed =
        await(this, m_client->torrentsBottomPrio(QStringList{hashTwo}));
    QVERIFY2(!bottomed.isError(), qPrintable(bottomed.error().toString()));
    const QJsonObject afterBottom = pollUntil(hashTwo, [&](const QJsonObject &t) {
        return t.value(QStringLiteral("priority")).toInt() > priorityAfterTop;
    });
    QVERIFY2(afterBottom.value(QStringLiteral("priority")).toInt() > priorityAfterTop,
             "bottomPrio did not move the torrent down the queue");

    // increasePrio/decreasePrio: just assert they are accepted (order among two neighbours
    // may not change visibly depending on queue state).
    const Result<QByteArray> increased =
        await(this, m_client->torrentsIncreasePrio(QStringList{hashOne}));
    QVERIFY2(!increased.isError(), qPrintable(increased.error().toString()));
    const Result<QByteArray> decreased =
        await(this, m_client->torrentsDecreasePrio(QStringList{hashOne}));
    QVERIFY2(!decreased.isError(), qPrintable(decreased.error().toString()));
}

void TestQbtClientActionsLive::addFileFixtureAndDeleteWithFiles()
{
    const QByteArray bytes =
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    QVERIFY(!bytes.isEmpty());
    const QString hash = QStringLiteral("857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04");
    const QString savePath = m_downloadsRoot + QStringLiteral("/t25-file");
    m_addedDirs.append(savePath); // removed by the deleteFiles=true call itself, if it works

    QbtClient::AddOptions options;
    options.stopped = true;
    options.savepath = savePath;

    const Result<QByteArray> added =
        reAddFile(hash, QStringLiteral("single_file_v1.torrent"), bytes, options);
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));

    const QJsonObject info = pollUntil(hash, [](const QJsonObject &t) { return !t.isEmpty(); });
    QVERIFY2(!info.isEmpty(), "the fixture .torrent never showed up in torrents/info");
    QCOMPARE(info.value(QStringLiteral("hash")).toString(), hash);
    QCOMPARE(info.value(QStringLiteral("save_path")).toString(), savePath);
    // qBittorrent creates the save directory lazily (once it actually writes data, which a
    // stopped, peerless torrent may never do), so only assert its removal when it existed.
    const bool dirExistedBeforeDelete = QDir(savePath).exists();

    const Result<QByteArray> deleted =
        await(this, m_client->torrentsDelete(QStringList{hash}, true));
    QVERIFY2(!deleted.isError(), qPrintable(deleted.error().toString()));

    QTest::qWait(300); // qBittorrent removes the directory asynchronously
    QCOMPARE(torrentInfo(hash), QJsonObject());
    if (dirExistedBeforeDelete)
        QVERIFY2(!QDir(savePath).exists(), "deleteFiles=true left the save directory behind");
    m_addedDirs.removeAll(savePath); // already gone (or never existed); nothing to clean up
}

void TestQbtClientActionsLive::deleteWithoutFilesKeepsData()
{
    const QByteArray bytes = readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/v2_stub.torrent"));
    QVERIFY(!bytes.isEmpty());
    // v2_stub.torrent declares meta version 2; qBittorrent 5.x reports it in torrents/info
    // under the first 40 hex chars of its v2 (SHA-256) infohash, not the v1 one, even though
    // the fixture carries both (see tests/fixtures/fixtures.json, infoHashV2).
    const QString hash = QStringLiteral("0703e91a4e92560200a851c3f8e8283077f23335");
    const QString savePath = m_downloadsRoot + QStringLiteral("/t25-keep");
    m_addedDirs.append(savePath);

    QbtClient::AddOptions options;
    options.stopped = true;
    options.savepath = savePath;

    const Result<QByteArray> added =
        reAddFile(hash, QStringLiteral("v2_stub.torrent"), bytes, options);
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    pollUntil(hash, [](const QJsonObject &t) { return !t.isEmpty(); });

    const Result<QByteArray> deleted =
        await(this, m_client->torrentsDelete(QStringList{hash}, false));
    QVERIFY2(!deleted.isError(), qPrintable(deleted.error().toString()));

    QTest::qWait(200);
    QCOMPARE(torrentInfo(hash), QJsonObject());
    // deleteFiles=false: qBittorrent may or may not have created the directory yet for a
    // stopped, metadata-only add, so this only checks that no error occurred above; any
    // directory left behind is still removed by cleanupTestCase().
}

QTEST_MAIN(TestQbtClientActionsLive)

#include "tst_qbtclient_actions_live.moc"
