// N1 — the Files tab's server actions through App (src/ui/app/App.hpp, the "// --- N1" block
// and setFilePriority) against the :8081 playground: a priority on one file, the "by shown
// file order" split, a file rename and a folder rename, each read back through
// torrents/files; and #64's guard, which refuses a name a stray file already holds on disk.
// QSKIPs when the playground does not answer.
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

#include "LiveServer.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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
    return LiveServer::playground();
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

class TestAppFilesLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void priorityRenameAndFolderRenameReachTheServer();
    void manyFilesRenameReachesTheServer();
    void aNameAlreadyOnDiskIsNeverSent();

private:
    /// torrents/files as {name: priority}, or empty on an error.
    QHash<QString, int> serverFiles();
    /// Removes what earlier runs left of the fixture's folder on disk, when the playground
    /// saves on this machine and no torrent of it lives there. qBittorrent's delete with files
    /// does not always take every file the renames made, and a leftover file is one #64's
    /// guard rightly refuses to rename onto.
    void clearLeftovers();

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
    clearLeftovers();

    QFile file(QString::fromLatin1(kFixtureFile));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    QbtClient::AddOptions options;
    options.stopped = true; // the test box never downloads anything
    options.category = kCategory;
    const Result<QByteArray> added =
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
    clearLeftovers();
}

void TestAppFilesLive::clearLeftovers()
{
    const Result<QJsonObject> prefs = await(this, m_client->appPreferences());
    if (prefs.isError())
        return;
    const QString savePath = prefs.value().value(QStringLiteral("save_path")).toString();
    const QString folder = QDir::cleanPath(savePath + QStringLiteral("/Unicode Pack"));
    if (!QDir::isAbsolutePath(savePath) || !QFileInfo(folder).isDir())
        return;
    const Result<QJsonArray> torrents = await(this, m_client->torrentsInfo());
    if (torrents.isError())
        return;
    for (const QJsonValue &value : torrents.value()) {
        const QString content =
            QDir::cleanPath(value.toObject().value(QStringLiteral("content_path")).toString());
        if (content == folder || content.startsWith(folder + QLatin1Char('/')))
            return;
    }
    QDir(folder).removeRecursively();
}

