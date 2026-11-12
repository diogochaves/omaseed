// DelugeTranslate (#87, E6): Deluge 2.2's answers through deluge-web in omaseed's terms, read
// from answers recorded off a real daemon (tests/fixtures/deluge/,
// scripts/dev/deluge-fixtures.py): the poll's parts, states, one torrent's every list key, the
// details' tabs, the Label plugin's labels in the categories place (C25 B), the server's
// figures and settings, a prefetch's metadata, the feature record, and the way out.

#include "core/PieceStates.hpp"
#include "core/engines/deluge/DelugeTranslate.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
namespace DT = omaseed::core::DelugeTranslate;

namespace {

const QString kPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kSingle = QStringLiteral("ed6dc254e7756302401a59491e69f6a53ac157be");
const QString kError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
const QString kStream = QStringLiteral("a8701869a74f56c5c3a16f4673c037a682619afb");
const QString kQueued = QStringLiteral("6f2336946faaaa1532ae3e8b3216a00bb08e55be");

QJsonValue result(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/deluge/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("result"));
}

/// One recorded torrent, every key the recorder asked for, its hash put in.
QJsonObject recorded(const QString &hash)
{
    QJsonObject row = result("core_get_torrents_status.json").toObject().value(hash).toObject();
    if (row.isEmpty())
        qFatal("no recorded torrent %s", qPrintable(hash));
    row.insert(QStringLiteral("hash"), hash);
    return row;
}

QByteArray bencoded(const QByteArray &text)
{
    return QByteArray::number(text.size()) + ':' + text;
}

} // namespace

class TestDelugeTranslate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void thePollIsTakenApart();
    void statesBecomePhasesAndFlags();
    void aTorrentReadsEveryListKey();
    void aLabelIsReadOnlyWithThePlugin();
    void unfinishedTorrentsGoOnToTheirMoveFolder();
    void theContentRootIsTheTopFolderOrTheOneFile();
    void stoppedGoesByTheStateWord();
    void trackerUrlsForTheRail();
    void serverStateFromTheStatsAndTheSession();
    void limitsAndPreferences();
    void preferencesGoBackAsConfig();
    void theGeneralTab();
    void theFilesTab();
    void filePrioritiesBothWays();
    void theTrackersTabSaysTheOneInUse();
    void thePeersTab();
    void piecesAsStates();
    void labelsAndTheirFolders();
    void labelNamesAreDeluges();
    void aPrefetchGivesTheFiles();
    void anEmptyPrefetchIsStillLooking();
    void featuresAreDeluges();
    void trackersAreEditedWhole();
    void speedsLeaveInKibibytes();
};

void TestDelugeTranslate::thePollIsTakenApart()
{
    const DT::Poll poll = DT::poll(result("web_update_ui.json"));
    QVERIFY(poll.connected);
    QCOMPARE(poll.torrents.size(), 6);
    for (const QJsonObject &row : poll.torrents)
        QCOMPARE(row.value(QStringLiteral("hash")).toString().size(), 40);
    QVERIFY(poll.labelPlugin);
    // Every label, those on no torrent too; "" and "All" are counts, not labels.
    QCOMPARE(poll.labels, QStringList({"empty", "fixtures"}));
    QCOMPARE(poll.stats.value(QStringLiteral("num_connections")).toInt(), 1);

    const DT::Poll off = DT::poll(result("web_update_ui_no_label.json"));
    QVERIFY(off.connected);
    QVERIFY(!off.labelPlugin);
    QVERIFY(off.labels.isEmpty());

    const DT::Poll none = DT::poll(result("web_update_ui_disconnected.json"));
    QVERIFY(!none.connected);
    QVERIFY(none.torrents.isEmpty());
}

