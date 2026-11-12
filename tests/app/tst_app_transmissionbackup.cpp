// Settings › Transmission › Backup's export, in the app (#88; docs/57 §3, §4; docs/DECISIONS.md
// C40 X1, X3): App on a fake Transmission (FakeTransmission) whose torrents are real .torrent
// fixtures kept in a folder of the test's, as the daemon's `torrents` folder would be. The
// export picks Transmission's exporter; whether the .torrent files are read from here is asked
// and said; a folder given for them is checked, remembered with the server and gone with it;
// and the result says which links are links because a file couldn't be read.

#include "FakeTransmission.hpp"

#include "core/GlobalState.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentsBackup.hpp"
#include "ui/app/App.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using omaseed::app::App;
using namespace fakehttp;
using namespace faketransmission;
using namespace omaseed::core;

namespace {

const QString kRecordedPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kRecordedSingle = QStringLiteral("2b86112ae20c6ca831cbfb3e4333c8acc6d21088");
const QString kRecordedMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
/// Where a daemon on another machine keeps its .torrent files.
const QString kFar = QStringLiteral("/srv/transmission/torrents");

QByteArray torrentBytes(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

} // namespace

class TestAppTransmissionBackup : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void theExportIsTransmissionsOwn();
    void filesOutOfSightAreSaidAndGoInAsLinks();
    void aFolderGivenIsCheckedAndRemembered();
    void aRememberedFolderThatWentFallsBack();
    void aQbittorrentAsksNothing();

private:
    /// A recorded row as the torrent of `fixtureName`, its .torrent in `m_folder`, the daemon
    /// saying it is under `daemonFolder`.
    void put(const QString &recorded, const char *fixtureName, const QString &daemonFolder);
    /// Three torrents: a pack and a single file with their .torrent files in `m_folder`, and
    /// the recorded magnet without metadata.
    void fill(const QString &daemonFolder);
    /// Saves the daemon as "seedbox", starts `app` on it and waits for its torrents.
    QString startOn(App &app);
    bool settled(App &app, const QString &reach);
    QString exportTo(App &app, const QString &path);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeTransmission> m_wire;
    PollAnswers m_library;
    QString m_folder;
};

void TestAppTransmissionBackup::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
    m_folder = m_home->filePath(QStringLiteral("daemon/torrents"));
    QVERIFY(QDir().mkpath(m_folder));
    QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral("empty"))));
    m_library = PollAnswers();
    m_library.sessionGet.insert(QStringLiteral("config_dir"),
                                m_home->filePath(QStringLiteral("daemon")));
    m_server = std::make_unique<FakeServer>();
    m_wire = std::make_unique<FakeTransmission>();
    m_server->setHandler(m_wire->handler());
    m_wire->answer = [this](const QString &method, const QJsonObject &params) {
        const QJsonArray ids = params.value(QStringLiteral("ids")).toArray();
        if (method != QLatin1String("torrent_get") || ids.isEmpty())
            return m_library.answer(method, params);
        // Only those asked for, as Transmission does with hashes for ids.
        PollAnswers some = m_library;
        some.torrents.removeIf([&ids](const QJsonObject &t) {
            return !ids.contains(t.value(QStringLiteral("hash_string")));
        });
        return some.answer(method, params);
    };
}

void TestAppTransmissionBackup::cleanup()
{
    m_wire.reset();
    m_server.reset();
    m_home.reset();
}

void TestAppTransmissionBackup::put(const QString &recorded,
                                    const char *fixtureName,
                                    const QString &daemonFolder)
{
    QJsonObject &json = m_library.byHash(recorded);
    const QByteArray bytes = torrentBytes(fixtureName);
    const TorrentFile parsed = TorrentFile::parse(bytes);
    QVERIFY(parsed.isValid());
    const QString hash = parsed.infoHashV1;
    json.insert(QStringLiteral("hash_string"), hash);
    json.insert(QStringLiteral("name"), parsed.name);
    QJsonArray files;
    QJsonArray stats;
    for (const QString &name : TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"))) {
        files.append(QJsonObject{{QStringLiteral("name"), name}});
        stats.append(
            QJsonObject{{QStringLiteral("wanted"), true}, {QStringLiteral("priority"), 0}});
    }
    json.insert(QStringLiteral("files"), files);
    json.insert(QStringLiteral("file_stats"), stats);
    json.insert(QStringLiteral("magnet_link"),
                QStringLiteral("magnet:?xt=urn:btih:%1&dn=x").arg(hash));
    json.insert(QStringLiteral("torrent_file"),
                daemonFolder + QLatin1Char('/') + hash + QStringLiteral(".torrent"));
    QFile file(QDir(m_folder).filePath(hash + QStringLiteral(".torrent")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

void TestAppTransmissionBackup::fill(const QString &daemonFolder)
{
    m_library.torrents.removeIf([](const QJsonObject &t) {
        const QString hash = t.value(QStringLiteral("hash_string")).toString();
        return hash != kRecordedPack && hash != kRecordedSingle && hash != kRecordedMagnet;
    });
    put(kRecordedPack, "multi_file_v1_unicode.torrent", daemonFolder);
    put(kRecordedSingle, "single_file_v1.torrent", daemonFolder);
    QJsonObject &magnet = m_library.byHash(kRecordedMagnet);
    magnet.insert(QStringLiteral("metadata_percent_complete"), 0);
    magnet.insert(QStringLiteral("magnet_link"),
                  QStringLiteral("magnet:?xt=urn:btih:%1&dn=x").arg(kRecordedMagnet));
}

QString TestAppTransmissionBackup::startOn(App &app)
{
    QUrl url = m_server->url();
    url.setPath(QStringLiteral("/transmission/web/"));
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    const QString id = app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), url.toString()},
        {QStringLiteral("authMode"), QStringLiteral("none")},
        {QStringLiteral("engine"), QStringLiteral("transmission")},
    });
    if (id.isEmpty() || !QTest::qWaitFor([&app]() { return app.torrents()->count() == 3; }, 5000))
        return {};
    return id;
}

