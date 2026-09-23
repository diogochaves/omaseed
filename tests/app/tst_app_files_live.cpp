// N1 — the Files tab's server actions through App (src/ui/app/App.hpp, the "// --- N1" block
// and setFilePriority) against the :8081 playground: a priority on one file, the "by shown
// file order" split, a file rename and a folder rename, each read back through
// torrents/files. QSKIPs when the playground does not answer.
//
// It borrows tests/fixtures/multi_file_v1_unicode.torrent (three files in two nested folders),
// added stopped under its own category and deleted with its files and category at the end.
// tst_torrentdetails_live uses the same torrent; both are LIVE tests, which ctest runs under
// one resource lock, so they never overlap.

#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
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

using namespace omaseed::core;
using omaseed::app::App;

namespace {

const char *kFixtureFile = OMASEED_FIXTURES_DIR "/multi_file_v1_unicode.torrent";
const QString kHash = QStringLiteral("6f04b6c7642d15dc5cfa6cd2478e7f128a0e556f");
const QString kCategory = QStringLiteral("omaseed-n1-live");

QUrl playgroundUrl()
{
    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("QBT_TEST_URL"));
    return QUrl(fromEnv.isEmpty() ? QStringLiteral("http://127.0.0.1:8081") : fromEnv);
}

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

class TestAppFilesLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void priorityRenameAndFolderRenameReachTheServer();

private:
    /// torrents/files as {name: priority}, or empty on an error.
    QHash<QString, int> serverFiles();

    QTemporaryDir m_config;
    QbtClient *m_client = nullptr;
};

void TestAppFilesLive::initTestCase()
{
    const QUrl url = playgroundUrl();
    if (!url.isValid() || !answers(url))
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");

    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    qputenv("OMASEED_SERVER_URL", url.toString().toUtf8());

    m_client = new QbtClient(this);
    m_client->setBaseUrl(url);
    // A run that failed half-way leaves the torrent behind; start from nothing.
    await(this, m_client->torrentsDelete({kHash}, /*deleteFiles=*/true));

    QFile file(QString::fromLatin1(kFixtureFile));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    QbtClient::AddOptions options;
    options.stopped = true; // the test box never downloads anything
    options.category = kCategory;
    const QbtResult<QByteArray> added =
        await(this,
              m_client->torrentsAddFile(
                  QStringLiteral("multi_file_v1_unicode.torrent"), file.readAll(), options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    QVERIFY2(waitFor([this]() { return serverFiles().size() == 3; }, 10000),
             "the fixture's file list never appeared");
}

void TestAppFilesLive::cleanupTestCase()
{
    if (m_client == nullptr)
        return;
    await(this, m_client->torrentsDelete({kHash}, /*deleteFiles=*/true));
    await(this, m_client->torrentsRemoveCategories({kCategory}));
}

QHash<QString, int> TestAppFilesLive::serverFiles()
{
    QHash<QString, int> out;
    const QbtResult<QJsonArray> files = await(this, m_client->torrentsFiles(kHash));
    if (files.isError())
        return out;
    for (const QJsonValue &value : files.value()) {
        const QJsonObject file = value.toObject();
        out.insert(file.value(QStringLiteral("name")).toString(),
                   file.value(QStringLiteral("priority")).toInt());
    }
    return out;
}

void TestAppFilesLive::priorityRenameAndFolderRenameReachTheServer()
{
    App app;
    app.start();
    QVERIFY(app.hasServer());
    QVERIFY2(waitFor([&app]() { return app.torrents()->indexOfHash(kHash) >= 0; }, 10000),
             "the fixture never reached the table");

    app.selectTorrent(kHash);
    TorrentFilesModel *files = app.details()->files();
    // Unicode Pack/, café/, notes.txt, 日本語/, sub フォルダ/, ファイル.bin, root.txt.
    QVERIFY2(waitFor([files]() { return files->count() == 7; }, 10000),
             "the Files tab never filled");
    QSignalSpy failed(&app, &App::actionFailed);

    // One file skipped: the row changes at once, the server follows.
    const QString notes = QStringLiteral("Unicode Pack/café/notes.txt");
    app.setFilePriority(kHash, files->fileIndexes(files->rowOfPath(notes)), 0);
    QCOMPARE(files->priority(files->rowOfPath(notes)), 0);
    QVERIFY2(waitFor([&]() { return serverFiles().value(notes, -1) == 0; }, 10000),
             "the skip never reached the server");

    // "By shown file order" on the whole torrent, in the torrent's own order: one file per
    // group, so maximal, high, normal.
    app.setFilePriorityGroups(kHash, files->shownOrderGroups(0));
    QVERIFY2(waitFor(
                 [&]() {
                     const QHash<QString, int> now = serverFiles();
                     return now.value(notes) == 7 &&
                            now.value(QStringLiteral(
                                "Unicode Pack/日本語/sub フォルダ/ファイル.bin")) == 6 &&
                            now.value(QStringLiteral("Unicode Pack/root.txt")) == 1;
                 },
                 10000),
             "the shown-order priorities never reached the server");

    // A file renamed by its name, inside its own folder.
    app.renameTorrentFile(kHash, notes, QStringLiteral("memo.txt"), false);
    const QString memo = QStringLiteral("Unicode Pack/café/memo.txt");
    QVERIFY2(waitFor([&]() { return serverFiles().contains(memo); }, 10000),
             "the file rename never reached the server");

    // A folder renamed takes its files with it; the pane's rows follow the next poll.
    app.renameTorrentFile(kHash, QStringLiteral("Unicode Pack/café"), QStringLiteral("cafe"), true);
    const QString moved = QStringLiteral("Unicode Pack/cafe/memo.txt");
    QVERIFY2(waitFor([&]() { return serverFiles().contains(moved); }, 10000),
             "the folder rename never reached the server");
    QVERIFY2(waitFor([&]() { return files->rowOfPath(moved) >= 0; }, 10000),
             "the Files tab never showed the renamed folder");
    QCOMPARE(failed.count(), 0);

    app.engine()->stop();
}

QTEST_MAIN(TestAppFilesLive)
#include "tst_app_files_live.moc"