void TestDelugeTranslate::statesBecomePhasesAndFlags()
{
    using P = TorrentPhase;
    using F = TorrentFlag;
    const TorrentStatus pack = DT::status(recorded(kPack));
    QCOMPARE(pack.phase, P::Seeding);
    QVERIFY(pack.has(F::Finished) && pack.has(F::Stalled) && !pack.has(F::Forced));
    QCOMPARE(pack.word, QStringLiteral("Seeding"));

    QCOMPARE(DT::status(recorded(kSingle)).phase, P::Stopped);
    const TorrentStatus error = DT::status(recorded(kError));
    QCOMPARE(error.phase, P::Errored);
    QVERIFY(error.has(F::Finished));

    // A magnet without its metadata, started past the queue (auto_managed off).
    const TorrentStatus magnet = DT::status(recorded(kMagnet));
    QCOMPARE(magnet.phase, P::Metadata);
    QVERIFY(magnet.has(F::Forced) && magnet.has(F::Stalled));

    const TorrentStatus stream = DT::status(recorded(kStream));
    QCOMPARE(stream.phase, P::Downloading);
    QVERIFY(stream.has(F::Forced) && !stream.has(F::Stalled) && !stream.has(F::Finished));

    const TorrentStatus queued = DT::status(recorded(kQueued));
    QCOMPARE(queued.phase, P::Downloading);
    QVERIFY(queued.has(F::Queued) && !queued.has(F::Forced));

    // The rest of Deluge's words (engine matrix §4.2; the filter tree lists them).
    const auto of = [](const char *state, bool finished) {
        return DT::status({{QStringLiteral("state"), QString::fromLatin1(state)},
                           {QStringLiteral("is_finished"), finished},
                           {QStringLiteral("num_pieces"), 4}});
    };
    QCOMPARE(of("Moving", true).phase, P::Moving);
    QVERIFY(of("Moving", true).has(F::Finished));
    QCOMPARE(of("Checking", false).phase, P::Checking);
    QCOMPARE(of("Allocating", false).phase, P::Allocating);
    QCOMPARE(of("Queued", true).phase, P::Seeding);
    QVERIFY(of("Queued", true).has(F::Queued));
    QCOMPARE(of("Something new", false).phase, P::Unknown);
    QCOMPARE(DT::status({}).phase, P::Unknown);
}

void TestDelugeTranslate::aTorrentReadsEveryListKey()
{
    using F = TorrentField;
    DT::Context context;
    context.labelPlugin = true;
    context.nowSecs = 1793960200;
    context.contentRoot = QStringLiteral("Fixture Pack");

    const TorrentPatch pack = DT::torrent(recorded(kPack), context);
    QCOMPARE(pack.hash, kPack);
    QCOMPARE(pack.values.name, QStringLiteral("Fixture Pack"));
    QCOMPARE(pack.values.size, 64440);
    QCOMPARE(pack.values.totalSize, 64440);
    QCOMPARE(pack.values.progress, 1.0);
    QCOMPARE(pack.values.category, QStringLiteral("fixtures"));
    QVERIFY(!pack.has(F::Tags)); // no tags on Deluge
    // Finished: where it is now is its folder, and nothing waits anywhere else.
    QCOMPARE(pack.values.savePath, QStringLiteral("/srv/deluge/fixtures-recording/data"));
    QCOMPARE(pack.values.downloadPath, QString());
    QCOMPARE(pack.values.contentPath,
             QStringLiteral("/srv/deluge/fixtures-recording/data/Fixture Pack"));
    QCOMPARE(pack.values.addedOn, QDateTime::fromSecsSinceEpoch(1793960151));
    QCOMPARE(pack.values.completionOn, QDateTime::fromSecsSinceEpoch(1793960151));
    QVERIFY(!pack.values.lastActivity.isValid()); // never transferred
    QCOMPARE(pack.values.ratio, 0.0);
    QCOMPARE(pack.values.priority, 0);    // out of the queue
    QVERIFY(pack.values.hasTrackerError); // "Error: Connection refused"
    QVERIFY(!pack.values.eta);
    QCOMPARE(pack.values.dlLimit, 0);
    QCOMPARE(pack.values.ratioLimit, RatioLimit::unlimited());
    QCOMPARE(pack.values.shareLimitAction, QStringLiteral("Stop"));
    QCOMPARE(pack.values.infohashV1, kPack);
    QCOMPARE(pack.values.errorMessage, QString());
    QVERIFY(pack.has(F::IsPrivate) && !pack.values.isPrivate);
    QVERIFY(!pack.has(F::SeedingTimeLimit) && !pack.has(F::InactiveSeedingTimeLimit));
    QVERIFY(!pack.has(F::AutoTmm) && !pack.has(F::MagnetUri) && !pack.has(F::InfohashV2));

    // Its own limits: 250 KiB/s down, stop at 1.5 and remove keeping the files, sequential.
    const TorrentPatch single = DT::torrent(recorded(kSingle), context);
    QCOMPARE(single.values.dlLimit, 250 * 1024);
    QCOMPARE(single.values.upLimit, 0);
    QCOMPARE(single.values.ratioLimit, RatioLimit::of(1.5));
    QCOMPARE(single.values.shareLimitAction, QStringLiteral("Remove"));
    QVERIFY(single.values.seqDl);
    QCOMPARE(single.values.priority, 1); // queue 0 is the first place
    QCOMPARE(single.values.progress, 0.0);
    QCOMPARE(single.values.ratio, 0.0); // -1: nothing to divide by yet

    const TorrentPatch error = DT::torrent(recorded(kError), context);
    QCOMPARE(error.values.errorMessage,
             QStringLiteral("Failed to move download folder: Permission denied"));

    // Without metadata: no size yet, nothing said about privacy.
    const TorrentPatch magnet = DT::torrent(recorded(kMagnet), DT::Context{true, 1, {}});
    QCOMPARE(magnet.values.totalSize, -1);
    QVERIFY(!magnet.has(F::IsPrivate));
    QVERIFY(!magnet.has(F::ContentPath)); // where its content starts isn't known yet

    // Downloading from a real peer.
    const TorrentPatch stream = DT::torrent(recorded(kStream), context);
    QCOMPARE(stream.values.eta, std::optional<qint64>(127));
    QCOMPARE(stream.values.numSeeds, 1);
    QVERIFY(stream.values.dlspeed > 0);
    QVERIFY(stream.values.progress > 0.05 && stream.values.progress < 0.06);
    QCOMPARE(stream.values.availability, 1.0520000457763672);
    QCOMPARE(stream.values.lastActivity, QDateTime::fromSecsSinceEpoch(1793960200)); // 0 s ago
    QVERIFY(!stream.has(F::NumComplete)); // -1: no tracker said
}