bool TestAppTransmissionBackup::settled(App &app, const QString &reach)
{
    return QTest::qWaitFor([&app, &reach]() { return app.torrentFilesReach() == reach; }, 5000);
}

QString TestAppTransmissionBackup::exportTo(App &app, const QString &path)
{
    QSignalSpy finished(&app, &App::torrentsExportFinished);
    app.exportTorrents(path);
    if (app.torrentsBackupRunning() != QLatin1String("export"))
        return QStringLiteral("not running: ") + finished.value(0).value(0).toString();
    if (!finished.wait(5000))
        return QStringLiteral("never finished");
    return (finished.at(0).at(1).toBool() ? QStringLiteral("ok: ") : QStringLiteral("failed: ")) +
           finished.at(0).at(0).toString();
}

// C40 X1: the same call, the engine's exporter. A daemon on this machine is the easy case:
// its files are read where it says they are, and the row can say from where.
void TestAppTransmissionBackup::theExportIsTransmissionsOwn()
{
    fill(m_folder);
    App app;
    QVERIFY(!startOn(app).isEmpty());
    QCOMPARE(app.qbtEngine(), nullptr);
    QVERIFY(app.transmissionEngine() != nullptr);
    QCOMPARE(app.torrentFilesReach(), QString());
    QCOMPARE(app.torrentsExportSummary(),
             QStringLiteral("3 torrents with their folders, labels, limits and renames, in one "
                            "private file."));

    app.refreshTorrentFilesReach();
    QCOMPARE(app.torrentFilesReach(), QStringLiteral("asking"));
    QVERIFY(settled(app, QStringLiteral("readable")));
    QCOMPARE(app.torrentFilesDaemonFolder(), m_folder);
    QCOMPARE(app.torrentFilesFolder(), m_folder);
    QCOMPARE(app.torrentFilesRemembered(), QString());
    QCOMPARE(app.torrentFilesNote(), QString());

    const QString path = m_home->filePath(QStringLiteral("out.omaseed-torrents"));
    // The one without metadata is a link on any engine: the words every export has.
    QCOMPARE(exportTo(app, path),
             QStringLiteral("ok: Exported seedbox's 3 torrents to %1. 1 magnet went in as a link.")
                 .arg(path));
    QVERIFY(app.torrentsBackupRunning().isEmpty());
    QString error;
    const std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(path, &error);
    QVERIFY2(contents.has_value(), qPrintable(error));
    QCOMPARE(contents->manifest.engine, EngineKind::Transmission);
    QCOMPARE(contents->manifest.serverName, QStringLiteral("seedbox"));
    QCOMPARE(contents->manifest.count, 3);
    QCOMPARE(contents->manifest.magnets, 1);

    // Its import is the next step (C40 X9): refused in words, nothing read.
    QCOMPARE(app.readTorrentsImport(path), QStringLiteral("seedbox cannot import torrents."));
    QSignalSpy imported(&app, &App::torrentsImportFinished);
    app.applyTorrentsImport({});
    QCOMPARE(imported.size(), 1);
    QVERIFY(!imported.at(0).at(1).toBool());
    app.engine()->stop();
}

