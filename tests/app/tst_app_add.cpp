// T4.2 — the add flow of App (spec v2 §3 Fig. 5 and Fig. 10).
//
// Four things are checked here: that `parseAddable` reads a magnet, a `.torrent` fixture and
// an http URL into the map the add sheet draws; that `findDuplicate` recognises an infohash
// the library already holds (the demo library stands in for one); that a real add of a
// `.torrent` reaches the :8081 playground with its category, tags and save path; and that a
// magnet added stopped turns up stopped. The two live cases are skipped when the playground
// is not answering, and both clean up after themselves — nothing here ever touches :8080.

#include "core/AddFilesModel.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
using omaseed::app::App;

namespace {

/// The `.torrent` both live cases add. Its infohash is the manifest's.
const char *kFixtureFile = "single_file_v1.torrent";
const char *kFixtureHash = "857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04";
const char *kFixtureName = "Single.bin";

/// A magnet nobody seeds, for the "add stopped" case. Its own hash, so the two live cases
/// never collide with each other or with tst_syncengine_live's fixture.
const char *kMagnetHash = "0102030405060708090a0b0c0d0e0f1011121315";

/// docs/11 D1: the eight-file tree the file picker is tested with. Sintel/ (ids 3 and 4) is
/// skipped and README.txt (id 7) raised to maximal before the add.
const char *kTreeFile = "open_movies_v1.torrent";
const char *kTreeHash = "429d28a2cdae51f937735430e0a88cec3ee76cbc";
const char *kTreeRename = "omaseed-d1 renamed";

/// The category, tag and save path the live add names. Created and removed by the test.
#define kCategory "omaseed-t42"
#define kTag "omaseed-add"
#define kSavePath "/tmp/omaseed-t42"

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
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

/// Whether `torrents/info` says this one is stopped. 5.x says "stoppedDL"/"stoppedUP", 4.x
/// "pausedDL"/"pausedUP"; a just-added torrent is "checkingResumeData" for a moment first.
bool isStopped(const QJsonObject &info)
{
    const QString state = info.value(QStringLiteral("state")).toString();
    return state.startsWith(QStringLiteral("stopped"), Qt::CaseInsensitive) ||
           state.startsWith(QStringLiteral("paused"), Qt::CaseInsensitive);
}

/// Every OMASEED_* key App reads, cleared so a developer's own shell cannot change a test.
void clearServerEnvironment()
{
    for (const char *name : {"OMASEED_SERVER_URL",
                             "OMASEED_SERVER_NAME",
                             "OMASEED_API_KEY",
                             "OMASEED_USERNAME",
                             "OMASEED_PASSWORD",
                             "OMASEED_DEMO_ADD"})
        qunsetenv(name);
}

} // namespace

class TestAppAdd : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void parsesAMagnet();
    void parsesAnHttpUrl();
    void refusesNonsense();
    void parsesATorrentFileOffTheMainThread();
    void findsADuplicateInTheLibrary();
    void findsNoDuplicateForAnUnknownHash();
    void loadsATorrentsFilesForThePicker();
    void demoTorrentOpensThePicker();

    void addsATorrentFileWithItsOptions();
    void addsAMagnetStopped();
    void addsWithFileChoicesRenameAndLimits();

private:
    /// The one `torrents/info` entry for `hash`, or an empty object.
    QJsonObject infoFor(QbtClient &client, const QString &hash);
    void deleteFixtures();

    QUrl m_url;
    bool m_live = false;
    /// The server store must never be the developer's own: every case runs against an empty
    /// config directory, so App can only ever find the server this test puts in the
    /// environment. Nothing here can reach :8080.
    QTemporaryDir m_config;
    QByteArray m_savedConfigHome;
};

void TestAppAdd::initTestCase()
{
    m_url = playgroundUrl();
    m_live = m_url.isValid() && answers(m_url);
}

void TestAppAdd::init()
{
    QVERIFY(m_config.isValid());
    m_savedConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    clearServerEnvironment();
}