void TestDelugeTranslate::aLabelIsReadOnlyWithThePlugin()
{
    // With the plugin off, deluge-web may still send a stale label: it isn't one.
    DT::Context off;
    off.labelPlugin = false;
    const TorrentPatch pack = DT::torrent(recorded(kPack), off);
    QVERIFY(pack.has(TorrentField::Category));
    QCOMPARE(pack.values.category, QString());
}

void TestDelugeTranslate::unfinishedTorrentsGoOnToTheirMoveFolder()
{
    // Deluge's inverse model (matrix §4.3): it downloads where it is, and moves when done.
    QJsonObject row = recorded(kSingle);
    row.insert(QStringLiteral("move_completed"), true);
    row.insert(QStringLiteral("move_completed_path"), QStringLiteral("/srv/finished"));
    const TorrentPatch going = DT::torrent(row, DT::Context{false, 1, QStringLiteral("x.bin")});
    QCOMPARE(going.values.savePath, QStringLiteral("/srv/finished"));
    QCOMPARE(going.values.downloadPath, QStringLiteral("/srv/deluge/fixtures-recording/empty"));
    QCOMPARE(going.values.contentPath,
             QStringLiteral("/srv/deluge/fixtures-recording/empty/x.bin"));
}

void TestDelugeTranslate::theContentRootIsTheTopFolderOrTheOneFile()
{
    QCOMPARE(DT::contentRoot(recorded(kPack)), QStringLiteral("Fixture Pack"));
    QCOMPARE(DT::contentRoot(recorded(kSingle)), QStringLiteral("fixture-single.bin"));
    QCOMPARE(DT::contentRoot(recorded(kMagnet)), QString());
    // Files in no one folder (a renamed layout): no single root.
    const QJsonObject loose{
        {QStringLiteral("files"),
         QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("a/x")}},
                    QJsonObject{{QStringLiteral("path"), QStringLiteral("b/y")}}}}};
    QCOMPARE(DT::contentRoot(loose), QString());
}

void TestDelugeTranslate::stoppedGoesByTheStateWord()
{
    QVERIFY(DT::isStopped(recorded(kSingle)));
    QVERIFY(DT::isStopped(recorded(kError)));
    QVERIFY(!DT::isStopped(recorded(kPack)));
    // libtorrent pauses a queued torrent too: it isn't stopped.
    QVERIFY(recorded(kQueued).value(QStringLiteral("paused")).toBool());
    QVERIFY(!DT::isStopped(recorded(kQueued)));
    // A failed move after a resume: not paused, still in error, so still stopped (K24).
    const QJsonObject resumed = result("core_get_torrent_status_error_resumed.json").toObject();
    QVERIFY(!resumed.value(QStringLiteral("paused")).toBool());
    QVERIFY(DT::isStopped(resumed));
    // …but not paused: C20's errored-and-stopped read asks that, so it runs in error (#244).
    QVERIFY(!DT::isPaused(resumed));
    QVERIFY(DT::isPaused(recorded(kError)));
    QVERIFY(DT::isPaused(recorded(kSingle)));
    QVERIFY(!DT::isPaused(recorded(kQueued)));
    QVERIFY(!DT::isPaused(recorded(kPack)));
}

void TestDelugeTranslate::trackerUrlsForTheRail()
{
    QCOMPARE(DT::trackerUrls(recorded(kPack)),
             QStringList({"udp://127.0.0.1:9/announce", "http://127.0.0.1:9/announce"}));
    QVERIFY(DT::trackerUrls(recorded(kStream)).isEmpty());
    // Without `trackers`, the one in use.
    QCOMPARE(DT::trackerUrls({{QStringLiteral("tracker"), QStringLiteral("http://t/announce")}}),
             QStringList({"http://t/announce"}));
}