// C40 X3: a daemon whose disk this machine doesn't see. The row is told, and an export all the
// same writes each torrent as its link, saying why.
void TestAppTransmissionBackup::filesOutOfSightAreSaidAndGoInAsLinks()
{
    fill(kFar);
    App app;
    QVERIFY(!startOn(app).isEmpty());
    app.refreshTorrentFilesReach();
    QVERIFY(settled(app, QStringLiteral("unreadable")));
    QCOMPARE(app.torrentFilesDaemonFolder(), kFar);
    QCOMPARE(app.torrentFilesFolder(), kFar);

    const QString path = m_home->filePath(QStringLiteral("links.omaseed-torrents"));
    QCOMPARE(exportTo(app, path),
             QStringLiteral("ok: Exported seedbox's 3 torrents to %1. 3 went in as links: omaseed "
                            "couldn't read their .torrent files.")
                 .arg(path));
    const std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(path);
    QVERIFY(contents.has_value());
    QCOMPARE(contents->manifest.count, 3);
    QCOMPARE(contents->manifest.magnets, 3);
    app.engine()->stop();
}

// C40 X3: "their folder…". A folder that isn't theirs is said and not kept; one that is, is
// remembered in the server's own entry, read by the export, and gone with the server.
void TestAppTransmissionBackup::aFolderGivenIsCheckedAndRemembered()
{
    fill(kFar);
    App app;
    const QString id = startOn(app);
    QVERIFY(!id.isEmpty());
    app.refreshTorrentFilesReach();
    QVERIFY(settled(app, QStringLiteral("unreadable")));

    app.useTorrentFilesFolder(m_home->filePath(QStringLiteral("empty")));
    QCOMPARE(app.torrentFilesReach(), QStringLiteral("asking"));
    QVERIFY(settled(app, QStringLiteral("unreadable")));
    QCOMPARE(app.torrentFilesNote(),
             QStringLiteral("That folder doesn't hold seedbox's .torrent files."));
    QCOMPARE(app.torrentFilesRemembered(), QString());
    QCOMPARE(app.servers()->byId(id)->torrentsFolder, QString());

    app.useTorrentFilesFolder(m_folder);
    QVERIFY(settled(app, QStringLiteral("readable")));
    QCOMPARE(app.torrentFilesNote(), QString());
    QCOMPARE(app.torrentFilesRemembered(), m_folder);
    QCOMPARE(app.torrentFilesFolder(), m_folder);
    QCOMPARE(app.torrentFilesDaemonFolder(), kFar);
    QCOMPARE(app.servers()->byId(id)->torrentsFolder, m_folder);
    // On disk, in servers.json: what another run reads.
    {
        ServerStore again;
        QVERIFY(again.load());
        QCOMPARE(again.byId(id)->torrentsFolder, m_folder);
    }
    // Asked again (the pane shows again), it is still read from there.
    app.refreshTorrentFilesReach();
    QVERIFY(settled(app, QStringLiteral("readable")));
    QCOMPARE(app.torrentFilesFolder(), m_folder);

    const QString path = m_home->filePath(QStringLiteral("seen.omaseed-torrents"));
    QCOMPARE(exportTo(app, path),
             QStringLiteral("ok: Exported seedbox's 3 torrents to %1. 1 magnet went in as a link.")
                 .arg(path));
    QCOMPARE(TorrentsBackup::read(path)->manifest.magnets, 1);

    // Editing the server in its sheet keeps the folder; removing the server takes it along.
    QVERIFY(app.serverList()->update(QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QStringLiteral("renamed")},
        {QStringLiteral("url"), app.servers()->byId(id)->url.toString()},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    }));
    QCOMPARE(app.servers()->byId(id)->torrentsFolder, m_folder);
    app.engine()->stop();
    QVERIFY(app.servers()->remove(id));
    QFile saved(m_home->filePath(QStringLiteral("omaseed/servers.json")));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(!saved.readAll().contains("torrentsFolder"));
}

// C40 X3: a remembered folder that no longer holds them is the not-seen state again.
void TestAppTransmissionBackup::aRememberedFolderThatWentFallsBack()
{
    fill(kFar);
    App app;
    const QString id = startOn(app);
    QVERIFY(!id.isEmpty());
    app.useTorrentFilesFolder(m_folder);
    QVERIFY(settled(app, QStringLiteral("readable")));

    QVERIFY(QDir(m_folder).removeRecursively());
    app.refreshTorrentFilesReach();
    QVERIFY(settled(app, QStringLiteral("unreadable")));
    QCOMPARE(app.torrentFilesFolder(), kFar);
    app.engine()->stop();
}

// A qBittorrent sends its .torrent files itself: nothing to ask, nothing to say.
void TestAppTransmissionBackup::aQbittorrentAsksNothing()
{
    App app;
    app.loadDemo(4);
    app.refreshTorrentFilesReach();
    QCOMPARE(app.torrentFilesReach(), QString());
    QCOMPARE(app.transmissionEngine(), nullptr);
    QVERIFY(app.featuresMap().value(QStringLiteral("torrentsExport")).toBool());
    QVERIFY(app.featuresMap().value(QStringLiteral("torrentsImport")).toBool());
}

QTEST_MAIN(TestAppTransmissionBackup)
#include "tst_app_transmissionbackup.moc"