void TestAppAdd::cleanup()
{
    if (m_live)
        deleteFixtures();
    clearServerEnvironment();
    if (m_savedConfigHome.isEmpty())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_savedConfigHome);
}

void TestAppAdd::deleteFixtures()
{
    QbtClient client;
    client.setBaseUrl(m_url);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("hashes"),
                      QStringLiteral("%1|%2|%3")
                          .arg(QString::fromLatin1(kFixtureHash),
                               QString::fromLatin1(kMagnetHash),
                               QString::fromLatin1(kTreeHash)));
    form.addQueryItem(QStringLiteral("deleteFiles"), QStringLiteral("true"));
    await(this, client.post(QStringLiteral("torrents/delete"), form));
}

QJsonObject TestAppAdd::infoFor(QbtClient &client, const QString &hash)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hashes"), hash);
    const QbtResult<QByteArray> result =
        await(this, client.get(QStringLiteral("torrents/info"), query));
    if (result.isError())
        return QJsonObject();
    const QJsonArray rows = QJsonDocument::fromJson(result.value()).array();
    return rows.isEmpty() ? QJsonObject() : rows.first().toObject();
}

// ---- local parsing ----------------------------------------------------------------------

void TestAppAdd::parsesAMagnet()
{
    App app;
    const QVariantMap map = app.parseAddable(
        QStringLiteral("magnet:?xt=urn:btih:9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071"
                       "&dn=Fedora-Workstation-Live-44-1.6.x86_64.iso"
                       "&xl=2512345678"));

    QCOMPARE(map.value(QStringLiteral("kind")).toString(), QStringLiteral("magnet"));
    QVERIFY(map.value(QStringLiteral("valid")).toBool());
    QVERIFY(!map.value(QStringLiteral("pending")).toBool());
    QCOMPARE(map.value(QStringLiteral("name")).toString(),
             QStringLiteral("Fedora-Workstation-Live-44-1.6.x86_64.iso"));
    QCOMPARE(map.value(QStringLiteral("infoHashV1")).toString(),
             QStringLiteral("9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071"));
    // Fig. 5's subtitle: "… · 2.34 GB · from clipboard".
    QCOMPARE(map.value(QStringLiteral("sizeText")).toString(), QStringLiteral("2.34 GB"));
}

void TestAppAdd::parsesAnHttpUrl()
{
    App app;
    const QVariantMap map = app.parseAddable(
        QStringLiteral("https://example.org/iso/archlinux-2026.09.01-x86_64.iso.torrent"));

    QCOMPARE(map.value(QStringLiteral("kind")).toString(), QStringLiteral("url"));
    QVERIFY(map.value(QStringLiteral("valid")).toBool());
    // Nothing is parsed for a URL — the name is the last path segment, and there is no hash,
    // so the duplicate test cannot run for one.
    QCOMPARE(map.value(QStringLiteral("name")).toString(),
             QStringLiteral("archlinux-2026.09.01-x86_64.iso.torrent"));
    QVERIFY(map.value(QStringLiteral("infoHashV1")).toString().isEmpty());
}

void TestAppAdd::refusesNonsense()
{
    App app;
    const QVariantMap map = app.parseAddable(QStringLiteral("not a link at all"));
    QCOMPARE(map.value(QStringLiteral("kind")).toString(), QStringLiteral("none"));
    QVERIFY(!map.value(QStringLiteral("valid")).toBool());
    QVERIFY(!map.value(QStringLiteral("error")).toString().isEmpty());
}

