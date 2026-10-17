// #33 (docs/24 §4–§5): App's side of a qBittorrent's torrents, between two qbittorrent-nox of
// the test's own (tests/ThrowawayQbt.hpp). The export says how many it wrote; the import sheet
// is told what is new, what is skipped and where the save paths go; the "!" line follows the
// rows; the import says what it did; the same file again skips everything (D8 a). The runs'
// own details are core's tst_torrentsbackup_live.
//
// Needs qbittorrent-nox, the swarm's tiny-notes.txt .torrent and its file in the :8081
// playground's folder (copied, never touched); skips without.

#include "core/SyncEngine.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/qbt/QbtClient.hpp"
#include "ui/app/App.hpp"

#include "LiveServer.hpp"
#include "ThrowawayQbt.hpp"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::app::App;
using omaseed::core::QbtClient;
using omaseed::core::Result;
using omaseed::core::SyncEngine;

namespace {

const QString kTiny = QStringLiteral("tiny-notes.txt");

template<class T>
Result<T> wait(omaseed::core::Reply<T> *reply, QObject *context)
{
    std::optional<Result<T>> got;
    reply->then(context, [&got](const Result<T> &r) { got = r; });
    if (!QTest::qWaitFor([&got] { return got.has_value(); }, 20000))
        return Result<T>(T());
    return got.value_or(Result<T>(T()));
}

bool connectApp(App &app, ThrowawayQbt &qbt)
{
    app.start();
    QSignalSpy synced(app.engine(), &SyncEngine::firstSyncFinished);
    const QString id = app.addServer({{QStringLiteral("name"), QStringLiteral("box")},
                                      {QStringLiteral("url"), qbt.url().toString()},
                                      {QStringLiteral("authMode"), QStringLiteral("none")}});
    return !id.isEmpty() && synced.wait(20000);
}

QVariantMap planRow(const QVariantMap &info, const QString &key)
{
    for (const QVariant &row : info.value(QStringLiteral("plan")).toList()) {
        if (row.toMap().value(QStringLiteral("key")).toString() == key)
            return row.toMap();
    }
    return {};
}

} // namespace

class TestAppTorrentsBackupLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        qunsetenv("OMASEED_SERVER_URL");
    }

    void init()
    {
        // The exporting App starts with no servers of its own.
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config-source")).toUtf8());
    }

    void exportsAndImports()
    {
        const QString torrentFile =
            qEnvironmentVariable("OMASEED_SWARM_PROFILE",
                                 QDir::homePath() + QStringLiteral("/.local/share/omaseed-swarm")) +
            QStringLiteral("/torrents/") + kTiny + QStringLiteral(".torrent");
        QFile torrent(torrentFile);
        if (!torrent.open(QIODevice::ReadOnly))
            QSKIP("no swarm .torrent files (scripts/dev/swarm.sh start && seed)");
        QbtClient playground;
        playground.setBaseUrl(LiveServer::playground());
        QString swarmDir;
        const QJsonArray infos = wait(playground.torrentsInfo(), this).value();
        for (const QJsonValue &v : infos) {
            if (v.toObject().value(QStringLiteral("name")).toString() == kTiny)
                swarmDir = v.toObject().value(QStringLiteral("save_path")).toString();
        }
        if (swarmDir.isEmpty() || !QFile::exists(swarmDir + QLatin1Char('/') + kTiny))
            QSKIP("the playground has no swarm files to copy");

        ThrowawayQbt source;
        if (!source.start(false))
            QSKIP(qPrintable(source.skipReason().isEmpty() ? source.error() : source.skipReason()));
        QbtClient a;
        a.setBaseUrl(source.url());
        QbtClient::AddOptions options;
        options.savepath = source.downloadsPath();
        options.dlLimit = 1;
        options.tags = {QStringLiteral("keep")};
        QVERIFY(wait(a.torrentsAddFile(kTiny, torrent.readAll(), options), this).isValid());

        const QString file = m_dir.filePath(QStringLiteral("box.omaseed-torrents"));
        {
            App app;
            QVERIFY(connectApp(app, source));
            QTRY_COMPARE_WITH_TIMEOUT(app.engine()->torrents()->count(), 1, 10000);
            QVERIFY2(app.torrentsExportSummary().startsWith(
                         QStringLiteral("1 torrent with its save path")),
                     qPrintable(app.torrentsExportSummary()));
            // docs/34 D2 a: the file it proposes names the server.
            QVERIFY2(app.torrentsExportSummary().endsWith(
                         QStringLiteral("Saved as torrents-box-%1.omaseed-torrents.")
                             .arg(QDate::currentDate().toString(Qt::ISODate))),
                     qPrintable(app.torrentsExportSummary()));
            QSignalSpy exported(&app, &App::torrentsExportFinished);
            app.exportTorrents(file);
            QCOMPARE(app.torrentsBackupRunning(), QStringLiteral("export"));
            QVERIFY(exported.wait(20000));
            QVERIFY2(exported.at(0).at(1).toBool(), qPrintable(exported.at(0).at(0).toString()));
            QVERIFY2(exported.at(0).at(0).toString().startsWith(
                         QStringLiteral("Exported box's 1 torrent to ")),
                     qPrintable(exported.at(0).at(0).toString()));
            QVERIFY(app.torrentsBackupRunning().isEmpty());
        }

        ThrowawayQbt target;
        QVERIFY(target.start(false));
        const QString data = m_dir.filePath(QStringLiteral("files"));
        QVERIFY(QDir().mkpath(data));
        QVERIFY(QFile::copy(swarmDir + QLatin1Char('/') + kTiny, data + QLatin1Char('/') + kTiny));
        // A second App, on the target alone.
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config-target")).toUtf8());
        App app;
        QVERIFY(connectApp(app, target));
        QSignalSpy read(&app, &App::torrentsImportRead);
        QCOMPARE(app.readTorrentsImport(file), QString());
        QVERIFY(read.wait(20000));
        QVariantMap info = read.at(0).at(0).toMap();
        QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
                 qPrintable(info.value(QStringLiteral("error")).toString()));
        QCOMPARE(info.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Import 1 torrent into box"));
        // docs/34 D2 a: whose export it is, from the file.
        const QString sub = info.value(QStringLiteral("sub")).toString();
        QVERIFY2(sub.startsWith(QStringLiteral("From box's export of ")) &&
                     sub.endsWith(QStringLiteral(", box.omaseed-torrents.")),
                 qPrintable(sub));
        QCOMPARE(planRow(info, QStringLiteral("new")).value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(planRow(info, QStringLiteral("present")).value(QStringLiteral("count")).toInt(),
                 0);
        QCOMPARE(info.value(QStringLiteral("importCount")).toInt(), 1);
        QCOMPARE(info.value(QStringLiteral("created")).toString(),
                 QStringLiteral("1 tag they use is created first"));
        // No path map anywhere: one row from the file's default save path, its "to" to fill.
        const QVariantList paths = info.value(QStringLiteral("paths")).toList();
        QCOMPARE(paths.size(), 1);
        QCOMPARE(paths.at(0).toMap().value(QStringLiteral("from")).toString(),
                 source.downloadsPath());
        QVERIFY(paths.at(0).toMap().value(QStringLiteral("to")).toString().isEmpty());
        QCOMPARE(app.torrentsImportPathsNote(paths),
                 QStringLiteral("save paths are kept as they are in the file"));
        const QVariantList rows{QVariantMap{{QStringLiteral("from"), source.downloadsPath()},
                                            {QStringLiteral("to"), data}}};
        QCOMPARE(app.torrentsImportPathsNote(rows), QString());
        const QVariantList elsewhere{QVariantMap{
            {QStringLiteral("from"), QStringLiteral("/nowhere")}, {QStringLiteral("to"), data}}};
        QCOMPARE(app.torrentsImportPathsNote(elsewhere),
                 QStringLiteral("1 save path doesn't start with /nowhere and is kept as it is"));

        QSignalSpy imported(&app, &App::torrentsImportFinished);
        app.applyTorrentsImport({{QStringLiteral("rows"), rows}, {QStringLiteral("check"), true}});
        QCOMPARE(app.torrentsBackupRunning(), QStringLiteral("import"));
        QVERIFY(imported.wait(60000));
        QVERIFY2(imported.at(0).at(1).toBool(), qPrintable(imported.at(0).at(0).toString()));
        QCOMPARE(imported.at(0).at(0).toString(),
                 QStringLiteral("Imported into box: 1 torrent. After the check: 1 started."));
        QbtClient b;
        b.setBaseUrl(target.url());
        const QJsonArray there = wait(b.torrentsInfo(), this).value();
        QCOMPARE(there.size(), 1);
        QCOMPARE(there.at(0).toObject().value(QStringLiteral("save_path")).toString(), data);
        QCOMPARE(there.at(0).toObject().value(QStringLiteral("tags")).toString(),
                 QStringLiteral("keep"));

        // D8 a: the same file again: already here, skipped.
        read.clear();
        QCOMPARE(app.readTorrentsImport(file), QString());
        QVERIFY(read.wait(20000));
        info = read.at(0).at(0).toMap();
        QCOMPARE(planRow(info, QStringLiteral("present")).value(QStringLiteral("count")).toInt(),
                 1);
        QCOMPARE(info.value(QStringLiteral("importCount")).toInt(), 0);
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestAppTorrentsBackupLive)
#include "tst_app_torrentsbackup_live.moc"
