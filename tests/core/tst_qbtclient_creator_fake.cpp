// QbtClient's torrent creator calls (#146, DECISIONS C27) against a fake server: every field
// sent (startSeeding always off, one tier per tracker), the task id, the status entry, the
// .torrent's bytes, qBittorrent's 409 words kept, and 404 for a task it doesn't know. The
// bodies are the 5.3.0 playground's; the trackers are invented.

#include "FakeHttp.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

using namespace fakehttp;
using omaseed::core::QbtClient;

namespace {

QString field(const Recorded &request, const QString &name)
{
    return QUrlQuery(QString::fromUtf8(request.body)).queryItemValue(name, QUrl::FullyDecoded);
}

} // namespace

class TestQbtClientCreatorFake : public QObject
{
    Q_OBJECT

private slots:
    void addTaskSendsEveryField();
    void statusIsOneTask();
    void anUnknownTaskIs404();
    void theTorrentFileAndItsConflictWords();
    void deleteTaskSendsTheId();
    void theCreatorNeedsWebApi2104();
};

// C27 CR12: every field, startSeeding off, no torrentFilePath; trackers one tier each.
void TestQbtClientCreatorFake::addTaskSendsEveryField()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(200, "OK", R"({"taskID":"52abf916-abf5-406a-a724-db433c3277e7"})");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    QbtClient::CreateTask task;
    task.sourcePath = QStringLiteral("/mnt/media/share/field notes & sketches");
    task.format = QStringLiteral("v1");
    task.pieceSize = 1024 * 1024;
    task.isPrivate = true;
    task.trackers = {QStringLiteral("https://tracker-a.example/announce/k3y"),
                     QStringLiteral("udp://open.tracker-c.example:6969/announce")};
    task.urlSeeds = {QStringLiteral("https://mirror.example/a/"),
                     QStringLiteral("https://mirror.example/b/")};
    task.source = QStringLiteral("TA");
    task.comment = QStringLiteral("walks, 2026");
    const auto added = await(this, client.torrentCreatorAddTask(task));
    QVERIFY(!added.isError());
    QCOMPARE(added.value(), QStringLiteral("52abf916-abf5-406a-a724-db433c3277e7"));
    QCOMPARE(server.requests.size(), 1);
    const Recorded &request = server.requests.at(0);
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.target, QByteArrayLiteral("/api/v2/torrentcreator/addTask"));
    QCOMPARE(field(request, QStringLiteral("sourcePath")),
             QStringLiteral("/mnt/media/share/field notes & sketches"));
    QCOMPARE(field(request, QStringLiteral("format")), QStringLiteral("v1"));
    QCOMPARE(field(request, QStringLiteral("pieceSize")), QStringLiteral("1048576"));
    QCOMPARE(field(request, QStringLiteral("private")), QStringLiteral("true"));
    QCOMPARE(field(request, QStringLiteral("ignoreDotfiles")), QStringLiteral("true"));
    QCOMPARE(field(request, QStringLiteral("startSeeding")), QStringLiteral("false"));
    QCOMPARE(field(request, QStringLiteral("trackers")),
             QStringLiteral("https://tracker-a.example/announce/k3y||"
                            "udp://open.tracker-c.example:6969/announce"));
    QCOMPARE(field(request, QStringLiteral("urlSeeds")),
             QStringLiteral("https://mirror.example/a/|https://mirror.example/b/"));
    QCOMPARE(field(request, QStringLiteral("source")), QStringLiteral("TA"));
    QCOMPARE(field(request, QStringLiteral("comment")), QStringLiteral("walks, 2026"));
    QVERIFY(!QUrlQuery(QString::fromUtf8(request.body))
                 .hasQueryItem(QStringLiteral("torrentFilePath")));

    // A POST is never sent twice for a transient failure.
    FakeServer broken;
    broken.setHandler([](const Recorded &, int) { return httpResponse(500, "Error", "x"); });
    client.setBaseUrl(broken.url());
    QVERIFY(await(this, client.torrentCreatorAddTask(task)).isError());
    QCOMPARE(broken.requests.size(), 1);
}