void TestDelugeTranslate::serverStateFromTheStatsAndTheSession()
{
    const DT::Poll poll = DT::poll(result("web_update_ui.json"));
    const ServerStatePatch s =
        DT::serverState(poll.stats, result("core_get_session_status.json").toObject());
    QCOMPARE(s.dlSpeed, std::optional<qint64>(95920));
    QCOMPARE(s.upSpeed, std::optional<qint64>(0));
    QCOMPARE(s.dlSession, std::optional<qint64>(383683));
    QCOMPARE(s.upSession, std::optional<qint64>(0));
    QCOMPARE(s.freeSpaceOnDisk, std::optional<qint64>(400000000000));
    QCOMPARE(s.connectionStatus, std::optional<QString>(QStringLiteral("firewalled")));
    QCOMPARE(s.dhtNodes, std::optional<int>(0));
    QCOMPARE(s.peerConnections, std::optional<int>(1));
    QCOMPARE(s.dlRateLimit, std::optional<qint64>(0));
    QCOMPARE(s.queueing, std::optional<bool>(true));
    QCOMPARE(s.altSpeedLimits, std::optional<bool>(false));
    // K34: no all-time totals.
    QVERIFY(!s.allTimeDownload && !s.allTimeUpload && !s.globalRatio);
}

void TestDelugeTranslate::limitsAndPreferences()
{
    const QJsonObject config = result("core_get_config_values.json").toObject();
    const GlobalLimits limits = DT::globalLimits(config);
    QCOMPARE(limits.download, std::optional<qint64>(0));
    QCOMPARE(limits.upload, std::optional<qint64>(0));
    QVERIFY(!limits.altDownload && !limits.altUpload);
    QCOMPARE(DT::globalLimits({{QStringLiteral("max_download_speed"), 512.5}}).download,
             std::optional<qint64>(524800));

    const QVariantMap prefs = DT::speedPreferences(config);
    QCOMPARE(prefs.value(QStringLiteral("dl_limit")).toLongLong(), 0);
    QCOMPARE(prefs.value(QStringLiteral("save_path")).toString(),
             QStringLiteral("/srv/deluge/downloads"));
    QCOMPARE(prefs.value(QStringLiteral("listen_port")).toInt(), 26993);
    QCOMPARE(prefs.value(QStringLiteral("dht")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("max_ratio_enabled")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("max_ratio")).toDouble(), 2.0);
    QCOMPARE(prefs.value(QStringLiteral("max_ratio_act")).toInt(), 0);
    // No turtle and no clock (K20).
    QVERIFY(!prefs.contains(QStringLiteral("alt_dl_limit")));
    QVERIFY(!prefs.contains(QStringLiteral("scheduler_enabled")));

    // Settings › Deluge (E6): the whole of core.conf, the rest by Deluge's own names.
    const QVariantMap all = DT::speedPreferences(result("core_get_config.json").toObject());
    QCOMPARE(all.value(QStringLiteral("listen_port")).toInt(), 26993);
    QCOMPARE(all.value(QStringLiteral("utpex")).toBool(), false);
    QCOMPARE(all.value(QStringLiteral("max_connections_global")).toInt(), 200);
    QCOMPARE(all.value(QStringLiteral("max_connections_per_torrent")).toInt(), -1);
    QCOMPARE(all.value(QStringLiteral("max_active_limit")).toInt(), 8);
    QCOMPARE(all.value(QStringLiteral("share_ratio_limit")).toDouble(), 2.0);
    QCOMPARE(all.value(QStringLiteral("seed_time_limit")).toInt(), 180);
    QCOMPARE(all.value(QStringLiteral("max_download_speed_per_torrent")).toDouble(), -1.0);
    QCOMPARE(all.value(QStringLiteral("move_completed_path")).toString(),
             QStringLiteral("/home/user/Downloads"));
    QCOMPARE(all.value(QStringLiteral("encryption")).toString(), QStringLiteral("enabled"));
    QCOMPARE(all.value(QStringLiteral("enabled_plugins")).toStringList(),
             QStringList({QStringLiteral("Label")}));
    // Nothing Settings has no row for (the proxy and its password, the daemon's own).
    QVERIFY(!all.contains(QStringLiteral("proxy")));
    QVERIFY(!all.contains(QStringLiteral("daemon_port")));
    QVERIFY(!all.contains(QStringLiteral("enc_in_policy")));
}

