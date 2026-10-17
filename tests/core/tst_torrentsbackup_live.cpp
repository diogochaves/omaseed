// TorrentsExport and TorrentsImport against real qBittorrents (docs/24 §8 "checks"; #33).
//
// The export of the :8081 playground only reads it. The round trips run between
// qbittorrent-nox of the test's own (tests/ThrowawayQbt.hpp): two of the swarm's torrents and a
// magnet on one, with a category, tags, a renamed file, a skipped file and a share limit,
// exported, then imported into another whose files sit elsewhere (a "from → to" row), checked
// and compared there. Then the same import cut off halfway by the server going away, and run
// again; and one where a renamed file's old name is a stray file on disk (#64, #67).
//
// The round trips need qbittorrent-nox, the swarm's .torrent files (scripts/dev/swarm.sh seed)
// and their data in the playground's folder, copied (never touched); they skip without.

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/TorrentsExport.hpp"
#include "core/TorrentsImport.hpp"
#include "core/engines/qbt/QbtClient.hpp"

#include "LiveServer.hpp"
#include "ThrowawayQbt.hpp"

#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::QbtClient;
using omaseed::core::Reply;
using omaseed::core::Result;
using omaseed::core::TorrentFile;
using omaseed::core::TorrentRecord;
using omaseed::core::TorrentsBackup;
using omaseed::core::TorrentsExport;
using omaseed::core::TorrentsImport;
using omaseed::core::TorrentsInfoQuery;

namespace {

template<class T>
Result<T> wait(Reply<T> *reply, QObject *context)
{
    std::optional<Result<T>> got;
    reply->then(context, [&got](const Result<T> &r) { got = r; });
    if (!QTest::qWaitFor([&got] { return got.has_value(); }, 20000))
        return Result<T>(omaseed::core::ApiError(omaseed::core::ApiError::Kind::Timeout,
                                                 QStringLiteral("no answer")));
    return *got;
}

TorrentsExport::Outcome exportFrom(QbtClient &client, const QString &path)
{
    TorrentsExport exporter(&client);
    QSignalSpy finished(&exporter, &TorrentsExport::finished);
    exporter.start(
        path, QStringLiteral("source"), client.baseUrl().toString(), QStringLiteral("test"));
    if (!finished.wait(60000))
        return {};
    return finished.at(0).at(0).value<TorrentsExport::Outcome>();
}

/// What the server has, as the plan reads it.
TorrentsImport::Here hereOf(QbtClient &client, QObject *context)
{
    TorrentsImport::Here here;
    const QJsonArray infos = wait(client.torrentsInfo(), context).value();
    for (const QJsonValue &v : infos) {
        const QJsonObject t = v.toObject();
        for (const char *key : {"hash", "infohash_v1", "infohash_v2"}) {
            const QString id = t.value(QLatin1String(key)).toString().toLower();
            if (!id.isEmpty())
                here.ids.insert(id);
        }
    }
    const QJsonObject categories = wait(client.torrentsCategories(), context).value();
    here.categories = categories.keys();
    const QJsonArray tags = wait(client.torrentsTags(), context).value();
    for (const QJsonValue &tag : tags)
        here.tags.append(tag.toString());
    return here;
}

QJsonObject torrentNamed(QbtClient &client, QObject *context, const QString &name)
{
    const QJsonArray infos = wait(client.torrentsInfo(), context).value();
    for (const QJsonValue &v : infos) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name)
            return v.toObject();
    }
    return {};
}

bool copyTree(const QString &from, const QString &to)
{
    const QFileInfo info(from);
    if (info.isFile())
        return QFile::copy(from, to);
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + file.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target))
            return false;
    }
    return true;
}

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

const QString kTiny = QStringLiteral("tiny-notes.txt");
const QString kAlbum = QStringLiteral("Field Recordings Vol. 3 [FLAC]");
const QString kTrack = QStringLiteral("01 - Harbour at Dawn.flac");
const QString kRenamed = QStringLiteral("01 - Harbour.flac");
const QString kMagnetHash = QStringLiteral("dddddddddddddddddddddddddddddddddddddddd");

} // namespace