QHash<QString, int> TestAppFilesLive::serverFiles()
{
    QHash<QString, int> out;
    const Result<QJsonArray> files = await(this, m_client->torrentsFiles(kHash));
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

// #11: the rename sheet's run, one torrents/renameFile per file, read back from the server.
// Whatever the first test left, the .txt files are numbered in front of their names.
void TestAppFilesLive::manyFilesRenameReachesTheServer()
{
    App app;
    app.start();
    QVERIFY(app.hasServer());
    QVERIFY2(waitFor([&app]() { return app.torrents()->indexOfHash(kHash) >= 0; }, 10000),
             "the fixture never reached the table");
    app.selectTorrent(kHash);
    TorrentFilesModel *files = app.details()->files();
    QVERIFY2(waitFor([files]() { return files->filePaths().size() == 3; }, 10000),
             "the Files tab never filled");

    QStringList texts;
    for (const QString &path : files->filePaths()) {
        if (path.endsWith(QStringLiteral(".txt")))
            texts.append(path);
    }
    QCOMPARE(texts.size(), 2);
    const QVariantMap options{{QStringLiteral("find"), QStringLiteral("^")},
                              {QStringLiteral("replace"), QStringLiteral("{nn} ")},
                              {QStringLiteral("regex"), true}};
    QSignalSpy done(&app, &App::torrentFilesRenamed);
    QSignalSpy failed(&app, &App::actionFailed);
    app.renameTorrentFiles(kHash, texts, options);
    QVERIFY2(done.wait(10000), "the renames never finished");
    QCOMPARE(done.at(0).at(1).toInt(), 2);
    QVERIFY(done.at(0).at(4).toString().isEmpty());
    QCOMPARE(failed.count(), 0);

    const auto numbered = [](const QString &path, int n) {
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        return path.left(slash + 1) + QStringLiteral("%1 ").arg(n, 2, 10, QLatin1Char('0')) +
               path.mid(slash + 1);
    };
    QVERIFY2(waitFor(
                 [&]() {
                     const QHash<QString, int> now = serverFiles();
                     return now.contains(numbered(texts.at(0), 1)) &&
                            now.contains(numbered(texts.at(1), 2));
                 },
                 10000),
             "the numbered names never reached the server");

    app.engine()->stop();
}

// #64: qBittorrent lets a rename replace a file of that name that the torrent does not own.
// The playground saves on this machine, so App sees its disk (FreeSpaceProbe's calibration)
// and refuses such a name before asking: a file, a folder holding a stray file, and the
// many-files plan. The stray files are written beside the fixture's (added stopped, so its
// own files are not on disk) and removed again. QSKIPs where this machine does not see the
// playground's disk (inside an omabox, say).
void TestAppFilesLive::aNameAlreadyOnDiskIsNeverSent()
{
    App app;
    app.start();
    QVERIFY(app.hasServer());
    QVERIFY2(waitFor([&app]() { return app.torrents()->indexOfHash(kHash) >= 0; }, 10000),
             "the fixture never reached the table");
    app.selectTorrent(kHash);
    TorrentFilesModel *files = app.details()->files();
    QVERIFY2(waitFor([files]() { return files->filePaths().size() == 3; }, 10000),
             "the Files tab never filled");
    // Any folder but the default save path reads "local" only once the calibration holds.
    if (!waitFor(
            [&app]() {
                return app.freeSpaceFor(QDir::tempPath()).value(QStringLiteral("source")) ==
                       QStringLiteral("local");
            },
            10000))
        QSKIP("this machine does not see the playground's disk");

    // A file of the fixture inside a folder of its own, whatever the tests before renamed.
    QString file;
    for (const QString &path : files->filePaths()) {
        if (path.count(QLatin1Char('/')) >= 2)
            file = path;
    }
    QVERIFY(!file.isEmpty());
    const QString folder = file.left(file.lastIndexOf(QLatin1Char('/')));
    const QString parent = folder.left(folder.lastIndexOf(QLatin1Char('/')));
    const QString strayFile =
        app.torrentFileLocalPath(kHash, folder + QStringLiteral("/stray-64.txt"));
    const QString strayFolder =
        app.torrentFileLocalPath(kHash, parent + QStringLiteral("/stray-64/keep.txt"));
    QVERIFY(QDir::isAbsolutePath(strayFile));
    const auto write = [](const QString &path) {
        QDir().mkpath(QFileInfo(path).path());
        QFile out(path);
        return out.open(QIODevice::WriteOnly) && out.write("keep me") == 7;
    };
    QVERIFY(write(strayFile));
    QVERIFY(write(strayFolder));
    const auto cleanup = qScopeGuard([&]() {
        QFile::remove(strayFile);
        QDir(QFileInfo(strayFolder).path()).removeRecursively();
    });

    QSignalSpy requested(&app, &App::torrentFileRenameRequested);
    QSignalSpy failed(&app, &App::actionFailed);
    const QHash<QString, int> before = serverFiles();
    app.renameTorrentFile(kHash, file, QStringLiteral("stray-64.txt"), false);
    app.renameTorrentFile(kHash, folder, QStringLiteral("stray-64"), true);
    const QVariantMap options{{QStringLiteral("find"), QStringLiteral("^.+$")},
                              {QStringLiteral("replace"), QStringLiteral("stray-64.txt")},
                              {QStringLiteral("regex"), true},
                              {QStringLiteral("includeExtension"), true}};
    const QVariantMap plan = app.planFileRenames({file}, options);
    QCOMPARE(plan.value(QStringLiteral("collisions")).toInt(), 1);
    app.renameTorrentFiles(kHash, {file}, options);
    QCOMPARE(requested.count(), 0);
    QCOMPARE(failed.count(), 3);

    // Nothing reached the server, and the stray files are as they were.
    QTest::qWait(1500);
    QCOMPARE(serverFiles(), before);
    QFile kept(strayFile);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("keep me"));

    app.engine()->stop();
}

QTEST_MAIN(TestAppFilesLive)
#include "tst_app_files_live.moc"