void TestAppAdd::parsesATorrentFileOffTheMainThread()
{
    App app;
    QSignalSpy parsed(&app, &App::addableParsed);

    const QString path =
        QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kFixtureFile);
    const QVariantMap first = app.parseAddable(path);

    QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
    QVERIFY2(first.value(QStringLiteral("pending")).toBool(),
             "the SHA-1 is computed on a worker thread, so the first answer is only a promise");
    QVERIFY(!first.value(QStringLiteral("valid")).toBool());

    QVERIFY(parsed.wait(10000));
    const QVariantMap map = parsed.first().first().toMap();
    QCOMPARE(map.value(QStringLiteral("token")).toString(),
             first.value(QStringLiteral("token")).toString());
    QVERIFY(map.value(QStringLiteral("valid")).toBool());
    QVERIFY(!map.value(QStringLiteral("pending")).toBool());
    QCOMPARE(map.value(QStringLiteral("name")).toString(), QString::fromLatin1(kFixtureName));
    QCOMPARE(map.value(QStringLiteral("infoHashV1")).toString(), QString::fromLatin1(kFixtureHash));
    QCOMPARE(map.value(QStringLiteral("sizeText")).toString(), QStringLiteral("49 KB"));
}

void TestAppAdd::loadsATorrentsFilesForThePicker()
{
    App app;
    QSignalSpy parsed(&app, &App::addableParsed);
    app.parseAddable(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kTreeFile));
    QVERIFY(parsed.wait(10000));
    const QVariantMap map = parsed.first().first().toMap();
    QVERIFY(map.value(QStringLiteral("valid")).toBool());
    QCOMPARE(map.value(QStringLiteral("fileCount")).toInt(), 8);

    app.loadAddFiles(map.value(QStringLiteral("token")).toString());
    QCOMPARE(app.addFiles()->fileCount(), 8);
    QVERIFY(!app.addFiles()->isCustomised());

    // A token that names nothing (a magnet has none) empties the picker.
    app.loadAddFiles(QString());
    QCOMPARE(app.addFiles()->fileCount(), 0);
}

void TestAppAdd::demoTorrentOpensThePicker()
{
    App app;
    QVERIFY(app.demoAddText().isEmpty());
    app.startDemo(16);
    qputenv("OMASEED_DEMO_ADD", QByteArrayLiteral("torrent"));
    const QString path = app.demoAddText();
    QVERIFY(path.endsWith(QStringLiteral(".torrent")));

    QSignalSpy parsed(&app, &App::addableParsed);
    const QVariantMap first = app.parseAddable(path);
    QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
    QVERIFY(parsed.wait(10000));
    const QVariantMap map = parsed.first().first().toMap();
    QVERIFY2(map.value(QStringLiteral("valid")).toBool(),
             qPrintable(map.value(QStringLiteral("error")).toString()));
    QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("Blender Open Movies"));
    QCOMPARE(map.value(QStringLiteral("fileCount")).toInt(), 8);
}

// ---- Fig. 10 ------------------------------------------------------------------------------

void TestAppAdd::findsADuplicateInTheLibrary()
{
    App app;
    app.startDemo(16);
    QCOMPARE(app.torrents()->count(), 16);

    const QString known =
        app.torrents()->data(app.torrents()->index(0), TorrentModel::HashRole).toString();
    QVERIFY(!known.isEmpty());

    const QVariantMap row = app.findDuplicate(known, QString());
    QCOMPARE(row.value(QStringLiteral("hash")).toString(), known);
    QVERIFY(!row.value(QStringLiteral("name")).toString().isEmpty());
    // Fig. 10's body reads "<state> since <added>".
    QVERIFY(!row.value(QStringLiteral("stateText")).toString().isEmpty());
    QVERIFY(!row.value(QStringLiteral("addedOnText")).toString().isEmpty());

    // The same hash in capitals is the same torrent.
    QCOMPARE(app.findDuplicate(known.toUpper(), QString()).value(QStringLiteral("hash")).toString(),
             known);
}

void TestAppAdd::findsNoDuplicateForAnUnknownHash()
{
    App app;
    app.startDemo(16);
    QVERIFY(app.findDuplicate(QString::fromLatin1(kFixtureHash), QString()).isEmpty());
    QVERIFY(app.findDuplicate(QString(), QString()).isEmpty());
}

// ---- live, against the :8081 playground ---------------------------------------------------

