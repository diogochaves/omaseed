// QbtTranslate (#81, docs/36 §4): qBittorrent's replies in omaseed's neutral types. Every
// state word through phase and flags gives exactly Q16's results; the wire's sentinels end
// here; a key qBittorrent did not send stays absent.

#include "core/PieceStates.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <cmath>

using namespace omaseed::core;

namespace {

// --- the rules as main had them, one qBittorrent state word at a time (Q16) ------------------

bool among(const QString &word, std::initializer_list<const char *> words)
{
    for (const char *w : words) {
        if (word == QLatin1String(w))
            return true;
    }
    return false;
}

const QStringList kWords{
    QStringLiteral("downloading"),
    QStringLiteral("stalledDL"),
    QStringLiteral("metaDL"),
    QStringLiteral("forcedMetaDL"),
    QStringLiteral("forcedDL"),
    QStringLiteral("queuedDL"),
    QStringLiteral("checkingDL"),
    QStringLiteral("uploading"),
    QStringLiteral("stalledUP"),
    QStringLiteral("forcedUP"),
    QStringLiteral("queuedUP"),
    QStringLiteral("checkingUP"),
    QStringLiteral("stoppedDL"),
    QStringLiteral("stoppedUP"),
    QStringLiteral("pausedDL"),
    QStringLiteral("pausedUP"),
    QStringLiteral("error"),
    QStringLiteral("missingFiles"),
    QStringLiteral("checkingResumeData"),
    QStringLiteral("moving"),
    QStringLiteral("allocating"),
    QStringLiteral("unknown"),
    QStringLiteral("wat"),
};

StatusGroups referenceGroups(const QString &w, double progress, qint64 dl, qint64 ul)
{
    StatusGroups groups;
    if (among(w,
              {"downloading",
               "stalledDL",
               "metaDL",
               "forcedMetaDL",
               "forcedDL",
               "queuedDL",
               "checkingDL",
               "stoppedDL",
               "pausedDL"}))
        groups |= StatusGroup::Downloading;
    if (among(w, {"uploading", "stalledUP", "forcedUP", "queuedUP", "checkingUP"}))
        groups |= StatusGroup::Seeding;
    const bool upState = among(
        w,
        {"uploading", "stalledUP", "forcedUP", "queuedUP", "checkingUP", "stoppedUP", "pausedUP"});
    if (upState || progress >= 1.0)
        groups |= StatusGroup::Completed;
    if (among(w, {"stoppedDL", "stoppedUP", "pausedDL", "pausedUP"}))
        groups |= StatusGroup::Stopped;
    if (among(w, {"stalledDL", "stalledUP"}))
        groups |= StatusGroup::Stalled;
    if (among(w, {"checkingDL", "checkingUP", "checkingResumeData"}))
        groups |= StatusGroup::Checking;
    if (among(w, {"error", "missingFiles"}))
        groups |= StatusGroup::Errored;
    if (dl > 0 || ul > 0 ||
        among(w, {"downloading", "uploading", "forcedDL", "forcedMetaDL", "forcedUP"}))
        groups |= StatusGroup::Active;
    return groups;
}

QString referenceText(const QString &w)
{
    if (among(w, {"downloading", "forcedDL"}))
        return QStringLiteral("downloading");
    if (among(w, {"metaDL", "forcedMetaDL"}))
        return QStringLiteral("metadata");
    if (w == QLatin1String("stalledDL"))
        return QStringLiteral("stalled");
    if (among(w, {"queuedDL", "queuedUP"}))
        return QStringLiteral("queued");
    if (among(w, {"checkingDL", "checkingUP", "checkingResumeData", "allocating"}))
        return QStringLiteral("checking");
    if (among(w, {"uploading", "forcedUP", "stalledUP"}))
        return QStringLiteral("seeding");
    if (among(w, {"stoppedDL", "pausedDL"}))
        return QStringLiteral("stopped");
    if (among(w, {"stoppedUP", "pausedUP"}))
        return QStringLiteral("completed");
    if (w == QLatin1String("error"))
        return QStringLiteral("error");
    if (w == QLatin1String("missingFiles"))
        return QStringLiteral("missing files");
    if (w == QLatin1String("moving"))
        return QStringLiteral("moving");
    return QStringLiteral("unknown");
}

bool referenceHasEta(const QString &w, double progress)
{
    return progress < 1.0 &&
           among(w, {"downloading", "forcedDL", "metaDL", "forcedMetaDL", "stalledDL"});
}

QJsonObject one(const char *key, const QJsonValue &value)
{
    return QJsonObject{{QLatin1StringView(key), value}};
}

} // namespace

