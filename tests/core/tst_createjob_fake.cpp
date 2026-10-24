// core::CreateJob (#146, DECISIONS C27, board 49 §1) against a fake qBittorrent: a public
// creation from addTask to seeding (seedMode at the folder's parent, the file kept), a private
// one never added, qBittorrent's failure words with the passkey masked, a task the server
// forgot (a restart), a cancel while hashing and one before the id is known, a refused add that
// keeps the .torrent, and a task taken up again after omaseed's restart. Trackers and keys are
// invented.

#include "FakeHttp.hpp"

#include "core/CreateJob.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::CreateJob;
using omaseed::core::QbtClient;

namespace {

const QByteArray kTorrent =
    "d8:announce26:udp://a.example:1/announce10:created by18:qBittorrent v5.3.04:infod5:filesld6:"
    "lengthi3e4:pathl5:a.txteee4:name11:photo-walks12:piece lengthi16384e6:pieces20:"
    "aaaaaaaaaaaaaaaaaaaaee";

QByteArray statusBody(const QByteArray &status, int progress, const QByteArray &extra = {})
{
    return R"([{"taskID":"t1","status":")" + status + R"(","progress":)" +
           QByteArray::number(progress) +
           R"(,"pieceSize":16384,"trackers":["https://tracker-a.example/announce/k3y5"])" + extra +
           "}]";
}

QString formField(const Recorded &request, const QString &name)
{
    return QUrlQuery(QString::fromUtf8(request.body)).queryItemValue(name, QUrl::FullyDecoded);
}

/// A multipart part's value.
QByteArray partValue(const Recorded &request, const QByteArray &name)
{
    const QByteArray marker = "name=\"" + name + "\"\r\n\r\n";
    const qsizetype at = request.body.indexOf(marker);
    if (at < 0)
        return {};
    const qsizetype start = at + marker.size();
    return request.body.mid(start, request.body.indexOf("\r\n--", start) - start);
}

int countOf(const FakeServer &server, const QByteArray &path)
{
    int n = 0;
    for (const Recorded &request : server.requests)
        n += request.target.startsWith("/api/v2/" + path) ? 1 : 0;
    return n;
}

const Recorded *lastOf(const FakeServer &server, const QByteArray &path)
{
    for (qsizetype i = server.requests.size() - 1; i >= 0; --i)
        if (server.requests.at(i).target.startsWith("/api/v2/" + path))
            return &server.requests.at(i);
    return nullptr;
}

CreateJob::Request request(bool addAfter)
{
    CreateJob::Request request;
    request.task.sourcePath = QStringLiteral("/mnt/media/share/photo-walks");
    request.task.trackers = {QStringLiteral("udp://a.example:1/announce")};
    request.name = QStringLiteral("photo-walks");
    request.addAfter = addAfter;
    request.category = QStringLiteral("photos");
    request.apiVersion = QStringLiteral("2.16.2");
    return request;
}

} // namespace

class TestCreateJobFake : public QObject
{
    Q_OBJECT

private slots:
    void aPublicOneSeedsWhereItIs();
    void aPrivateOneIsNeverAdded();
    void qBittorrentsFailureWordsMasked();
    void aForgottenTaskIsARestart();
    void cancelWhileHashing();
    void cancelBeforeTheIdIsKnown();
    void aRefusedAddKeepsTheFile();
    void aRunningTaskIsTakenUpAgain();
};