void TestAppAdd::addsATorrentFileWithItsOptions()
{
    if (!m_live)
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");

    deleteFixtures(); // a leftover from an interrupted run must not confuse us

    QbtClient probe;
    probe.setBaseUrl(m_url);
    // The server keeps a category's save path, so it has to exist before the add names it.
    await(this, probe.torrentsCreateCategory(QStringLiteral(kCategory), QStringLiteral(kSavePath)));

    qputenv("OMASEED_SERVER_URL", m_url.toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", QByteArrayLiteral("playground"));

    App app;
    app.start();
    QVERIFY(app.hasServer());

    // The sheet's payload: the parse map plus the rows of Fig. 5.
    QSignalSpy parsed(&app, &App::addableParsed);
    const QString path =
        QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kFixtureFile);
    QVariantMap request = app.parseAddable(path);
    QVERIFY(parsed.wait(10000));
    request = parsed.first().first().toMap();
    QVERIFY(request.value(QStringLiteral("valid")).toBool());

    request.insert(QStringLiteral("category"), QStringLiteral(kCategory));
    request.insert(QStringLiteral("tags"), QStringList{QStringLiteral(kTag)});
    request.insert(QStringLiteral("savePath"), QStringLiteral(kSavePath));
    request.insert(QStringLiteral("autoTMM"), false);
    request.insert(QStringLiteral("stopped"), true);
    request.insert(QStringLiteral("upLimitKb"), 500);
    request.insert(QStringLiteral("sequentialDownload"), true);
    request.insert(QStringLiteral("contentLayout"), QStringLiteral("Original"));

    QSignalSpy finished(&app, &App::addFinished);
    // A successful add also moves the table's cursor once the new hash turns up in a sync.
    QSignalSpy shown(&app, &App::showTorrentRequested);
    app.addTorrent(request);
    QVERIFY(finished.wait(20000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(finished.first().at(2).toString(), QString::fromLatin1(kFixtureHash));

    QJsonObject info;
    QVERIFY2(waitFor(
                 [&]() {
                     info = infoFor(probe, QString::fromLatin1(kFixtureHash));
                     return !info.isEmpty();
                 },
                 15000),
             "the added torrent never turned up in torrents/info");

    QCOMPARE(info.value(QStringLiteral("name")).toString(), QString::fromLatin1(kFixtureName));
    QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral(kCategory));
    QCOMPARE(info.value(QStringLiteral("tags")).toString(), QStringLiteral(kTag));
    QVERIFY2(
        info.value(QStringLiteral("save_path")).toString().contains(QStringLiteral("omaseed-t42")),
        qPrintable(info.value(QStringLiteral("save_path")).toString()));
    QCOMPARE(info.value(QStringLiteral("up_limit")).toInt(), 500 * 1024);
    QVERIFY(info.value(QStringLiteral("seq_dl")).toBool());
    // A just-added torrent spends a moment in "checkingResumeData" before it settles.
    QVERIFY2(waitFor(
                 [&]() {
                     info = infoFor(probe, QString::fromLatin1(kFixtureHash));
                     return isStopped(info);
                 },
                 15000),
             qPrintable(info.value(QStringLiteral("state")).toString()));

    QVERIFY2(waitFor([&shown]() { return shown.count() > 0; }, 15000),
             "the add never asked the table to show the new torrent");
    QCOMPARE(shown.first().first().toString(), QString::fromLatin1(kFixtureHash));

    app.engine()->stop();
    await(this, probe.torrentsRemoveCategories(QStringList{QStringLiteral(kCategory)}));
    await(this, probe.torrentsDeleteTags(QStringList{QStringLiteral(kTag)}));
}

void TestAppAdd::addsAMagnetStopped()
{
    if (!m_live)
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");

    deleteFixtures();

    qputenv("OMASEED_SERVER_URL", m_url.toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", QByteArrayLiteral("playground"));

    App app;
    app.start();
    QVERIFY(app.hasServer());

    QVariantMap request =
        app.parseAddable(QStringLiteral("magnet:?xt=urn:btih:%1&dn=omaseed-t42-magnet")
                             .arg(QString::fromLatin1(kMagnetHash)));
    QVERIFY(request.value(QStringLiteral("valid")).toBool());
    request.insert(QStringLiteral("stopped"), true);

    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(20000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));

    QbtClient probe;
    probe.setBaseUrl(m_url);
    QJsonObject info;
    QVERIFY2(waitFor(
                 [&]() {
                     info = infoFor(probe, QString::fromLatin1(kMagnetHash));
                     return !info.isEmpty();
                 },
                 15000),
             "the added magnet never turned up in torrents/info");
    QVERIFY2(waitFor(
                 [&]() {
                     info = infoFor(probe, QString::fromLatin1(kMagnetHash));
                     return isStopped(info);
                 },
                 15000),
             qPrintable(info.value(QStringLiteral("state")).toString()));

    app.engine()->stop();
}

void TestAppAdd::addsWithFileChoicesRenameAndLimits()
{
    if (!m_live)
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");

    deleteFixtures();

    qputenv("OMASEED_SERVER_URL", m_url.toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", QByteArrayLiteral("playground"));

    App app;
    app.start();
    QVERIFY(app.hasServer());

    QSignalSpy parsed(&app, &App::addableParsed);
    app.parseAddable(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kTreeFile));
    QVERIFY(parsed.wait(10000));
    QVariantMap request = parsed.first().first().toMap();
    QVERIFY(request.value(QStringLiteral("valid")).toBool());

    // The sheet's picker: Sintel/ skipped, README.txt to maximal.
    app.loadAddFiles(request.value(QStringLiteral("token")).toString());
    AddFilesModel *files = app.addFiles();
    for (int row = 0; row < files->count(); ++row) {
        const QString name =
            files->data(files->index(row, 0), TorrentFilesModel::NameRole).toString();
        if (name == QLatin1String("Sintel/"))
            files->toggleWanted(row);
        else if (name == QLatin1String("README.txt"))
            files->setPriority(row, 7);
    }
    QVERIFY(files->isCustomised());

    request.insert(QStringLiteral("filePriorities"), files->priorityList());
    request.insert(QStringLiteral("rename"), QString::fromLatin1(kTreeRename));
    request.insert(QStringLiteral("dlLimitKb"), 300);
    request.insert(QStringLiteral("firstLastPiecePrio"), true);
    request.insert(QStringLiteral("stopped"), false);

    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(20000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(finished.first().at(2).toString(), QString::fromLatin1(kTreeHash));

    QbtClient probe;
    probe.setBaseUrl(m_url);
    QJsonObject info;
    QVERIFY2(waitFor(
                 [&]() {
                     info = infoFor(probe, QString::fromLatin1(kTreeHash));
                     return !info.isEmpty();
                 },
                 15000),
             "the added torrent never turned up in torrents/info");
    QCOMPARE(info.value(QStringLiteral("name")).toString(), QString::fromLatin1(kTreeRename));
    QCOMPARE(info.value(QStringLiteral("dl_limit")).toInt(), 300 * 1024);
    QVERIFY(info.value(QStringLiteral("f_l_piece_prio")).toBool());
    // Added stopped for the priorities, then started, because `stopped` was false.
    QVERIFY2(waitFor(
                 [&]() {
                     info = infoFor(probe, QString::fromLatin1(kTreeHash));
                     return !info.isEmpty() && !isStopped(info);
                 },
                 15000),
             qPrintable(info.value(QStringLiteral("state")).toString()));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hash"), QString::fromLatin1(kTreeHash));
    const QbtResult<QByteArray> listed =
        await(this, probe.get(QStringLiteral("torrents/files"), query));
    QVERIFY(!listed.isError());
    const QJsonArray rows = QJsonDocument::fromJson(listed.value()).array();
    QCOMPARE(rows.size(), 8);
    QList<int> priorities;
    for (const QJsonValue &row : rows)
        priorities.append(row.toObject().value(QStringLiteral("priority")).toInt());
    QCOMPARE(priorities, (QList<int>{1, 1, 1, 0, 0, 1, 1, 7}));

    app.engine()->stop();
}

QTEST_MAIN(TestAppAdd)
#include "tst_app_add.moc"