class TestQbtTranslate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyStateKeepsQ16();
    void stateWordsBecomePhasesAndFlags();
    void absentKeysStayAbsent();
    void sentinelsBecomeExplicitValues();
    void syncDeltaCarriesTheEnvelope();
    void categoriesKeepAMissingSavePathMissing();
    void serverStateTakesOnlyWhatCame();
    void filePrioritiesAreNamed();
    void filesKeepWhatTheyLack();
    void trackersNameThePeerSources();
    void peersArriveAsADelta();
    void pieceStatesAndLog();
    void propertiesSayWhatIsUnknown();
};

void TestQbtTranslate::everyStateKeepsQ16()
{
    for (const QString &word : kWords) {
        for (const double progress : {0.0, 0.5, 1.0}) {
            for (const qint64 speed : {qint64(0), qint64(1000)}) {
                Torrent t;
                t.status = QbtTranslate::status(word);
                t.progress = progress;
                t.dlspeed = speed;
                const QString what =
                    QStringLiteral("%1 at %2, %3 B/s").arg(word).arg(progress).arg(speed);
                QVERIFY2(t.statusGroups() == referenceGroups(word, progress, speed, 0),
                         qPrintable(what));
                t.dlspeed = 0;
                t.upspeed = speed;
                QVERIFY2(t.statusGroups() == referenceGroups(word, progress, 0, speed),
                         qPrintable(what));
                QVERIFY2(t.stateText() == referenceText(word), qPrintable(what));
                QVERIFY2(t.hasEta() == referenceHasEta(word, progress), qPrintable(what));
            }
        }
    }
}

void TestQbtTranslate::stateWordsBecomePhasesAndFlags()
{
    const TorrentStatus stalledUp = QbtTranslate::status(u"stalledUP");
    QCOMPARE(stalledUp.phase, TorrentPhase::Seeding);
    QVERIFY(stalledUp.has(TorrentFlag::Stalled));
    QVERIFY(stalledUp.has(TorrentFlag::Finished));
    QCOMPARE(stalledUp.word, QStringLiteral("stalledUP"));

    // The pre-5.0 spellings are the same state.
    QCOMPARE(QbtTranslate::status(u"pausedUP").phase, QbtTranslate::status(u"stoppedUP").phase);
    QCOMPARE(QbtTranslate::status(u"pausedUP").flags, QbtTranslate::status(u"stoppedUP").flags);

    const TorrentStatus forcedMeta = QbtTranslate::status(u"forcedMetaDL");
    QCOMPARE(forcedMeta.phase, TorrentPhase::Metadata);
    QVERIFY(forcedMeta.has(TorrentFlag::Forced));

    QCOMPARE(QbtTranslate::status(u"checkingResumeData").phase, TorrentPhase::Loading);
    QCOMPARE(QbtTranslate::status(u"missingFiles").phase, TorrentPhase::Missing);
    QCOMPARE(QbtTranslate::status(u"queuedForChecking").phase, TorrentPhase::Checking);

    // An unknown word keeps the word and says nothing else.
    const TorrentStatus unknown = QbtTranslate::status(u"wat");
    QCOMPARE(unknown.phase, TorrentPhase::Unknown);
    QVERIFY(unknown.flags == TorrentFlags());
    QCOMPARE(unknown.word, QStringLiteral("wat"));
}