void TestCreateJobFake::aPublicOneSeedsWhereItIs()
{
    FakeServer server;
    int polls = 0;
    server.setHandler([&polls](const Recorded &request, int) {
        const QByteArray &target = request.target;
        if (target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        if (target.startsWith("/api/v2/torrentcreator/status")) {
            ++polls;
            return httpResponse(200,
                                "OK",
                                polls == 1   ? statusBody("Queued", 0)
                                : polls == 2 ? statusBody("Running", 40)
                                             : statusBody("Finished", 100));
        }
        if (target.startsWith("/api/v2/torrentcreator/torrentFile"))
            return httpResponse(200, "OK", kTorrent);
        if (target.startsWith("/api/v2/torrents/add"))
            return httpResponse(200,
                                "OK",
                                R"({"added_torrent_ids":["x"],"failure_count":0,)"
                                R"("pending_count":0,"success_count":1})");
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    job.setPollInterval(std::chrono::milliseconds(10));
    QSignalSpy started(&job, &CreateJob::taskStarted);
    QSignalSpy finished(&job, &CreateJob::finished);
    QList<CreateJob::State> seen;
    connect(&job, &CreateJob::changed, this, [&]() {
        if (seen.isEmpty() || seen.last() != job.state())
            seen.append(job.state());
    });
    job.start(request(true));
    QCOMPARE(job.state(), CreateJob::State::Starting);
    QVERIFY(job.isActive());
    QVERIFY(finished.wait(5000));
    QCOMPARE(started.first().first().toString(), QStringLiteral("t1"));
    QCOMPARE(job.state(), CreateJob::State::Done);
    QCOMPARE(job.failure(), CreateJob::Failure::None);
    QCOMPARE(seen,
             (QList<CreateJob::State>{CreateJob::State::Starting,
                                      CreateJob::State::Queued,
                                      CreateJob::State::Hashing,
                                      CreateJob::State::Fetching,
                                      CreateJob::State::Adding,
                                      CreateJob::State::Done}));
    QCOMPARE(job.progress(), 100);
    QCOMPARE(job.pieceSize(), qint64(16384));
    QCOMPARE(job.bytes(), kTorrent);
    QVERIFY(job.torrent().isValid());
    QCOMPARE(job.torrent().name, QStringLiteral("photo-walks"));
    QTRY_COMPARE_WITH_TIMEOUT(countOf(server, "torrentcreator/deleteTask"), 1, 5000);
    // startSeeding off at addTask (CR12)…
    QCOMPARE(formField(*lastOf(server, "torrentcreator/addTask"), QStringLiteral("startSeeding")),
             QStringLiteral("false"));
    // …then the add: seed mode at the folder's parent, as it is, started.
    const Recorded *add = lastOf(server, "torrents/add");
    QVERIFY(add != nullptr);
    QCOMPARE(partValue(*add, "savepath"), QByteArrayLiteral("/mnt/media/share"));
    QCOMPARE(partValue(*add, "seedMode"), QByteArrayLiteral("true"));
    QCOMPARE(partValue(*add, "autoTMM"), QByteArrayLiteral("false"));
    QCOMPARE(partValue(*add, "contentLayout"), QByteArrayLiteral("Original"));
    QCOMPARE(partValue(*add, "stopped"), QByteArrayLiteral("false"));
    QCOMPARE(partValue(*add, "category"), QByteArrayLiteral("photos"));
    QVERIFY(add->body.contains(kTorrent));
}

// CR7: a private one is saved by the caller, never added.
void TestCreateJobFake::aPrivateOneIsNeverAdded()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        if (request.target.startsWith("/api/v2/torrentcreator/status"))
            return httpResponse(200, "OK", statusBody("Finished", 100));
        if (request.target.startsWith("/api/v2/torrentcreator/torrentFile"))
            return httpResponse(200, "OK", kTorrent);
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    job.setPollInterval(std::chrono::milliseconds(10));
    QSignalSpy finished(&job, &CreateJob::finished);
    job.start(request(false));
    QVERIFY(finished.wait(5000));
    QCOMPARE(job.state(), CreateJob::State::Done);
    QCOMPARE(job.bytes(), kTorrent);
    QCOMPARE(countOf(server, "torrents/add"), 0);
}

// CR17: qBittorrent's reason, its lead dropped and any announce URL masked.
void TestCreateJobFake::qBittorrentsFailureWordsMasked()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        if (request.target.startsWith("/api/v2/torrentcreator/status"))
            return httpResponse(
                200,
                "OK",
                statusBody(
                    "Failed",
                    0,
                    R"(,"errorMessage":"Create new torrent file failed. Reason: )"
                    R"(Permission denied [system:13]. https://tracker-a.example/announce/k3y5")"));
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    job.setPollInterval(std::chrono::milliseconds(10));
    QSignalSpy finished(&job, &CreateJob::finished);
    job.start(request(true));
    QVERIFY(finished.wait(5000));
    QCOMPARE(job.state(), CreateJob::State::Failed);
    QCOMPARE(job.failure(), CreateJob::Failure::Hashing);
    QVERIFY(job.errorText().startsWith(QStringLiteral("Permission denied [system:13].")));
    QVERIFY(!job.errorText().contains(QStringLiteral("k3y5")));
    QTRY_COMPARE_WITH_TIMEOUT(countOf(server, "torrentcreator/deleteTask"), 1, 5000);
    QCOMPARE(countOf(server, "torrents/add"), 0);
}

void TestCreateJobFake::aForgottenTaskIsARestart()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        if (request.target.startsWith("/api/v2/torrentcreator/status"))
            return httpResponse(404, "Not Found", "");
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    QSignalSpy finished(&job, &CreateJob::finished);
    job.start(request(true));
    QVERIFY(finished.wait(5000));
    QCOMPARE(job.failure(), CreateJob::Failure::Restarted);
    QCOMPARE(countOf(server, "torrentcreator/deleteTask"), 0);
}