void TestDelugeTranslate::preferencesGoBackAsConfig()
{
    QString why;
    const QJsonObject set =
        DT::configSet({{QStringLiteral("dl_limit"), 102400},
                       {QStringLiteral("up_limit"), 0},
                       {QStringLiteral("listen_port"), 6881},
                       {QStringLiteral("dht"), true},
                       {QStringLiteral("max_ratio_enabled"), true},
                       {QStringLiteral("max_ratio"), 1.5},
                       {QStringLiteral("max_ratio_act"), 1},
                       {QStringLiteral("save_path"), QStringLiteral("/srv/in")}},
                      &why);
    QVERIFY2(!set.isEmpty(), qPrintable(why));
    QCOMPARE(set.value(QStringLiteral("max_download_speed")).toDouble(), 100.0);
    QCOMPARE(set.value(QStringLiteral("max_upload_speed")).toDouble(), -1.0);
    QCOMPARE(set.value(QStringLiteral("listen_ports")).toArray(), QJsonArray({6881, 6881}));
    QCOMPARE(set.value(QStringLiteral("dht")).toBool(), true);
    QCOMPARE(set.value(QStringLiteral("stop_seed_at_ratio")).toBool(), true);
    QCOMPARE(set.value(QStringLiteral("stop_seed_ratio")).toDouble(), 1.5);
    QCOMPARE(set.value(QStringLiteral("remove_seed_at_ratio")).toBool(), true);
    QCOMPARE(set.value(QStringLiteral("download_location")).toString(), QStringLiteral("/srv/in"));

    QVERIFY(DT::configSet({{QStringLiteral("alt_dl_limit"), 10}}, &why).isEmpty());
    QVERIFY(why.contains(QLatin1String("alt_dl_limit")));

    // Settings › Deluge (E6): its own keys by its own names; -1 is no limit where it may be.
    const QJsonObject own =
        DT::configSet({{QStringLiteral("utpex"), false},
                       {QStringLiteral("max_connections_global"), -1},
                       {QStringLiteral("max_active_downloading"), 0},
                       {QStringLiteral("share_ratio_limit"), 1.5},
                       {QStringLiteral("max_upload_speed_per_torrent"), 64},
                       {QStringLiteral("move_completed_path"), QStringLiteral("/srv/done")},
                       {QStringLiteral("encryption"), QStringLiteral("disabled")}},
                      &why);
    QVERIFY2(!own.isEmpty(), qPrintable(why));
    QCOMPARE(own.value(QStringLiteral("utpex")), QJsonValue(false));
    QCOMPARE(own.value(QStringLiteral("max_connections_global")), QJsonValue(-1));
    QCOMPARE(own.value(QStringLiteral("max_active_downloading")), QJsonValue(0));
    QCOMPARE(own.value(QStringLiteral("share_ratio_limit")).toDouble(), 1.5);
    QCOMPARE(own.value(QStringLiteral("max_upload_speed_per_torrent")).toDouble(), 64.0);
    QCOMPARE(own.value(QStringLiteral("move_completed_path")).toString(),
             QStringLiteral("/srv/done"));
    QCOMPARE(own.value(QStringLiteral("enc_in_policy")), QJsonValue(2));
    QCOMPARE(own.value(QStringLiteral("enc_out_policy")), QJsonValue(2));
    // Out of range, not a whole count, an unknown word, a plugin, a key with no row.
    QVERIFY(DT::configSet({{QStringLiteral("max_connections_global"), 0}}, &why).isEmpty());
    QVERIFY(why.contains(QLatin1String("-1 (no limit)")));
    QVERIFY(DT::configSet({{QStringLiteral("max_active_seeding"), 2.5}}, &why).isEmpty());
    QVERIFY(
        DT::configSet({{QStringLiteral("encryption"), QStringLiteral("prefer")}}, &why).isEmpty());
    QVERIFY(DT::configSet({{QStringLiteral("enabled_plugins"), QStringList()}}, &why).isEmpty());
    QVERIFY(DT::configSet({{QStringLiteral("daemon_port"), 1}}, &why).isEmpty());
    QVERIFY(DT::configSet({{QStringLiteral("alt_dl_limit"), 10}}, &why).isEmpty());
    QVERIFY(why.contains(QLatin1String("alt_dl_limit")));
    QVERIFY(DT::configSet({{QStringLiteral("max_ratio_act"), 3}}, &why).isEmpty());
    QVERIFY(DT::configSet({{QStringLiteral("listen_port"), 70000}}, &why).isEmpty());
}