void TestQbtTranslate::absentKeysStayAbsent()
{
    const TorrentPatch patch =
        QbtTranslate::torrent(QStringLiteral("aa"), QJsonObject{{QStringLiteral("dlspeed"), 5}});
    QCOMPARE(patch.hash, QStringLiteral("aa"));
    QCOMPARE(patch.fields, TorrentFields({TorrentField::DlSpeed}));
    QCOMPARE(patch.values.dlspeed, qint64(5));

    // Every key maindata sends has its field.
    const QJsonObject everything{
        {QStringLiteral("name"), QStringLiteral("x")},
        {QStringLiteral("size"), 1},
        {QStringLiteral("total_size"), 1},
        {QStringLiteral("progress"), 0.5},
        {QStringLiteral("state"), QStringLiteral("downloading")},
        {QStringLiteral("dlspeed"), 1},
        {QStringLiteral("upspeed"), 1},
        {QStringLiteral("eta"), 1},
        {QStringLiteral("num_seeds"), 1},
        {QStringLiteral("num_complete"), 1},
        {QStringLiteral("num_leechs"), 1},
        {QStringLiteral("num_incomplete"), 1},
        {QStringLiteral("ratio"), 1.0},
        {QStringLiteral("category"), QStringLiteral("c")},
        {QStringLiteral("tags"), QStringLiteral("a, b")},
        {QStringLiteral("save_path"), QStringLiteral("/s")},
        {QStringLiteral("content_path"), QStringLiteral("/s/x")},
        {QStringLiteral("download_path"), QStringLiteral("/d")},
        {QStringLiteral("added_on"), 1},
        {QStringLiteral("completion_on"), 1},
        {QStringLiteral("last_activity"), 1},
        {QStringLiteral("tracker"), QStringLiteral("udp://t/a")},
        {QStringLiteral("trackers_count"), 1},
        {QStringLiteral("has_tracker_error"), false},
        {QStringLiteral("priority"), 1},
        {QStringLiteral("seq_dl"), true},
        {QStringLiteral("f_l_piece_prio"), true},
        {QStringLiteral("super_seeding"), true},
        {QStringLiteral("auto_tmm"), true},
        {QStringLiteral("private"), true},
        {QStringLiteral("dl_limit"), 1},
        {QStringLiteral("up_limit"), 1},
        {QStringLiteral("ratio_limit"), 1.0},
        {QStringLiteral("seeding_time_limit"), 1},
        {QStringLiteral("inactive_seeding_time_limit"), 1},
        {QStringLiteral("share_limit_action"), QStringLiteral("Stop")},
        {QStringLiteral("availability"), 1.0},
        {QStringLiteral("amount_left"), 1},
        {QStringLiteral("completed"), 1},
        {QStringLiteral("downloaded"), 1},
        {QStringLiteral("uploaded"), 1},
        {QStringLiteral("uploaded_session"), 1},
        {QStringLiteral("time_active"), 1},
        {QStringLiteral("seeding_time"), 1},
        {QStringLiteral("infohash_v1"), QStringLiteral("v1")},
        {QStringLiteral("infohash_v2"), QStringLiteral("v2")},
        {QStringLiteral("magnet_uri"), QStringLiteral("magnet:?")},
    };
    const TorrentPatch all = QbtTranslate::torrent(QStringLiteral("aa"), everything);
    QCOMPARE(all.fields, TorrentFields::all());
    QCOMPARE(all.values.tags, QStringList({QStringLiteral("a"), QStringLiteral("b")}));
}

void TestQbtTranslate::sentinelsBecomeExplicitValues()
{
    const auto patch = [](const char *key, const QJsonValue &value) {
        return QbtTranslate::torrent(QStringLiteral("aa"), one(key, value)).values;
    };
    // ETA: 8640000 is "no estimate".
    QCOMPARE(patch("eta", 8640000).eta, std::optional<qint64>());
    QCOMPARE(patch("eta", 120).eta, std::optional<qint64>(120));
    // Ratio: 9999 is infinite.
    QVERIFY(std::isinf(patch("ratio", 9999).ratio));
    QCOMPARE(patch("ratio", 1.25).ratio, 1.25);
    // Share limits: -2 the server's default, -1 none.
    QCOMPARE(patch("ratio_limit", -2).ratioLimit, RatioLimit::global());
    QCOMPARE(patch("ratio_limit", -1).ratioLimit, RatioLimit::unlimited());
    QCOMPARE(patch("ratio_limit", 1.5).ratioLimit, RatioLimit::of(1.5));
    QCOMPARE(patch("seeding_time_limit", -2).seedingTimeLimit, MinutesLimit::global());
    QCOMPARE(patch("seeding_time_limit", -1).seedingTimeLimit, MinutesLimit::unlimited());
    QCOMPARE(patch("inactive_seeding_time_limit", 30).inactiveSeedingTimeLimit,
             MinutesLimit::of(30));
    // And back, for the Limits sheet.
    QCOMPARE(RatioLimit::global().code(), -2.0);
    QCOMPARE(MinutesLimit::unlimited().code(), qint64(-1));
    QCOMPARE(RatioLimit::fromCode(-1), RatioLimit::unlimited());
    // Speed limits: 0 is none, as older servers' -1 is.
    QCOMPARE(patch("dl_limit", -1).dlLimit, qint64(0));
    QCOMPARE(patch("up_limit", 1024).upLimit, qint64(1024));
    // A timestamp of 0 is "never".
    QVERIFY(!patch("completion_on", 0).completionOn.isValid());
    QVERIFY(patch("completion_on", 1790000000).completionOn.isValid());
}