void TestCreateJobFake::cancelWhileHashing()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        if (request.target.startsWith("/api/v2/torrentcreator/status"))
            return httpResponse(200, "OK", statusBody("Running", 61));
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    job.setPollInterval(std::chrono::milliseconds(10));
    job.start(request(true));
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), CreateJob::State::Hashing, 5000);
    QCOMPARE(job.progress(), 61);
    QSignalSpy finished(&job, &CreateJob::finished);
    job.cancel();
    QCOMPARE(job.state(), CreateJob::State::Cancelled);
    QCOMPARE(finished.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(server, "torrentcreator/deleteTask"), 1, 5000);
    QCOMPARE(formField(*lastOf(server, "torrentcreator/deleteTask"), QStringLiteral("taskID")),
             QStringLiteral("t1"));
    const int polls = countOf(server, "torrentcreator/status");
    QTest::qWait(100);
    QCOMPARE(countOf(server, "torrentcreator/status"), polls);
}

void TestCreateJobFake::cancelBeforeTheIdIsKnown()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        return httpResponse(200, "OK", statusBody("Running", 1));
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    QSignalSpy finished(&job, &CreateJob::finished);
    job.start(request(true));
    job.cancel();
    QVERIFY(finished.wait(5000));
    QCOMPARE(job.state(), CreateJob::State::Cancelled);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(server, "torrentcreator/deleteTask"), 1, 5000);
    QCOMPARE(countOf(server, "torrentcreator/status"), 0);
}

// C27 §7: the add's own refusal; the .torrent is in hand all the same.
void TestCreateJobFake::aRefusedAddKeepsTheFile()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/addTask"))
            return httpResponse(200, "OK", R"({"taskID":"t1"})");
        if (request.target.startsWith("/api/v2/torrentcreator/status"))
            return httpResponse(200, "OK", statusBody("Finished", 100));
        if (request.target.startsWith("/api/v2/torrentcreator/torrentFile"))
            return httpResponse(200, "OK", kTorrent);
        if (request.target.startsWith("/api/v2/torrents/add"))
            return httpResponse(409, "Conflict", "Torrent is already present");
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    job.setPollInterval(std::chrono::milliseconds(10));
    QSignalSpy finished(&job, &CreateJob::finished);
    job.start(request(true));
    QVERIFY(finished.wait(5000));
    QCOMPARE(job.state(), CreateJob::State::Done);
    QCOMPARE(job.failure(), CreateJob::Failure::AddFailed);
    QCOMPARE(job.bytes(), kTorrent);
    QVERIFY(!job.errorText().isEmpty());
}

void TestCreateJobFake::aRunningTaskIsTakenUpAgain()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        if (request.target.startsWith("/api/v2/torrentcreator/status"))
            return httpResponse(200, "OK", statusBody("Finished", 100));
        if (request.target.startsWith("/api/v2/torrentcreator/torrentFile"))
            return httpResponse(200, "OK", kTorrent);
        return httpResponse(200, "OK", "");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    CreateJob job(&client);
    qint64 clock = 50000;
    job.setClock([&clock]() { return clock; });
    QSignalSpy finished(&job, &CreateJob::finished);
    job.resume(request(false), QStringLiteral("t1"), 20000);
    QVERIFY(finished.wait(5000));
    QCOMPARE(job.state(), CreateJob::State::Done);
    QCOMPARE(job.taskId(), QStringLiteral("t1"));
    QCOMPARE(countOf(server, "torrentcreator/addTask"), 0);
    QCOMPARE(job.tookMs(), qint64(30000));
}

QTEST_MAIN(TestCreateJobFake)
#include "tst_createjob_fake.moc"