class TestTorrentsBackupLive : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_export;          ///< the file the source made, for the later tests
    QString m_playgroundSwarm; ///< where the playground keeps the swarm's files
    QString m_swarmTorrents;
    std::unique_ptr<ThrowawayQbt> m_source;

    bool skipRoundTrips(QString *why)
    {
        if (!m_source) {
            *why = QStringLiteral("no source server");
            return true;
        }
        return false;
    }

    /// A folder holding copies of the two torrents' files, the album's first track already
    /// under its renamed name.
    QString dataCopy(const QString &name)
    {
        const QString dir = m_dir.filePath(name);
        QDir().mkpath(dir);
        if (!copyTree(m_playgroundSwarm + QLatin1Char('/') + kTiny,
                      dir + QLatin1Char('/') + kTiny) ||
            !copyTree(m_playgroundSwarm + QLatin1Char('/') + kAlbum,
                      dir + QLatin1Char('/') + kAlbum))
            return {};
        const QString album = dir + QLatin1Char('/') + kAlbum + QLatin1Char('/');
        if (!QFile::rename(album + kTrack, album + kRenamed))
            return {};
        return dir;
    }

    TorrentsImport::Outcome
    runImport(QbtClient &client,
              const QString &data,
              const QString &journal,
              std::function<void(TorrentsImport &, const QString &, int)> onProgress = {})
    {
        QString error;
        const auto contents = TorrentsBackup::read(m_export, &error);
        if (!contents) {
            qWarning("%s", qPrintable(error));
            return {};
        }
        const TorrentsImport::Plan plan = TorrentsImport::plan(
            *contents, hereOf(client, this), TorrentsImport::Journal::load(journal));
        TorrentsImport import(&client);
        TorrentsImport::Options options;
        options.rows = {{m_source->downloadsPath(), data, {}}};
        options.journalPath = journal;
        options.localPathFor = [](const QString &path) { return path; }; // a local server
        options.pollMs = 300;
        options.batch = 1;
        if (onProgress)
            connect(&import,
                    &TorrentsImport::progress,
                    this,
                    [&import, onProgress](const QString &phase, int done, int) {
                        onProgress(import, phase, done);
                    });
        QSignalSpy finished(&import, &TorrentsImport::finished);
        import.start(*contents, plan, options);
        if (!finished.wait(90000))
            return {};
        return finished.at(0).at(0).value<TorrentsImport::Outcome>();
    }

    /// The round trip's result, read on the target: what docs/24 §8 compares.
    void compareTarget(QbtClient &client, const QString &data)
    {
        // qBittorrent shows a start in torrents/info a moment after it answers it.
        const auto running = [&client, this](const QString &name) {
            const QString state = torrentNamed(client, this, name).value(u"state").toString();
            return !state.isEmpty() && !state.startsWith(u"stopped");
        };
        QTRY_VERIFY_WITH_TIMEOUT(running(kTiny), 10000); // complete: started
        QTRY_VERIFY_WITH_TIMEOUT(running(kAlbum), 10000);
        const QJsonObject tiny = torrentNamed(client, this, kTiny);
        QVERIFY(!tiny.isEmpty());
        QCOMPARE(tiny.value(QStringLiteral("save_path")).toString(), data);
        QCOMPARE(tiny.value(QStringLiteral("category")).toString(), QStringLiteral("notes"));
        QCOMPARE(tiny.value(QStringLiteral("tags")).toString(), QStringLiteral("keep"));
        QCOMPARE(tiny.value(QStringLiteral("progress")).toDouble(), 1.0);

        const QJsonObject album = torrentNamed(client, this, kAlbum);
        QVERIFY(!album.isEmpty());
        const QString id = album.value(QStringLiteral("hash")).toString();
        QCOMPARE(album.value(QStringLiteral("save_path")).toString(), data);
        QCOMPARE(album.value(QStringLiteral("ratio_limit")).toDouble(), 2.0);
        QCOMPARE(album.value(QStringLiteral("progress")).toDouble(), 1.0);
        const QJsonArray files = wait(client.torrentsFiles(id), this).value();
        QCOMPARE(files.at(0).toObject().value(QStringLiteral("name")).toString(),
                 kAlbum + QLatin1Char('/') + kRenamed);
        bool skippedCover = false;
        for (const QJsonValue &f : files) {
            if (f.toObject().value(QStringLiteral("name")).toString().endsWith(u"cover.jpg"))
                skippedCover = f.toObject().value(QStringLiteral("priority")).toInt() == 0;
        }
        QVERIFY(skippedCover);

        const QJsonObject magnet = torrentNamed(client, this, QStringLiteral("link only"));
        QVERIFY(!magnet.isEmpty());
        QVERIFY(magnet.value(QStringLiteral("state")).toString().startsWith(u"stopped"));

        const QJsonObject categories = wait(client.torrentsCategories(), this).value();
        QCOMPARE(categories.value(QStringLiteral("notes")).toObject().value(u"savePath").toString(),
                 data + QStringLiteral("/notes"));
        // The files on disk are the copies, untouched by the check.
        QCOMPARE(
            QFileInfo(data + QLatin1Char('/') + kAlbum + QLatin1Char('/') + kRenamed).size(),
            QFileInfo(m_playgroundSwarm + QLatin1Char('/') + kAlbum + QLatin1Char('/') + kTrack)
                .size());
    }