void TestQbtTranslate::syncDeltaCarriesTheEnvelope()
{
    const SyncDelta full = QbtTranslate::syncDelta(QJsonObject{
        {QStringLiteral("rid"), 7},
        {QStringLiteral("full_update"), true},
        {QStringLiteral("torrents"),
         QJsonObject{{QStringLiteral("bb"), one("name", QStringLiteral("b"))},
                     {QStringLiteral("aa"), one("name", QStringLiteral("a"))}}},
        {QStringLiteral("tags"), QJsonArray{QStringLiteral("t")}},
        {QStringLiteral("trackers"),
         QJsonObject{{QStringLiteral("udp://t/a"), QJsonArray{QStringLiteral("aa")}}}},
        {QStringLiteral("server_state"), one("dl_info_speed", 9)},
    });
    QVERIFY(full.full);
    QCOMPARE(full.cursor, std::optional<int>(7));
    // In the map's order, which is hash order.
    QCOMPARE(full.torrents.size(), 2);
    QCOMPARE(full.torrents.at(0).hash, QStringLiteral("aa"));
    QCOMPARE(full.tags, QStringList({QStringLiteral("t")}));
    QCOMPARE(full.trackers.value(QStringLiteral("udp://t/a")), QStringList({QStringLiteral("aa")}));
    QVERIFY(full.serverState);
    QCOMPARE(full.serverState->dlSpeed, std::optional<qint64>(9));

    const SyncDelta partial = QbtTranslate::syncDelta(QJsonObject{
        {QStringLiteral("torrents_removed"), QJsonArray{QStringLiteral("aa")}},
        {QStringLiteral("categories_removed"), QJsonArray{QStringLiteral("c")}},
        {QStringLiteral("tags_removed"), QJsonArray{QStringLiteral("t")}},
        {QStringLiteral("trackers_removed"), QJsonArray{QStringLiteral("udp://t/a")}},
    });
    QVERIFY(!partial.full);
    QVERIFY(!partial.cursor);
    QVERIFY(!partial.serverState);
    QCOMPARE(partial.removedTorrents, QStringList({QStringLiteral("aa")}));
    QCOMPARE(partial.removedCategories, QStringList({QStringLiteral("c")}));
    QCOMPARE(partial.removedTags, QStringList({QStringLiteral("t")}));
    QCOMPARE(partial.removedTrackers, QStringList({QStringLiteral("udp://t/a")}));
}

void TestQbtTranslate::categoriesKeepAMissingSavePathMissing()
{
    const QList<CategoryPatch> categories = QbtTranslate::categories(QJsonObject{
        {QStringLiteral("movies"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("movies")},
                     {QStringLiteral("savePath"), QStringLiteral("/m")}}},
        {QStringLiteral("tv"), one("download_path", QStringLiteral("/x"))},
    });
    QCOMPARE(categories.size(), 2);
    QCOMPARE(categories.at(0).savePath, std::optional<QString>(QStringLiteral("/m")));
    // No name in the object: the key is the name. No savePath: absent, not empty.
    QCOMPARE(categories.at(1).name, QStringLiteral("tv"));
    QVERIFY(!categories.at(1).savePath);
}