void TestDelugeTranslate::theGeneralTab()
{
    const TorrentProperties stream = DT::properties(recorded(kStream));
    QCOMPARE(stream.hash, kStream);
    QCOMPARE(stream.infohashV1, kStream);
    QCOMPARE(stream.piecesNum, 96);
    QCOMPARE(stream.pieceSize, 65536);
    QVERIFY(stream.piecesHave > 0 && stream.piecesHave < 96);
    QCOMPARE(stream.availability, std::optional<double>(1.0520000457763672));
    QCOMPARE(stream.seeds, 1);
    QCOMPARE(stream.connections, std::optional<int>(1));
    QCOMPARE(stream.comment, QStringLiteral("omaseed fixture"));
    QVERIFY(stream.lastSeen && stream.lastSeen->isValid()); // a peer had it whole
    QVERIFY(!stream.createdOn.isValid());                   // Deluge doesn't say
    QVERIFY(!stream.dlSpeedAvg && !stream.upSpeedAvg);

    // Whole here, and no peer seen whole: nothing said, rather than "never".
    QVERIFY(!DT::properties(recorded(kPack)).lastSeen);
    // Not whole and never seen whole: never.
    const TorrentProperties single = DT::properties(recorded(kSingle));
    QVERIFY(single.lastSeen && !single.lastSeen->isValid());
    QCOMPARE(single.isPrivate, std::optional<bool>(false));
    QVERIFY(!DT::properties(recorded(kMagnet)).isPrivate);
}

void TestDelugeTranslate::theFilesTab()
{
    const QList<ContentFile> files = DT::files(recorded(kPack));
    QCOMPARE(files.size(), 3);
    QCOMPARE(files.at(0).path, QStringLiteral("Fixture Pack/disc 1/track-01.bin"));
    QCOMPARE(files.at(0).index, std::optional<int>(0));
    QCOMPARE(files.at(0).size, 40960);
    QCOMPARE(files.at(0).progress, 1.0);
    // The recorder set them high, low and skip (7, 1, 0).
    QCOMPARE(files.at(0).priority, std::optional<int>(int(FilePriority::High)));
    QCOMPARE(files.at(1).priority, std::optional<int>(int(FilePriority::Low)));
    QCOMPARE(files.at(2).priority, std::optional<int>(int(FilePriority::Skip)));
    // 16 KiB pieces: 0–2, then 2–2, then 2–3.
    QCOMPARE(files.at(0).pieces, (std::optional<std::pair<int, int>>({0, 2})));
    QCOMPARE(files.at(1).pieces, (std::optional<std::pair<int, int>>({2, 2})));
    QCOMPARE(files.at(2).pieces, (std::optional<std::pair<int, int>>({2, 3})));
    QVERIFY(DT::files(recorded(kMagnet)).isEmpty());
}

void TestDelugeTranslate::filePrioritiesBothWays()
{
    QCOMPARE(DT::filePriority(0), int(FilePriority::Skip));
    for (int p : {1, 2, 3})
        QCOMPARE(DT::filePriority(p), int(FilePriority::Low));
    QCOMPARE(DT::filePriority(4), int(FilePriority::Normal));
    for (int p : {5, 6, 7})
        QCOMPARE(DT::filePriority(p), int(FilePriority::High));
    QCOMPARE(DT::delugePriority(FilePriority::Skip), 0);
    QCOMPARE(DT::delugePriority(FilePriority::Low), 1);
    QCOMPARE(DT::delugePriority(FilePriority::Normal), 4);
    QCOMPARE(DT::delugePriority(FilePriority::High), 7);
    QCOMPARE(DT::delugePriority(FilePriority::Maximal), 7); // Deluge's highest is high (K17)
    QCOMPARE(DT::delugePriority(FilePriority::Mixed), 4);
}

void TestDelugeTranslate::theTrackersTabSaysTheOneInUse()
{
    const QList<TrackerEntry> trackers = DT::trackers(recorded(kPack));
    QCOMPARE(trackers.size(), 2);
    // Recorded in one tier, as Deluge listed them.
    QCOMPARE(trackers.at(0).url, QStringLiteral("udp://127.0.0.1:9/announce"));
    QCOMPARE(trackers.at(0).tier, 0);
    // None answered yet, so the first one carries what the last announce said.
    QCOMPARE(trackers.at(0).status, int(TrackerStatus::NotWorking));
    QCOMPARE(trackers.at(0).message, QStringLiteral("Connection refused"));
    QCOMPARE(trackers.at(1).url, QStringLiteral("http://127.0.0.1:9/announce"));
    QCOMPARE(trackers.at(1).tier, 0);
    QCOMPARE(trackers.at(1).status, int(TrackerStatus::NotContacted));
    QVERIFY(!trackers.at(1).seeds && !trackers.at(1).peers);

    QJsonObject working = recorded(kPack);
    working.insert(QStringLiteral("tracker"), QStringLiteral("http://127.0.0.1:9/announce"));
    working.insert(QStringLiteral("tracker_status"), QStringLiteral("Announce OK"));
    const QList<TrackerEntry> ok = DT::trackers(working);
    QCOMPARE(ok.at(0).status, int(TrackerStatus::NotContacted));
    QCOMPARE(ok.at(1).status, int(TrackerStatus::Working));
    QVERIFY(DT::trackers(recorded(kStream)).isEmpty());
}