void TestQbtClientCreatorFake::statusIsOneTask()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(
            200,
            "OK",
            R"([{"format":"v1","ignoreDotfiles":true,"pieceSize":0,"private":false,)"
            R"("progress":42,"sourcePath":"/mnt/media/share/a","status":"Running",)"
            R"("taskID":"t1","timeAdded":1792855650,"timeStarted":1792855650,)"
            R"("trackers":["udp://open.tracker-c.example:6969/announce"],"urlSeeds":[""]}])");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto status = await(this, client.torrentCreatorStatus(QStringLiteral("t1")));
    QVERIFY(!status.isError());
    QCOMPARE(status.value().value(QStringLiteral("status")).toString(), QStringLiteral("Running"));
    QCOMPARE(status.value().value(QStringLiteral("progress")).toInt(), 42);
    QCOMPARE(server.requests.at(0).method, QByteArrayLiteral("GET"));
    QCOMPARE(server.requests.at(0).target,
             QByteArrayLiteral("/api/v2/torrentcreator/status?taskID=t1"));
}

void TestQbtClientCreatorFake::anUnknownTaskIs404()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(404, "Not Found", ""); });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto status = await(this, client.torrentCreatorStatus(QStringLiteral("gone")));
    QVERIFY(status.isError());
    QCOMPARE(status.error().httpStatus(), 404);
}

void TestQbtClientCreatorFake::theTorrentFileAndItsConflictWords()
{
    const QByteArray torrent = "d8:announce26:udp://a.example:1/announce4:infod4:name4:demoee";
    FakeServer server;
    server.setHandler([torrent](const Recorded &, int index) {
        return index == 0 ? httpResponse(200, "OK", torrent)
                          : httpResponse(409, "Conflict", "Torrent creation failed.");
    });
    QbtClient client;
    client.setBaseUrl(server.url());
    const auto file = await(this, client.torrentCreatorTorrentFile(QStringLiteral("t1")));
    QVERIFY(!file.isError());
    QCOMPARE(file.value(), torrent);
    QCOMPARE(server.requests.at(0).target,
             QByteArrayLiteral("/api/v2/torrentcreator/torrentFile?taskID=t1"));
    const auto failed = await(this, client.torrentCreatorTorrentFile(QStringLiteral("t1")));
    QVERIFY(failed.isError());
    QCOMPARE(failed.error().httpStatus(), 409);
    QVERIFY(failed.error().message().contains(QStringLiteral("Torrent creation failed.")));
}

void TestQbtClientCreatorFake::deleteTaskSendsTheId()
{
    FakeServer server;
    QbtClient client;
    client.setBaseUrl(server.url());
    QVERIFY(!await(this, client.torrentCreatorDeleteTask(QStringLiteral("t1"))).isError());
    QCOMPARE(server.requests.at(0).method, QByteArrayLiteral("POST"));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrentcreator/deleteTask"));
    QCOMPARE(field(server.requests.at(0), QStringLiteral("taskID")), QStringLiteral("t1"));
}

void TestQbtClientCreatorFake::theCreatorNeedsWebApi2104()
{
    QVERIFY(!QbtClient::supportsTorrentCreator(QString()));
    QVERIFY(!QbtClient::supportsTorrentCreator(QStringLiteral("2.9.3")));
    QVERIFY(!QbtClient::supportsTorrentCreator(QStringLiteral("2.10.3")));
    QVERIFY(QbtClient::supportsTorrentCreator(QStringLiteral("2.10.4")));
    QVERIFY(QbtClient::supportsTorrentCreator(QStringLiteral("2.16.2")));
}

QTEST_MAIN(TestQbtClientCreatorFake)
#include "tst_qbtclient_creator_fake.moc"