void TestQbtTranslate::serverStateTakesOnlyWhatCame()
{
    const ServerStatePatch state = QbtTranslate::serverState(QJsonObject{
        {QStringLiteral("free_space_on_disk"), 42},
        {QStringLiteral("global_ratio"), QStringLiteral("2.16")},
    });
    QCOMPARE(state.freeSpaceOnDisk, std::optional<qint64>(42));
    QCOMPARE(state.globalRatio, std::optional<double>(2.16));
    QVERIFY(!state.dlSpeed);
    QCOMPARE(QbtTranslate::serverState(one("global_ratio", QStringLiteral("x"))).globalRatio,
             std::optional<double>(-1.0));
}

void TestQbtTranslate::filePrioritiesAreNamed()
{
    QCOMPARE(QbtTranslate::filePriority(0), std::optional<int>(int(FilePriority::Skip)));
    QCOMPARE(QbtTranslate::filePriority(1), std::optional<int>(int(FilePriority::Normal)));
    QCOMPARE(QbtTranslate::filePriority(4), std::optional<int>(int(FilePriority::Normal)));
    QCOMPARE(QbtTranslate::filePriority(6), std::optional<int>(int(FilePriority::High)));
    QCOMPARE(QbtTranslate::filePriority(7), std::optional<int>(int(FilePriority::Maximal)));
    QCOMPARE(QbtTranslate::filePriority(-1), std::optional<int>());
    QCOMPARE(QbtTranslate::filePriority(8), std::optional<int>());
}

void TestQbtTranslate::filesKeepWhatTheyLack()
{
    const QList<ContentFile> files = QbtTranslate::files(QJsonArray{
        QJsonObject{{QStringLiteral("index"), 3},
                    {QStringLiteral("name"), QStringLiteral("a/b.txt")},
                    {QStringLiteral("size"), 10},
                    {QStringLiteral("progress"), 0.5},
                    {QStringLiteral("priority"), 6},
                    {QStringLiteral("availability"), 1.5},
                    {QStringLiteral("piece_range"), QJsonArray{2, 4}}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("c.txt")},
                    {QStringLiteral("availability"), -1}},
    });
    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0).index, std::optional<int>(3));
    QCOMPARE(files.at(0).priority, std::optional<int>(int(FilePriority::High)));
    QCOMPARE(files.at(0).availability, std::optional<double>(1.5));
    QCOMPARE(files.at(0).pieces, (std::optional<std::pair<int, int>>(std::pair{2, 4})));
    QVERIFY(!files.at(1).index);
    QVERIFY(!files.at(1).priority);
    QVERIFY(!files.at(1).availability);
    QVERIFY(!files.at(1).pieces);
}

void TestQbtTranslate::trackersNameThePeerSources()
{
    const QList<TrackerEntry> trackers = QbtTranslate::trackers(
        QJsonArray{
            QJsonObject{{QStringLiteral("url"), QStringLiteral("** [DHT] **")},
                        {QStringLiteral("status"), 0},
                        {QStringLiteral("num_seeds"), -1}},
            QJsonObject{{QStringLiteral("url"), QStringLiteral("** [PeX] **")}},
            QJsonObject{{QStringLiteral("url"), QStringLiteral("** [LSD] **")}},
            QJsonObject{{QStringLiteral("url"), QStringLiteral("udp://t:1/a")},
                        {QStringLiteral("tier"), 0},
                        {QStringLiteral("status"), 2},
                        {QStringLiteral("num_seeds"), 4},
                        {QStringLiteral("next_announce"), 1790000100},
                        {QStringLiteral("msg"), QStringLiteral("ok")}},
            QJsonObject{{QStringLiteral("url"), QString()}},
        },
        1790000000);
    QCOMPARE(trackers.size(), 4); // the one without a URL is not a tracker
    QCOMPARE(trackers.at(0).source, TrackerSource::Dht);
    QCOMPARE(trackers.at(0).url, QStringLiteral("** [DHT] **"));
    QVERIFY(!trackers.at(0).seeds);
    QCOMPARE(trackers.at(1).source, TrackerSource::Pex);
    QCOMPARE(trackers.at(2).source, TrackerSource::Lsd);
    QCOMPARE(trackers.at(3).source, TrackerSource::Announce);
    QCOMPARE(trackers.at(3).seeds, std::optional<int>(4));
    QCOMPARE(trackers.at(3).nextAnnounce, std::optional<qint64>(1790000100));
    QCOMPARE(trackers.at(3).message, QStringLiteral("ok"));
    QVERIFY(!trackers.at(3).leeches);
}