void TestDelugeTranslate::thePeersTab()
{
    const PeersDelta peers = DT::peers(recorded(kStream));
    QVERIFY(peers.full);
    QCOMPARE(peers.peers.size(), 1);
    const PeerPatch &peer = peers.peers.first();
    QCOMPARE(peer.address, QStringLiteral("127.0.0.1:26994"));
    QCOMPARE(peer.ip, std::optional<QString>(QStringLiteral("127.0.0.1")));
    QCOMPARE(peer.port, std::optional<int>(26994));
    QCOMPARE(peer.client, std::optional<QString>(QStringLiteral("Transmission 4.1.3")));
    QCOMPARE(peer.dlSpeed, std::optional<qint64>(56630));
    QCOMPARE(peer.progress, std::optional<double>(1.0));
    QVERIFY(!peer.countryCode); // "" on loopback
    // A v6 peer in its brackets.
    const PeersDelta v6 =
        DT::peers({{QStringLiteral("peers"),
                    QJsonArray{QJsonObject{{QStringLiteral("ip"), QStringLiteral("[::1]:6881")},
                                           {QStringLiteral("country"), QStringLiteral("PT")}}}}});
    QCOMPARE(v6.peers.first().ip, std::optional<QString>(QStringLiteral("::1")));
    QCOMPARE(v6.peers.first().port, std::optional<int>(6881));
    QCOMPARE(v6.peers.first().countryCode, std::optional<QString>(QStringLiteral("pt")));
}

void TestDelugeTranslate::piecesAsStates()
{
    const QByteArray stream = DT::pieceStates(recorded(kStream));
    QCOMPARE(stream.size(), 96);
    QVERIFY(PieceStates::count(stream, PieceStates::Have) > 0);
    QVERIFY(PieceStates::count(stream, PieceStates::Missing) > 0);
    // Seeding: Deluge sends none, every piece is had.
    QCOMPARE(DT::pieceStates(recorded(kPack)), QByteArray(4, char(PieceStates::Have)));
    QVERIFY(DT::pieceStates(recorded(kMagnet)).isEmpty());
    const QByteArray mixed = DT::pieceStates(
        {{QStringLiteral("pieces"), QJsonArray{0, 1, 2, 3}}, {QStringLiteral("num_pieces"), 4}});
    QByteArray expected(4, char(PieceStates::Missing));
    expected[2] = char(PieceStates::Downloading);
    expected[3] = char(PieceStates::Have);
    QCOMPARE(mixed, expected);
}

void TestDelugeTranslate::labelsAndTheirFolders()
{
    const CategoryPatch fixtures =
        DT::category(QStringLiteral("fixtures"), result("label_get_options.json").toObject());
    QCOMPARE(fixtures.name, QStringLiteral("fixtures"));
    QCOMPARE(fixtures.savePath,
             std::optional<QString>(QStringLiteral("/srv/deluge/fixtures-recording/done")));
    // A label that moves nothing has no folder.
    const CategoryPatch empty =
        DT::category(QStringLiteral("empty"), result("label_get_options_empty.json").toObject());
    QCOMPARE(empty.savePath, std::optional<QString>(QString()));
}

void TestDelugeTranslate::labelNamesAreDeluges()
{
    QString why;
    QCOMPARE(DT::labelName(QStringLiteral("Movies"), &why), QStringLiteral("movies"));
    QCOMPARE(DT::labelName(QStringLiteral(" a.b-c_9 "), &why), QStringLiteral("a.b-c_9"));
    QCOMPARE(DT::labelName(QStringLiteral("two words"), &why), QString());
    QVERIFY(why.contains(QLatin1String("two words")));
    QCOMPARE(DT::labelName(QStringLiteral("ünï"), &why), QString());
    QCOMPARE(DT::labelName(QStringLiteral("  "), &why), QString());
    QVERIFY(why.contains(QLatin1String("empty")));
}