private Q_SLOTS:
    // docs/24 §8: the export reads the playground: every torrent in, the ones without
    // metadata as links, the file private.
    void exportsThePlayground()
    {
        QbtClient client;
        client.setBaseUrl(LiveServer::playground());
        const auto info = wait(client.torrentsInfo(), this);
        if (info.isError())
            QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");
        const QString path = m_dir.filePath(QStringLiteral("playground.omaseed-torrents"));
        const TorrentsExport::Outcome outcome = exportFrom(client, path);
        QVERIFY2(outcome.ok, qPrintable(outcome.error));
        QCOMPARE(outcome.torrents, int(info.value().size()));
        int magnets = 0;
        for (const QJsonValue &v : info.value()) {
            if (!v.toObject().value(QStringLiteral("has_metadata")).toBool())
                ++magnets;
            const QJsonObject t = v.toObject();
            if (t.value(QStringLiteral("name")).toString() == kTiny)
                m_playgroundSwarm = t.value(QStringLiteral("save_path")).toString();
        }
        QCOMPARE(outcome.magnets, magnets);
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());

        QString error;
        const auto contents = TorrentsBackup::read(path, &error);
        QVERIFY2(contents.has_value(), qPrintable(error));
        QCOMPARE(contents->records.size(), int(info.value().size()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        for (const TorrentRecord &r : contents->records) {
            if (r.isMagnet())
                continue;
            const TorrentFile parsed =
                TorrentFile::parse(TorrentsBackup::torrentBytes(&file, *contents, r.id));
            QVERIFY(parsed.isValid());
            QCOMPARE(parsed.torrentId(), r.id);
        }
    }

    // The source: two swarm torrents and a magnet, with what docs/24 §2 says goes along.
    void makesTheSource()
    {
        m_swarmTorrents =
            qEnvironmentVariable("OMASEED_SWARM_PROFILE",
                                 QDir::homePath() + QStringLiteral("/.local/share/omaseed-swarm")) +
            QStringLiteral("/torrents");
        const QString tinyFile =
            m_swarmTorrents + QLatin1Char('/') + kTiny + QStringLiteral(".torrent");
        const QString albumFile =
            m_swarmTorrents + QLatin1Char('/') + kAlbum + QStringLiteral(".torrent");
        if (!QFile::exists(tinyFile) || !QFile::exists(albumFile))
            QSKIP("no swarm .torrent files (scripts/dev/swarm.sh start && seed)");
        if (m_playgroundSwarm.isEmpty() ||
            !QFileInfo::exists(m_playgroundSwarm + QLatin1Char('/') + kAlbum))
            QSKIP("the playground has no swarm files to copy");
        m_source = std::make_unique<ThrowawayQbt>();
        if (!m_source->start(false)) {
            const QString why = m_source->skipReason();
            m_source.reset();
            if (!why.isEmpty())
                QSKIP(qPrintable(why));
            QFAIL("the source qBittorrent did not start");
        }
        QbtClient a;
        a.setBaseUrl(ThrowawayQbt::urlOn(m_source->webPort()));
        QVERIFY(wait(a.torrentsCreateCategory(QStringLiteral("notes"),
                                              m_source->downloadsPath() + QStringLiteral("/notes")),
                     this)
                    .isValid());
        const auto read = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        QbtClient::AddOptions o;
        o.savepath = m_source->downloadsPath();
        o.dlLimit = 1; // it has no files here and must not fetch any
        o.category = QStringLiteral("notes");
        o.tags = {QStringLiteral("keep")};
        QVERIFY(wait(a.torrentsAddFile(kTiny, read(tinyFile), o), this).isValid());
        o.category.reset();
        o.tags = {QStringLiteral("flac"), QStringLiteral("field")};
        QVERIFY(wait(a.torrentsAddFile(kAlbum, read(albumFile), o), this).isValid());
        QbtClient::AddOptions stopped;
        stopped.stopped = true;
        stopped.savepath = m_source->downloadsPath();
        QVERIFY(wait(a.torrentsAddUrls({QStringLiteral("magnet:?xt=urn:btih:") + kMagnetHash +
                                        QStringLiteral("&dn=link%20only")},
                                       stopped),
                     this)
                    .isValid());
        QTRY_VERIFY_WITH_TIMEOUT(!torrentNamed(a, this, kAlbum).isEmpty(), 10000);
        const QString album =
            torrentNamed(a, this, kAlbum).value(QStringLiteral("hash")).toString();
        QVERIFY(wait(a.torrentsRenameFile(album,
                                          kAlbum + QLatin1Char('/') + kTrack,
                                          kAlbum + QLatin1Char('/') + kRenamed),
                     this)
                    .isValid());
        const QJsonArray files = wait(a.torrentsFiles(album), this).value();
        for (const QJsonValue &f : files) {
            if (f.toObject().value(QStringLiteral("name")).toString().endsWith(u"cover.jpg"))
                QVERIFY(
                    wait(a.torrentsSetFilePrio(album, {f.toObject().value(u"index").toInt()}, 0),
                         this)
                        .isValid());
        }
        QVERIFY(wait(a.torrentsSetShareLimits({album}, 2.0, -2, -2), this).isValid());

        m_export = m_dir.filePath(QStringLiteral("source.omaseed-torrents"));
        const TorrentsExport::Outcome outcome = exportFrom(a, m_export);
        QVERIFY2(outcome.ok, qPrintable(outcome.error));
        QCOMPARE(outcome.torrents, 3);
        QCOMPARE(outcome.magnets, 1);
    }

    // docs/24 §5, D6 a, D7 a: into an empty server whose files are elsewhere; everything comes
    // back, the complete ones started.
    void roundTrip()
    {
        QString why;
        if (skipRoundTrips(&why))
            QSKIP(qPrintable(why));
        ThrowawayQbt target;
        QVERIFY(target.start(false));
        QbtClient b;
        b.setBaseUrl(ThrowawayQbt::urlOn(target.webPort()));
        const QString data = dataCopy(QStringLiteral("round-trip"));
        QVERIFY(!data.isEmpty());
        const QString journal = m_dir.filePath(QStringLiteral("journal-round.json"));
        const TorrentsImport::Outcome outcome = runImport(b, data, journal);
        QVERIFY2(outcome.complete, qPrintable(outcome.errors.join(u"; ")));
        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
        QCOMPARE(outcome.added, 2);
        QCOMPARE(outcome.magnets, 1);
        QCOMPARE(outcome.started, 2);
        QCOMPARE(outcome.categories, 1);
        compareTarget(b, data);
        QVERIFY(!QFile::exists(journal));

        // D8 a: the same file again adds nothing.
        const TorrentsImport::Outcome again = runImport(b, data, journal);
        QVERIFY(again.complete);
        QCOMPARE(again.added + again.magnets + again.finished, 0);
    }

    // §7: the server goes away halfway; what was added stays, and the next run finishes it.
    void resumesAfterAFailure()
    {
        QString why;
        if (skipRoundTrips(&why))
            QSKIP(qPrintable(why));
        ThrowawayQbt target;
        QVERIFY(target.start(false));
        QbtClient c;
        c.setBaseUrl(ThrowawayQbt::urlOn(target.webPort()));
        c.setMaxRetries(0);
        const QString data = dataCopy(QStringLiteral("resume"));
        QVERIFY(!data.isEmpty());
        const QString journal = m_dir.filePath(QStringLiteral("journal-resume.json"));
        bool cut = false;
        const TorrentsImport::Outcome first = runImport(
            c, data, journal, [&cut, &target](TorrentsImport &, const QString &phase, int done) {
                if (phase == QLatin1String("adding") && done >= 1 && !cut) {
                    cut = true;
                    target.stop();
                }
            });
        QVERIFY(cut);
        QVERIFY(!first.complete);
        QVERIFY(!first.errors.isEmpty());
        const TorrentsImport::Journal left = TorrentsImport::Journal::load(journal);
        QVERIFY(!left.stages.isEmpty());

        QVERIFY(target.restart());
        c.setMaxRetries(3);
        QTRY_VERIFY_WITH_TIMEOUT(wait(c.torrentsInfo(), this).isValid(), 10000);
        const auto contents = TorrentsBackup::read(m_export);
        QVERIFY(contents.has_value());
        const TorrentsImport::Plan plan = TorrentsImport::plan(
            *contents, hereOf(c, this), TorrentsImport::Journal::load(journal));
        QVERIFY(!plan.finish.isEmpty());
        QCOMPARE(plan.count(), 3);
        const TorrentsImport::Outcome second = runImport(c, data, journal);
        QVERIFY2(second.complete, qPrintable(second.errors.join(u"; ")));
        QVERIFY2(second.errors.isEmpty(), qPrintable(second.errors.join(u"; ")));
        QVERIFY(second.finished >= 1);
        compareTarget(c, data);
        QVERIFY(!QFile::exists(journal));
    }

    // #64, #67: the album's first track under its old name is a stray file here. Renaming onto
    // the real one would replace it; the rename is not sent and the album stays stopped.
    void neverRenamesOverAStrayFile()
    {
        QString why;
        if (skipRoundTrips(&why))
            QSKIP(qPrintable(why));
        ThrowawayQbt target;
        QVERIFY(target.start(false));
        QbtClient e;
        e.setBaseUrl(ThrowawayQbt::urlOn(target.webPort()));
        const QString data = dataCopy(QStringLiteral("stray"));
        QVERIFY(!data.isEmpty());
        const QString album = data + QLatin1Char('/') + kAlbum + QLatin1Char('/');
        {
            QFile stray(album + kTrack);
            QVERIFY(stray.open(QIODevice::WriteOnly));
            stray.write("a stray file, not the album's");
        }
        const qint64 realSize = QFileInfo(album + kRenamed).size();
        const TorrentsImport::Outcome outcome =
            runImport(e, data, m_dir.filePath(QStringLiteral("journal-stray.json")));
        QVERIFY(outcome.complete);
        QCOMPARE(outcome.refused, QStringList{kAlbum});
        QCOMPARE(readAll(album + kTrack), QStringLiteral("a stray file, not the album's"));
        QCOMPARE(QFileInfo(album + kRenamed).size(), realSize);
        const QJsonObject t = torrentNamed(e, this, kAlbum);
        QVERIFY(t.value(QStringLiteral("state")).toString().startsWith(u"stopped"));
        const QJsonArray files =
            wait(e.torrentsFiles(t.value(QStringLiteral("hash")).toString()), this).value();
        QCOMPARE(files.at(0).toObject().value(QStringLiteral("name")).toString(),
                 kAlbum + QLatin1Char('/') + kTrack);
    }
};

QTEST_GUILESS_MAIN(TestTorrentsBackupLive)
#include "tst_torrentsbackup_live.moc"