void TestQbtTranslate::peersArriveAsADelta()
{
    const PeersDelta full = QbtTranslate::peers(QJsonObject{
        {QStringLiteral("rid"), 3},
        {QStringLiteral("full_update"), true},
        {QStringLiteral("peers"),
         QJsonObject{{QStringLiteral("1.2.3.4:5"),
                      QJsonObject{{QStringLiteral("ip"), QStringLiteral("1.2.3.4")},
                                  {QStringLiteral("dl_speed"), 100}}}}},
    });
    QVERIFY(full.full);
    QCOMPARE(full.cursor, std::optional<int>(3));
    QCOMPARE(full.peers.size(), 1);
    QCOMPARE(full.peers.first().address, QStringLiteral("1.2.3.4:5"));
    QCOMPARE(full.peers.first().dlSpeed, std::optional<qint64>(100));
    QVERIFY(!full.peers.first().client);

    const PeersDelta partial = QbtTranslate::peers(
        QJsonObject{{QStringLiteral("peers_removed"), QJsonArray{QStringLiteral("1.2.3.4:5")}}});
    QVERIFY(!partial.full);
    QVERIFY(!partial.cursor);
    QCOMPARE(partial.removed, QStringList({QStringLiteral("1.2.3.4:5")}));
}

void TestQbtTranslate::pieceStatesAndLog()
{
    QCOMPARE(QbtTranslate::pieceStates(QJsonArray{0, 1, 2, 7}),
             QByteArray::fromRawData("\x00\x01\x02\x00", 4));
    QCOMPARE(QbtTranslate::webSeeds(
                 QJsonArray{one("url", QStringLiteral("http://w/")), one("url", QString())}),
             QStringList({QStringLiteral("http://w/")}));
    const QList<LogEntry> log = QbtTranslate::log(QJsonArray{
        QJsonObject{{QStringLiteral("id"), 4},
                    {QStringLiteral("timestamp"), 1790000000},
                    {QStringLiteral("type"), 8},
                    {QStringLiteral("message"), QStringLiteral("boom")}},
        QJsonObject{},
    });
    QCOMPARE(log.size(), 2);
    QCOMPARE(log.at(0), (LogEntry{4, 1790000000, 8, QStringLiteral("boom")}));
    QCOMPARE(log.at(1).id, -1);
}

void TestQbtTranslate::propertiesSayWhatIsUnknown()
{
    const TorrentProperties known = QbtTranslate::properties(QJsonObject{
        {QStringLiteral("availability"), 1.5},
        {QStringLiteral("nb_connections"), 3},
        {QStringLiteral("nb_connections_limit"), 100},
        {QStringLiteral("last_seen"), -1},
        {QStringLiteral("pieces_num"), 10},
        {QStringLiteral("private"), true},
        {QStringLiteral("dl_speed_avg"), 5},
    });
    QCOMPARE(known.availability, std::optional<double>(1.5));
    QCOMPARE(known.connections, std::optional<int>(3));
    // Said, and never: present but not a date.
    QVERIFY(known.lastSeen && !known.lastSeen->isValid());
    QCOMPARE(known.isPrivate, std::optional<bool>(true));
    QCOMPARE(known.dlSpeedAvg, std::optional<qint64>(5));
    QVERIFY(!known.upSpeedAvg);

    const TorrentProperties unknown = QbtTranslate::properties(QJsonObject{
        {QStringLiteral("availability"), -1},
        {QStringLiteral("private"), QJsonValue::Null},
        {QStringLiteral("is_private"), false},
    });
    QVERIFY(!unknown.availability);
    QVERIFY(!unknown.connections);
    QVERIFY(!unknown.lastSeen);
    QVERIFY(!unknown.isPrivate); // null until the metadata says
    // An older server's is_private counts once there is metadata.
    QCOMPARE(QbtTranslate::properties(QJsonObject{{QStringLiteral("is_private"), true},
                                                  {QStringLiteral("pieces_num"), 4}})
                 .isPrivate,
             std::optional<bool>(true));
}

QTEST_GUILESS_MAIN(TestQbtTranslate)
#include "tst_qbttranslate.moc"