void TestDelugeTranslate::aPrefetchGivesTheFiles()
{
    // The info dictionary alone, base64 (research/87): two files in a folder.
    const QByteArray pieces(20, 'x');
    const QByteArray info = "d5:filesl"
                            "d6:lengthi5e4:pathl" +
                            bencoded("sub") + bencoded("x.bin") +
                            "ee"
                            "d6:lengthi7e4:pathl" +
                            bencoded("y.bin") +
                            "eee"
                            "4:name" +
                            bencoded("pack") + "12:piece lengthi16384e" + "6:pieces" +
                            bencoded(pieces) + "e";
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex());
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:%1&dn=pack&tr=http%3A%2F%2Ft%2Fa").arg(hash);
    bool unreadable = true;
    const std::optional<TorrentMetadata> metadata =
        DT::metadata(QJsonArray{hash, QString::fromLatin1(info.toBase64())}, magnet, &unreadable);
    QVERIFY(metadata);
    QVERIFY(!unreadable);
    QCOMPARE(metadata->infoHashV1, hash);
    QCOMPARE(metadata->torrentId, hash);
    QCOMPARE(metadata->name, QStringLiteral("pack"));
    QVERIFY(metadata->multiFile);
    QCOMPARE(metadata->totalSize, 12);
    QCOMPARE(metadata->pieceLength, 16384);
    QCOMPARE(metadata->files.size(), 2);
    QCOMPARE(metadata->files.at(0).path, QStringLiteral("sub/x.bin"));
    QCOMPARE(metadata->files.at(1).size, 7);
    QCOMPARE(metadata->trackers, QStringList({"http://t/a"}));

    // Something that isn't an info dictionary.
    QVERIFY(
        !DT::metadata(QJsonArray{hash, QStringLiteral("bm90IGJlbmNvZGU=")}, magnet, &unreadable));
    QVERIFY(unreadable);
}

void TestDelugeTranslate::anEmptyPrefetchIsStillLooking()
{
    bool unreadable = true;
    QVERIFY(!DT::metadata(result("core_prefetch_empty.json"), QString(), &unreadable));
    QVERIFY(!unreadable);
}

void TestDelugeTranslate::featuresAreDeluges()
{
    const EngineFeatures f = DT::features(ServerInfo{QStringLiteral("2.2.0"), {}}, true);
    QVERIFY(f.categories && f.categorySavePaths && f.categoriesAreLabels);
    QVERIFY(!f.tags && !f.tagsAreLabels);
    QCOMPARE(f.filePriorities,
             QList<int>({int(FilePriority::Skip),
                         int(FilePriority::Low),
                         int(FilePriority::Normal),
                         int(FilePriority::High)}));
    QCOMPARE(f.shareLimitActions, QStringList({"Stop", "Remove"}));
    QVERIFY(!f.seedingTimeLimit && !f.altSpeeds);
    QVERIFY(f.queue && f.forceStart && f.sequentialDownload && f.firstLastPiece && f.superSeeding);
    QVERIFY(!f.autoManagement);
    QVERIFY(f.fetchMetadata && f.addOptions && !f.addContentLayout && !f.v2Only);
    QVERIFY(!f.renameOnDisk);
    QVERIFY(f.moveProgress && f.moveStopsOnFailure && f.listFolders && f.roomOnServer);
    QVERIFY(!f.movesHoldAnswers && !f.moveReplacesFile);
    QVERIFY(f.peerControl && !f.peerBan && !f.webSeeds && !f.editWebSeeds);
    QVERIFY(!f.engineLog && !f.qbtSettings && !f.torrentsBackup);
    QVERIFY(!f.torrentsExport && !f.torrentsImport);
    QVERIFY(f.sessionSettings); // Settings › Deluge (E6)
    QVERIFY(!f.rss && !f.addAgain && !f.search && !f.createTorrent);

    // The Label plugin off: no labels at all, and the section says why (K15).
    const EngineFeatures off = DT::features(ServerInfo{QStringLiteral("2.2.0"), {}}, false);
    QVERIFY(!off.categories && !off.categorySavePaths && off.categoriesAreLabels);
}

void TestDelugeTranslate::trackersAreEditedWhole()
{
    const QJsonArray before = recorded(kPack).value(QStringLiteral("trackers")).toArray();
    const QJsonArray more = DT::trackersWith(
        before,
        {QStringLiteral("http://new/announce"), QStringLiteral("http://127.0.0.1:9/announce")});
    QCOMPARE(more.size(), 3);
    QCOMPARE(more.at(2).toObject(), QJsonObject({{"url", "http://new/announce"}, {"tier", 1}}));
    // Only url and tier go back.
    QCOMPARE(more.at(0).toObject().keys(), QStringList({"tier", "url"}));
    const QJsonArray fewer =
        DT::trackersWithout(more, {QStringLiteral("udp://127.0.0.1:9/announce")});
    QCOMPARE(fewer.size(), 2);
    QCOMPARE(fewer.at(1).toObject().value(QStringLiteral("url")).toString(),
             QStringLiteral("http://new/announce"));
}

void TestDelugeTranslate::speedsLeaveInKibibytes()
{
    QCOMPARE(DT::speedKiB(0), -1.0);
    QCOMPARE(DT::speedKiB(-5), -1.0);
    QCOMPARE(DT::speedKiB(1024), 1.0);
    QCOMPARE(DT::speedKiB(1536), 1.5);
}

QTEST_GUILESS_MAIN(TestDelugeTranslate)
#include "tst_delugetranslate.moc"
