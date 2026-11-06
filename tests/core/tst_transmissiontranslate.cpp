// TransmissionTranslate (#85, E4): Transmission 4.1's answers in omaseed's terms, read from
// answers recorded from a real daemon (tests/fixtures/transmission/, made by
// scripts/dev/transmission-fixtures.py against the :8085 test server): a seeding pack with
// labels, two failing trackers, a web seed and file priorities; a stopped torrent with its own
// limits; one in a local error after a move into a read-only folder; a magnet waiting for its
// metadata. The peers answer is written by hand from rpc-spec §3.3 (the test server has no
// swarm).

#include "core/AltSchedule.hpp"
#include "core/PieceStates.hpp"
#include "core/engines/transmission/TransmissionTranslate.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QTest>

#include <cmath>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
namespace TT = omaseed::core::TransmissionTranslate;

namespace {

QJsonObject fixture(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/transmission/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject result(const char *name)
{
    return fixture(name).value(QStringLiteral("result")).toObject();
}

/// The recorded torrents, by name.
QJsonObject recorded(const QString &name)
{
    for (const QJsonObject &row : TT::rows(result("torrent_get_all.json"))) {
        if (row.value(QStringLiteral("name")).toString() == name)
            return row;
    }
    qFatal("no recorded torrent %s", qPrintable(name));
}

const QString kPack = QStringLiteral("Fixture Pack");
const QString kSingle = QStringLiteral("fixture-single.bin");
const QString kError = QStringLiteral("fixture-error.bin");
const QString kMagnet = QStringLiteral("fixture-magnet");

} // namespace

class TestTransmissionTranslate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tableAndObjectsReadAlike();
    void statusWordsBecomePhasesAndFlags();
    void statusFromRecordedTorrents();
    void seedingPackInOmaseedsTerms();
    void stoppedTorrentKeepsItsOwnLimits();
    void localErrorCarriesItsWords();
    void magnetWithoutMetadata();
    void incompleteFolderAndQueueFromTheSession();
    void absentFieldsStayAbsent();
    void sessionFiguresAndLimits();
    void sessionAsSpeedPreferences();
    void turtleWindowAsTheDaemonKeepsIt_data();
    void turtleWindowAsTheDaemonKeepsIt();
    void turtleWindowAcrossAMinute_data();
    void turtleWindowAcrossAMinute();
    void turtleTurnedOffByHandInsideTheWindow();
    void sessionAsBasics();
    void settingsByTransmissionsOwnNames();
    void filesWithPrioritiesAndPieces();
    void trackersEachWithItsState();
    void piecesFromTheBitfield();
    void propertiesOfThePack();
    void peersFromTheWholeList();
    void featuresFollowBoard47();
    void speedsLeaveInKilobytes();
    void trackerListsEditedWhole();
};

void TestTransmissionTranslate::tableAndObjectsReadAlike()
{
    const QList<QJsonObject> table = TT::rows(result("torrent_get_table.json"));
    const QList<QJsonObject> objects = TT::rows(result("torrent_get_all.json"));
    QCOMPARE(table.size(), 4);
    QCOMPARE(table.size(), objects.size());
    for (qsizetype i = 0; i < table.size(); ++i) {
        for (auto it = table.at(i).constBegin(); it != table.at(i).constEnd(); ++it)
            QCOMPARE(it.value(), objects.at(i).value(it.key()));
    }
    QVERIFY(TT::rows(QJsonObject()).isEmpty());
}

void TestTransmissionTranslate::statusWordsBecomePhasesAndFlags()
{
    using P = TorrentPhase;
    using F = TorrentFlag;
    const auto of = [](int status, QJsonObject more = {}) {
        more.insert(QStringLiteral("status"), status);
        if (!more.contains(QStringLiteral("left_until_done")))
            more.insert(QStringLiteral("left_until_done"), 100);
        return TT::status(more);
    };
    QCOMPARE(of(0).phase, P::Stopped);
    QCOMPARE(of(0).word, QStringLiteral("stopped"));
    QCOMPARE(of(0, {{QStringLiteral("left_until_done"), 0}}).flags, TorrentFlags(F::Finished));
    QCOMPARE(of(1).phase, P::Checking);
    QVERIFY(of(1).has(F::Queued));
    QCOMPARE(of(2).phase, P::Checking);
    QVERIFY(!of(2).has(F::Queued));
    QCOMPARE(of(3).phase, P::Downloading);
    QVERIFY(of(3).has(F::Queued));
    // Downloading with nothing coming in and nobody to get it from: stalled.
    QVERIFY(of(4).has(F::Stalled));
    const TorrentStatus moving = of(
        4, {{QStringLiteral("rate_download"), 5000}, {QStringLiteral("peers_sending_to_us"), 2}});
    QCOMPARE(moving.phase, P::Downloading);
    QVERIFY(!moving.has(F::Stalled));
    QVERIFY(of(4, {{QStringLiteral("rate_download"), 5000}, {QStringLiteral("is_stalled"), true}})
                .has(F::Stalled));
    QCOMPARE(of(4, {{QStringLiteral("metadata_percent_complete"), 0.25}}).phase, P::Metadata);
    QCOMPARE(of(5).phase, P::Seeding);
    QCOMPARE(of(5).flags, TorrentFlags(F::Queued | F::Finished));
    const TorrentStatus giving = of(6,
                                    {{QStringLiteral("rate_upload"), 100},
                                     {QStringLiteral("peers_getting_from_us"), 1},
                                     {QStringLiteral("left_until_done"), 0}});
    QCOMPARE(giving.phase, P::Seeding);
    QCOMPARE(giving.flags, TorrentFlags(F::Finished));
    QCOMPARE(giving.word, QStringLiteral("seeding"));
    // Only error 3 is the torrent's own; 1 and 2 belong to its trackers.
    QCOMPARE(of(0, {{QStringLiteral("error"), 3}}).phase, P::Errored);
    QCOMPARE(of(0, {{QStringLiteral("error"), 3}}).word, QStringLiteral("error"));
    QCOMPARE(of(6, {{QStringLiteral("error"), 2}}).phase, P::Seeding);
    QCOMPARE(of(6, {{QStringLiteral("error"), 1}}).phase, P::Seeding);
    // Nothing Transmission would send: no phase, no word.
    QCOMPARE(of(7).phase, P::Unknown);
    QVERIFY(of(7).word.isEmpty());
    QCOMPARE(TT::status(QJsonObject()).phase, P::Unknown);
}

void TestTransmissionTranslate::statusFromRecordedTorrents()
{
    QCOMPARE(TT::status(recorded(kPack)).phase, TorrentPhase::Seeding);
    QVERIFY(TT::status(recorded(kPack)).has(TorrentFlag::Finished));
    QVERIFY(TT::status(recorded(kPack)).has(TorrentFlag::Stalled)); // nobody to give to
    QCOMPARE(TT::status(recorded(kSingle)).phase, TorrentPhase::Stopped);
    QVERIFY(!TT::status(recorded(kSingle)).has(TorrentFlag::Finished));
    QCOMPARE(TT::status(recorded(kError)).phase, TorrentPhase::Errored);
    QVERIFY(TT::status(recorded(kError)).has(TorrentFlag::Finished));
    QCOMPARE(TT::status(recorded(kMagnet)).phase, TorrentPhase::Metadata);
}

void TestTransmissionTranslate::seedingPackInOmaseedsTerms()
{
    const QJsonObject json = recorded(kPack);
    const TorrentPatch p = TT::torrent(json);
    QCOMPARE(p.hash, QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91"));
    QCOMPARE(p.values.name, kPack);
    QCOMPARE(p.values.totalSize, qint64(40960 + 20480 + 3000));
    QCOMPARE(p.values.size, json.value(QStringLiteral("size_when_done")).toInteger());
    QCOMPARE(p.values.progress, 1.0);
    QCOMPARE(p.values.status.phase, TorrentPhase::Seeding);
    QCOMPARE(p.values.tags, QStringList({QStringLiteral("fixtures"), QStringLiteral("pack")}));
    QVERIFY(!p.has(TorrentField::Category)); // labels go in the tags place (C14 E5)
    QCOMPARE(p.values.savePath, QStringLiteral("/srv/transmission/fixtures-recording/data"));
    QCOMPARE(p.values.contentPath,
             QStringLiteral("/srv/transmission/fixtures-recording/data/Fixture Pack"));
    QCOMPARE(p.values.addedOn, QDateTime::fromSecsSinceEpoch(1793941304));
    QCOMPARE(p.values.completionOn, QDateTime::fromSecsSinceEpoch(1793941304));
    QVERIFY(!p.values.lastActivity.isValid()); // activity_date 0: never
    QVERIFY(!p.values.eta.has_value());        // -1: no estimate
    QCOMPARE(p.values.ratio, 0.0);
    QCOMPARE(p.values.trackersCount, 2);
    QVERIFY(p.values.tracker.isEmpty());        // none has answered well
    QVERIFY(p.values.hasTrackerError);          // the one that answered failed
    QVERIFY(!p.has(TorrentField::NumComplete)); // no tracker counted the swarm
    QCOMPARE(p.values.priority, 4);             // queue_position 3, queueing on (default)
    QCOMPARE(p.values.ratioLimit, RatioLimit::global());
    QCOMPARE(p.values.inactiveSeedingTimeLimit, MinutesLimit::global());
    QCOMPARE(p.values.dlLimit, qint64(0)); // download_limit 100 but not limited
    QCOMPARE(p.values.infohashV1, p.hash);
    QVERIFY(p.values.magnetUri.startsWith(QStringLiteral("magnet:?xt=urn:btih:69d539")));
    QVERIFY(p.has(TorrentField::ErrorMessage));
    QVERIFY(p.values.errorMessage.isEmpty());
    QCOMPARE(p.values.timeActive,
             json.value(QStringLiteral("seconds_seeding")).toInteger() +
                 json.value(QStringLiteral("seconds_downloading")).toInteger());
}

void TestTransmissionTranslate::stoppedTorrentKeepsItsOwnLimits()
{
    const TorrentPatch p = TT::torrent(recorded(kSingle));
    QCOMPARE(p.values.status.phase, TorrentPhase::Stopped);
    QCOMPARE(p.values.progress, 0.0);
    QCOMPARE(p.values.amountLeft, qint64(49152));
    QVERIFY(!p.values.completionOn.isValid());
    QCOMPARE(p.values.dlLimit, qint64(250 * 1000)); // kB/s, 1000 bytes (units.speed_bytes)
    QCOMPARE(p.values.upLimit, qint64(0));
    QCOMPARE(p.values.ratioLimit, RatioLimit::of(1.5));
    QCOMPARE(p.values.inactiveSeedingTimeLimit, MinutesLimit::unlimited());
    QVERIFY(p.values.seqDl);
    QCOMPARE(p.values.tags, QStringList({QStringLiteral("fixtures")}));
    QVERIFY(!p.values.hasTrackerError); // its tracker hasn't been asked yet
}

void TestTransmissionTranslate::localErrorCarriesItsWords()
{
    const TorrentPatch p = TT::torrent(recorded(kError));
    QCOMPARE(p.values.status.phase, TorrentPhase::Errored);
    QCOMPARE(p.values.errorMessage,
             QStringLiteral("Couldn't move '/srv/transmission/fixtures-recording/errdata' to "
                            "'/srv/transmission/fixtures-recording/readonly/inside': Permission "
                            "denied (13)"));
    // The move failed: its folder is still the old one (C25 K24, MoveFeed's signal).
    QCOMPARE(p.values.savePath, QStringLiteral("/srv/transmission/fixtures-recording/errdata"));
    QVERIFY(p.values.tags.isEmpty());
    QCOMPARE(p.values.trackersCount, 0);
}

void TestTransmissionTranslate::magnetWithoutMetadata()
{
    const TorrentPatch p = TT::torrent(recorded(kMagnet));
    QCOMPARE(p.values.status.phase, TorrentPhase::Metadata);
    QCOMPARE(p.values.totalSize, qint64(-1)); // not known before the metadata
    QCOMPARE(p.values.progress, 0.0);
    QVERIFY(!p.values.eta.has_value());       // -2: unknown
    QCOMPARE(p.values.ratio, 0.0);            // -1: none yet
    QVERIFY(!p.has(TorrentField::IsPrivate)); // the metadata says, later
    QVERIFY(p.values.hasTrackerError);
}

void TestTransmissionTranslate::incompleteFolderAndQueueFromTheSession()
{
    TT::Session session = TT::session(result("session_get.json"));
    QCOMPARE(session.speedBytes, qint64(1000));
    QVERIFY(session.queueing);
    QVERIFY(session.incompleteDir.isEmpty()); // incomplete_dir_enabled false

    QJsonObject sessionGet = result("session_get.json");
    sessionGet.insert(QStringLiteral("incomplete_dir_enabled"), true);
    sessionGet.insert(QStringLiteral("download_queue_enabled"), false);
    sessionGet.insert(QStringLiteral("seed_queue_enabled"), false);
    session = TT::session(sessionGet);
    QCOMPARE(session.incompleteDir, QStringLiteral("/home/user/Downloads"));
    QVERIFY(!session.queueing);

    // Unfinished: in the incomplete folder; finished: where it was put.
    const TorrentPatch single = TT::torrent(recorded(kSingle), session);
    QCOMPARE(single.values.contentPath, QStringLiteral("/home/user/Downloads/fixture-single.bin"));
    QCOMPARE(single.values.downloadPath, QStringLiteral("/home/user/Downloads"));
    QCOMPARE(single.values.priority, -1); // queueing off
    const TorrentPatch pack = TT::torrent(recorded(kPack), session);
    QCOMPARE(pack.values.contentPath,
             QStringLiteral("/srv/transmission/fixtures-recording/data/Fixture Pack"));
}

void TestTransmissionTranslate::absentFieldsStayAbsent()
{
    const TorrentPatch p = TT::torrent(
        {{QStringLiteral("hash_string"), QStringLiteral("ABCDEF")}, {QStringLiteral("status"), 4}});
    QCOMPARE(p.hash, QStringLiteral("abcdef"));
    QCOMPARE(p.fields, TorrentFields({TorrentField::Status, TorrentField::InfohashV1}));
    // What Transmission never reports stays absent everywhere (C25 K29).
    const TorrentPatch all = TT::torrent(recorded(kPack));
    for (const TorrentField field : {TorrentField::Category,
                                     TorrentField::FirstLastPiecePrio,
                                     TorrentField::SuperSeeding,
                                     TorrentField::AutoTmm,
                                     TorrentField::SeedingTimeLimit,
                                     TorrentField::ShareLimitAction,
                                     TorrentField::Availability,
                                     TorrentField::UploadedSession,
                                     TorrentField::InfohashV2})
        QVERIFY2(!all.has(field), qPrintable(QString::number(int(field))));
}

void TestTransmissionTranslate::sessionFiguresAndLimits()
{
    const QJsonObject sessionGet = result("session_get.json");
    const ServerStatePatch s =
        TT::serverState(result("session_stats.json"), sessionGet, result("free_space.json"));
    QCOMPARE(s.dlSpeed, std::optional<qint64>(0));
    QCOMPARE(s.upSpeed, std::optional<qint64>(0));
    QCOMPARE(s.upSession, std::optional<qint64>(0));
    QCOMPARE(s.allTimeUpload, std::optional<qint64>(2098816));
    QCOMPARE(s.allTimeDownload, std::optional<qint64>(0));
    QVERIFY(!s.globalRatio.has_value()); // nothing downloaded: no ratio to show
    QCOMPARE(s.altSpeedLimits, std::optional<bool>(false));
    QCOMPARE(s.dlRateLimit, std::optional<qint64>(0)); // speed_limit_down not enabled
    QCOMPARE(s.queueing, std::optional<bool>(true));
    QCOMPARE(s.freeSpaceOnDisk, std::optional<qint64>(400000000000));
    // Not reported: "", never the "disconnected" a missing value reads (E4 step 3).
    QCOMPARE(s.connectionStatus, std::optional<QString>(QString()));
    QVERIFY(!s.dhtNodes.has_value());

    // Turtle on: the limits in force are the alternative ones.
    QJsonObject turtle = sessionGet;
    turtle.insert(QStringLiteral("alt_speed_enabled"), true);
    const ServerStatePatch slow = TT::serverState(result("session_stats.json"), turtle);
    QCOMPARE(slow.dlRateLimit, std::optional<qint64>(50 * 1000));
    QCOMPARE(slow.upRateLimit, std::optional<qint64>(50 * 1000));
    QVERIFY(!slow.freeSpaceOnDisk.has_value());
    // A folder that isn't there answers -1 sizes: no figure.
    const QJsonObject missing = fixture("free_space_missing.json")
                                    .value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("data"))
                                    .toObject()
                                    .value(QStringLiteral("result"))
                                    .toObject();
    QVERIFY(!TT::serverState({}, {}, missing).freeSpaceOnDisk.has_value());

    const GlobalLimits limits = TT::globalLimits(sessionGet);
    QCOMPARE(limits.download, std::optional<qint64>(0));
    QCOMPARE(limits.upload, std::optional<qint64>(0));
    QCOMPARE(limits.altDownload, std::optional<qint64>(50000));
    QCOMPARE(limits.altUpload, std::optional<qint64>(50000));
    QJsonObject limited = sessionGet;
    limited.insert(QStringLiteral("speed_limit_down_enabled"), true);
    QCOMPARE(TT::globalLimits(limited).download, std::optional<qint64>(100000));
}

// C25 K20: the session's limits, turtle and its clock in app/preferences' words, and back.
void TestTransmissionTranslate::sessionAsSpeedPreferences()
{
    const QJsonObject sessionGet = result("session_get.json");
    const QVariantMap prefs = TT::speedPreferences(sessionGet);
    QCOMPARE(prefs.value(QStringLiteral("dl_limit")).toLongLong(), 0);
    QCOMPARE(prefs.value(QStringLiteral("up_limit")).toLongLong(), 0);
    QCOMPARE(prefs.value(QStringLiteral("alt_dl_limit")).toLongLong(), 50000);
    QCOMPARE(prefs.value(QStringLiteral("alt_up_limit")).toLongLong(), 50000);
    QCOMPARE(prefs.value(QStringLiteral("scheduler_enabled")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("schedule_from_hour")).toInt(), 9);
    QCOMPARE(prefs.value(QStringLiteral("schedule_from_min")).toInt(), 0);
    QCOMPARE(prefs.value(QStringLiteral("schedule_to_hour")).toInt(), 17);
    QCOMPARE(prefs.value(QStringLiteral("scheduler_day_mask")).toInt(), 127);
    QVERIFY(!prefs.contains(QStringLiteral("scheduler_days")));
    QCOMPARE(prefs.value(QStringLiteral("save_path")).toString(),
             QStringLiteral("/srv/transmission/downloads"));
    QVERIFY(TT::speedPreferences({}).isEmpty());

    // The way out: kB/s, enabled flags, a time from its two halves.
    QString why;
    QCOMPARE(TT::speedSessionSet({{QStringLiteral("alt_dl_limit"), 300 * 1024},
                                  {QStringLiteral("dl_limit"), 0},
                                  {QStringLiteral("schedule_from_hour"), 23},
                                  {QStringLiteral("scheduler_enabled"), true}},
                                 sessionGet,
                                 &why),
             QJsonObject({{"alt_speed_down", 308},
                          {"speed_limit_down_enabled", false},
                          {"alt_speed_time_begin", 23 * 60},
                          {"alt_speed_time_enabled", true}}));
    QCOMPARE(TT::speedSessionSet({{QStringLiteral("schedule_to_min"), 30}}, sessionGet),
             QJsonObject({{"alt_speed_time_end", 17 * 60 + 30}}));
    QCOMPARE(TT::speedSessionSet({{QStringLiteral("up_limit"), 2000000}}, sessionGet),
             QJsonObject({{"speed_limit_up_enabled", true}, {"speed_limit_up", 2000}}));
    // Nothing goes when one key has no place in the session.
    QVERIFY(TT::speedSessionSet(
                {{QStringLiteral("alt_up_limit"), 1000}, {QStringLiteral("web_ui_port"), 8081}},
                sessionGet,
                &why)
                .isEmpty());
    QCOMPARE(why, QStringLiteral("Transmission has no setting for web_ui_port"));
}

// #234, C25 K20's note: the turtle clock as Transmission 4.1.3 kept it on a live daemon, read
// through the session the way omaseed reads it. The cases and what the daemon did come from
// scripts/dev/research/234-turtle-window/ (its README has the run): Friday 2026-11-06 at 13:19,
// minute 799; the mask's bit for Friday is 32, Thursday's 16. Offsets are from 13:19.
namespace {

const QDateTime turtleRunAt(QDate(2026, 11, 6), QTime(13, 19));
constexpr int turtleRunMinute = 13 * 60 + 19;
constexpr int friday = 32, thursday = 16;

AltSchedule turtleClock(int begin, int end, int mask)
{
    const auto minute = [](int offset) {
        return ((turtleRunMinute + offset) % 1440 + 1440) % 1440;
    };
    const QJsonObject sessionGet{{"alt_speed_time_enabled", true},
                                 {"alt_speed_time_begin", minute(begin)},
                                 {"alt_speed_time_end", minute(end)},
                                 {"alt_speed_time_day", mask}};
    return AltSchedule::fromPreferences(TT::speedPreferences(sessionGet));
}

} // namespace

void TestTransmissionTranslate::turtleWindowAsTheDaemonKeepsIt_data()
{
    QTest::addColumn<int>("begin");
    QTest::addColumn<int>("end");
    QTest::addColumn<int>("mask");
    QTest::addColumn<bool>("daemon");
    // A window past midnight runs forwards into the next day, on the day it began.
    QTest::newRow("wrap, now after midnight, every day") << 60 << 30 << 127 << true;
    QTest::newRow("wrap, now after midnight, yesterday only") << 60 << 30 << thursday << true;
    QTest::newRow("wrap, now after midnight, today only") << 60 << 30 << friday << false;
    QTest::newRow("wrap, now before midnight, every day") << -60 << -120 << 127 << true;
    QTest::newRow("wrap, now before midnight, today only") << -60 << -120 << friday << true;
    QTest::newRow("wrap, now before midnight, yesterday only") << -60 << -120 << thursday << false;
    QTest::newRow("wrap, now outside it") << 60 << -60 << 127 << false;
    // Begin == end: a whole day from begin, not nothing.
    QTest::newRow("begin == end, before now, every day") << -30 << -30 << 127 << true;
    QTest::newRow("begin == end, after now, yesterday only") << 30 << 30 << thursday << true;
    QTest::newRow("begin == end, after now, today only") << 30 << 30 << friday << false;
    // The day mask on a window within the day.
    QTest::newRow("same day, today only") << -30 << 30 << friday << true;
    QTest::newRow("same day, every day but today") << -30 << 30 << (127 & ~friday) << false;
    QTest::newRow("same day, no day") << -30 << 30 << 0 << false;
}

void TestTransmissionTranslate::turtleWindowAsTheDaemonKeepsIt()
{
    QFETCH(int, begin);
    QFETCH(int, end);
    QFETCH(int, mask);
    QFETCH(bool, daemon);
    const AltSchedule clock = turtleClock(begin, end, mask);
    QCOMPARE(clock.rule(), AltSchedule::Rule::Transmission);
    QCOMPARE(clock.onAt(turtleRunAt), daemon);
    // The daemon acted at once on the new window, so its state is the schedule's, not a hand's.
    QVERIFY(!clock.read(daemon, turtleRunAt).byHand);
}

void TestTransmissionTranslate::turtleWindowAcrossAMinute_data()
{
    QTest::addColumn<int>("begin");
    QTest::addColumn<int>("end");
    QTest::addColumn<int>("mask");
    QTest::addColumn<bool>("before");
    QTest::addColumn<bool>("after");
    QTest::newRow("begins next minute") << 1 << 30 << 127 << false << true;
    QTest::newRow("yesterday's wrap ends next minute") << 60 << 1 << 127 << true << false;
    QTest::newRow("yesterday's whole day ends next minute") << 1 << 1 << thursday << true << false;
    QTest::newRow("whole days meet next minute") << 1 << 1 << 127 << true << true;
}

void TestTransmissionTranslate::turtleWindowAcrossAMinute()
{
    QFETCH(int, begin);
    QFETCH(int, end);
    QFETCH(int, mask);
    QFETCH(bool, before);
    QFETCH(bool, after);
    const AltSchedule clock = turtleClock(begin, end, mask);
    const QDateTime now = turtleRunAt.addSecs(30);
    const QDateTime edge = turtleRunAt.addSecs(60);
    QCOMPARE(clock.onAt(now), before);
    QCOMPARE(clock.onAt(edge), after);
    const AltSchedule::Reading reading = clock.read(before, now);
    QVERIFY(!reading.byHand);
    if (before != after) {
        // The turtle line's "until 13:20": the minute the daemon moved.
        QCOMPARE(reading.until, edge);
        QCOMPARE(AltSchedule::untilText(reading.until, now), QStringLiteral("13:20"));
    } else {
        QVERIFY(!reading.until.isValid()); // on for good: every day's window meets the next
    }
}

// Turned off by hand inside the window, the daemon leaves it off: its scheduler acts only when
// its own reading changes, so the hand's state lasts until the window opens again tomorrow.
void TestTransmissionTranslate::turtleTurnedOffByHandInsideTheWindow()
{
    const AltSchedule clock = turtleClock(-30, 30, 127);
    const AltSchedule::Reading reading = clock.read(false, turtleRunAt.addSecs(5));
    QVERIFY(reading.byHand);
    QCOMPARE(reading.until, turtleRunAt.addDays(1).addSecs(-30 * 60));
}

// TRANSMISSION › Basics (C25 K31): the port, DHT, the incomplete folder and the seeding
// defaults, both ways, in app/preferences' words.
void TestTransmissionTranslate::sessionAsBasics()
{
    const QJsonObject sessionGet = result("session_get.json");
    const QVariantMap prefs = TT::speedPreferences(sessionGet);
    QCOMPARE(prefs.value(QStringLiteral("listen_port")).toInt(), 26994);
    QCOMPARE(prefs.value(QStringLiteral("dht")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("temp_path_enabled")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("temp_path")).toString(),
             QStringLiteral("/home/user/Downloads"));
    QCOMPARE(prefs.value(QStringLiteral("max_ratio_enabled")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("max_ratio")).toDouble(), 2.0);
    QCOMPARE(prefs.value(QStringLiteral("max_inactive_seeding_time_enabled")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("max_inactive_seeding_time")).toInt(), 30);
    for (const char *field :
         {"peer_port", "dht_enabled", "seed_ratio_limited", "idle_seeding_limit"})
        QVERIFY2(TT::sessionFields().contains(QString::fromLatin1(field)), field);

    QCOMPARE(TT::speedSessionSet({{QStringLiteral("listen_port"), 51413},
                                  {QStringLiteral("dht"), true},
                                  {QStringLiteral("temp_path_enabled"), true},
                                  {QStringLiteral("temp_path"), QStringLiteral("/srv/incomplete")},
                                  {QStringLiteral("max_ratio_enabled"), true},
                                  {QStringLiteral("max_ratio"), 1.5},
                                  {QStringLiteral("max_inactive_seeding_time_enabled"), true},
                                  {QStringLiteral("max_inactive_seeding_time"), 45}},
                                 sessionGet),
             QJsonObject({{"peer_port", 51413},
                          {"dht_enabled", true},
                          {"incomplete_dir_enabled", true},
                          {"incomplete_dir", "/srv/incomplete"},
                          {"seed_ratio_limited", true},
                          {"seed_ratio_limit", 1.5},
                          {"idle_seeding_limit_enabled", true},
                          {"idle_seeding_limit", 45}}));
    // qBittorrent's "none" for a ratio leaves the value alone: the switch says it.
    QCOMPARE(TT::speedSessionSet({{QStringLiteral("max_ratio"), -1}}, sessionGet), QJsonObject());
    QString why;
    QVERIFY(
        TT::speedSessionSet({{QStringLiteral("listen_port"), 70000}}, sessionGet, &why).isEmpty());
    QCOMPARE(why, QStringLiteral("a port is from 1 to 65535, not 70000"));
    QVERIFY(TT::portIsOpen(QJsonObject{{"port_is_open", true}, {"ip_protocol", "ipv4"}}));
    QVERIFY(!TT::portIsOpen(QJsonObject{{"port_is_open", false}}));
}

// Settings › Transmission (E5, #86): the rest of the session by its own names, with what
// session_set would take but shouldn't refused before it is sent.
void TestTransmissionTranslate::settingsByTransmissionsOwnNames()
{
    const QJsonObject sessionGet = result("session_get.json");
    const QVariantMap prefs = TT::speedPreferences(sessionGet);
    QCOMPARE(prefs.value(QStringLiteral("download_queue_size")).toInt(), 5);
    QCOMPARE(prefs.value(QStringLiteral("seed_queue_enabled")).toBool(), false);
    QCOMPARE(prefs.value(QStringLiteral("encryption")).toString(), QStringLiteral("preferred"));
    QCOMPARE(prefs.value(QStringLiteral("peer_limit_global")).toInt(), 200);
    QCOMPARE(prefs.value(QStringLiteral("rename_partial_files")).toBool(), true);
    QCOMPARE(prefs.value(QStringLiteral("cache_size_mib")).toInt(), 4);
    QCOMPARE(prefs.value(QStringLiteral("blocklist_url")).toString(),
             QStringLiteral("http://www.example.com/blocklist"));
    // An older daemon's word for "allowed".
    QCOMPARE(TT::speedPreferences({{"encryption", "tolerated"}})
                 .value(QStringLiteral("encryption"))
                 .toString(),
             QStringLiteral("allowed"));
    // The poll asks for less than the pane does.
    QVERIFY(!TT::sessionFields().contains(QStringLiteral("pex_enabled")));
    for (const QString &field : TT::sessionFields())
        QVERIFY2(TT::settingsFields().contains(field), qPrintable(field));
    for (const char *field : {"pex_enabled",
                              "lpd_enabled",
                              "encryption",
                              "peer_limit_per_torrent",
                              "seed_queue_size",
                              "queue_stalled_minutes",
                              "default_trackers",
                              "script_torrent_done_filename"})
        QVERIFY2(TT::settingsFields().contains(QString::fromLatin1(field)), field);

    QCOMPARE(TT::speedSessionSet({{QStringLiteral("pex_enabled"), true},
                                  {QStringLiteral("encryption"), QStringLiteral("required")},
                                  {QStringLiteral("download_queue_size"), 3},
                                  {QStringLiteral("peer_limit_global"), 400},
                                  {QStringLiteral("default_trackers"),
                                   QStringLiteral("https://a.example/announce\n")}},
                                 sessionGet),
             QJsonObject({{"pex_enabled", true},
                          {"encryption", "required"},
                          {"download_queue_size", 3},
                          {"peer_limit_global", 400},
                          {"default_trackers", "https://a.example/announce\n"}}));
    QString why;
    // 4.1.3 keeps a queue of -3 if it is sent one.
    QVERIFY(TT::speedSessionSet({{QStringLiteral("download_queue_size"), -3}}, sessionGet, &why)
                .isEmpty());
    QCOMPARE(why, QStringLiteral("download_queue_size is from 1 to 2147483647, not -3"));
    QVERIFY(TT::speedSessionSet(
                {{QStringLiteral("encryption"), QStringLiteral("tolerated")}}, sessionGet, &why)
                .isEmpty());
    QCOMPARE(why,
             QStringLiteral("encryption is one of required, preferred, allowed, not tolerated"));
    QVERIFY(
        TT::speedSessionSet({{QStringLiteral("blocklist_size"), 10}}, sessionGet, &why).isEmpty());
    QCOMPARE(why, QStringLiteral("Transmission has no setting for blocklist_size"));
}

void TestTransmissionTranslate::filesWithPrioritiesAndPieces()
{
    const QList<ContentFile> files = TT::files(recorded(kPack));
    QCOMPARE(files.size(), 3);
    QCOMPARE(files.at(0).index, std::optional<int>(0));
    QCOMPARE(files.at(0).path, QStringLiteral("Fixture Pack/disc 1/track-01.bin"));
    QCOMPARE(files.at(0).size, qint64(40960));
    QCOMPARE(files.at(0).progress, 1.0);
    QCOMPARE(files.at(0).priority, std::optional<int>(int(FilePriority::High)));
    QCOMPARE(files.at(0).pieces, (std::optional<std::pair<int, int>>({0, 2})));
    QCOMPARE(files.at(1).priority, std::optional<int>(int(FilePriority::Low)));
    QCOMPARE(files.at(2).path, QStringLiteral("Fixture Pack/track-02.bin"));
    QCOMPARE(files.at(2).priority, std::optional<int>(int(FilePriority::Skip)));
    QCOMPARE(files.at(2).pieces, (std::optional<std::pair<int, int>>({2, 3})));
    QVERIFY(!files.at(0).availability.has_value());
    QVERIFY(TT::files(recorded(kMagnet)).isEmpty());

    QCOMPARE(TT::filePriority(true, 0), int(FilePriority::Normal));
    QCOMPARE(TT::filePriority(false, 1), int(FilePriority::Skip));
}

void TestTransmissionTranslate::trackersEachWithItsState()
{
    const QList<TrackerEntry> trackers = TT::trackers(recorded(kPack));
    QCOMPARE(trackers.size(), 2);
    QCOMPARE(trackers.at(0).url, QStringLiteral("http://127.0.0.1:9/announce"));
    QCOMPARE(trackers.at(0).status, int(TrackerStatus::NotContacted));
    QCOMPARE(trackers.at(0).tier, 0);
    QVERIFY(trackers.at(0).message.isEmpty());
    QVERIFY(!trackers.at(0).seeds.has_value()); // -1: not reported
    QCOMPARE(trackers.at(1).url, QStringLiteral("udp://127.0.0.1:9/announce"));
    QCOMPARE(trackers.at(1).status, int(TrackerStatus::NotWorking));
    QCOMPARE(trackers.at(1).message, QStringLiteral("Could not connect to tracker"));
    QCOMPARE(trackers.at(1).peers, std::optional<int>(0));
    QCOMPARE(TT::trackerUrls(recorded(kPack)),
             QStringList({QStringLiteral("http://127.0.0.1:9/announce"),
                          QStringLiteral("udp://127.0.0.1:9/announce")}));

    QJsonObject working{{QStringLiteral("announce"), QStringLiteral("http://t/announce")},
                        {QStringLiteral("has_announced"), true},
                        {QStringLiteral("last_announce_succeeded"), true},
                        {QStringLiteral("seeder_count"), 7},
                        {QStringLiteral("leecher_count"), 3},
                        {QStringLiteral("next_announce_time"), 1800000000}};
    QJsonObject busy = working;
    busy.insert(QStringLiteral("announce_state"), 3);
    QJsonObject lost = working;
    lost.insert(QStringLiteral("last_announce_succeeded"), false);
    lost.insert(QStringLiteral("last_announce_timed_out"), true);
    lost.insert(QStringLiteral("last_announce_result"), QStringLiteral("Tracker did not respond"));
    const QJsonObject json{
        {QStringLiteral("tracker_stats"), QJsonArray{working, busy, lost}},
    };
    const QList<TrackerEntry> three = TT::trackers(json);
    QCOMPARE(three.at(0).status, int(TrackerStatus::Working));
    QCOMPARE(three.at(0).seeds, std::optional<int>(7));
    QCOMPARE(three.at(0).leeches, std::optional<int>(3));
    QCOMPARE(three.at(0).nextAnnounce, std::optional<qint64>(1800000000));
    QCOMPARE(three.at(1).status, int(TrackerStatus::Updating));
    QCOMPARE(three.at(2).status, int(TrackerStatus::Unreachable));
    QCOMPARE(three.at(2).message, QStringLiteral("Tracker did not respond"));

    // The list's current tracker and the swarm's counts come from the same stats.
    QJsonObject row = json;
    row.insert(QStringLiteral("hash_string"), QStringLiteral("aa"));
    const TorrentPatch p = TT::torrent(row);
    QCOMPARE(p.values.tracker, QStringLiteral("http://t/announce"));
    QVERIFY(!p.values.hasTrackerError);
    QCOMPARE(p.values.numComplete, 7);
    QCOMPARE(p.values.numIncomplete, 3);
}

void TestTransmissionTranslate::piecesFromTheBitfield()
{
    // "8A==" is 0xF0: the pack's four pieces, all here.
    const QByteArray states = TT::pieceStates(recorded(kPack));
    QCOMPARE(states.size(), 4);
    QCOMPARE(PieceStates::count(states, PieceStates::Have), 4);
    // The stopped one has none; the magnet has no pieces yet.
    QCOMPARE(PieceStates::count(TT::pieceStates(recorded(kSingle)), PieceStates::Missing), 3);
    QVERIFY(TT::pieceStates(recorded(kMagnet)).isEmpty());
    // Most significant bit first: 0x40 is the second piece.
    const QByteArray second = TT::pieceStates(
        {{QStringLiteral("piece_count"), 3}, {QStringLiteral("pieces"), QStringLiteral("QA==")}});
    QByteArray expected(3, char(PieceStates::Missing));
    expected[1] = char(PieceStates::Have);
    QCOMPARE(second, expected);
}

void TestTransmissionTranslate::propertiesOfThePack()
{
    const QJsonObject json = recorded(kPack);
    const TorrentProperties p = TT::properties(json, 1793941304);
    QCOMPARE(p.hash, QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91"));
    QCOMPARE(p.infohashV1, p.hash);
    QVERIFY(p.infohashV2.isEmpty());
    QCOMPARE(p.name, kPack);
    QCOMPARE(p.comment, QStringLiteral("omaseed fixture"));
    QCOMPARE(p.totalSize, qint64(64440));
    QCOMPARE(p.piecesNum, 4);
    QCOMPARE(p.pieceSize, qint64(16384));
    QCOMPARE(p.piecesHave, 4);
    QCOMPARE(p.connectionsLimit, 50);
    QCOMPARE(p.connections, std::optional<int>(0));
    QVERIFY(!p.createdOn.isValid()); // --anonymize: no creation date
    QCOMPARE(p.completedOn, QDateTime::fromSecsSinceEpoch(1793941304));
    QCOMPARE(p.isPrivate, std::optional<bool>(false));
    QCOMPARE(p.reannounce, qint64(0));
    // Never reported by Transmission, so left out (C25 K29).
    QVERIFY(!p.lastSeen.has_value());
    QVERIFY(!p.availability.has_value());
    QVERIFY(!p.dlSpeedAvg.has_value());

    const TorrentProperties magnet = TT::properties(recorded(kMagnet), 1793941300);
    QCOMPARE(magnet.reannounce, qint64(26)); // next_announce_time 1793941326
    QVERIFY(!magnet.isPrivate.has_value());
}

void TestTransmissionTranslate::peersFromTheWholeList()
{
    const QJsonArray list{
        QJsonObject{{QStringLiteral("address"), QStringLiteral("10.0.0.7")},
                    {QStringLiteral("port"), 51413},
                    {QStringLiteral("client_name"), QStringLiteral("Transmission 4.1.3")},
                    {QStringLiteral("flag_str"), QStringLiteral("TDEI")},
                    {QStringLiteral("is_utp"), true},
                    {QStringLiteral("progress"), 0.5},
                    {QStringLiteral("rate_to_client"), 2000},
                    {QStringLiteral("rate_to_peer"), 300}},
        QJsonObject{{QStringLiteral("address"), QStringLiteral("2001:db8::1")},
                    {QStringLiteral("port"), 6881},
                    {QStringLiteral("is_utp"), false}},
    };
    const PeersDelta delta = TT::peers({{QStringLiteral("peers"), list}});
    QVERIFY(delta.full);
    QVERIFY(!delta.cursor.has_value());
    QCOMPARE(delta.peers.size(), 2);
    QCOMPARE(delta.peers.at(0).address, QStringLiteral("10.0.0.7:51413"));
    QCOMPARE(delta.peers.at(0).client,
             std::optional<QString>(QStringLiteral("Transmission 4.1.3")));
    QCOMPARE(delta.peers.at(0).connection, std::optional<QString>(QStringLiteral("μTP")));
    QCOMPARE(delta.peers.at(0).dlSpeed, std::optional<qint64>(2000));
    QCOMPARE(delta.peers.at(0).upSpeed, std::optional<qint64>(300));
    QCOMPARE(delta.peers.at(0).progress, std::optional<double>(0.5));
    QVERIFY(!delta.peers.at(0).country.has_value());
    QCOMPARE(delta.peers.at(1).address, QStringLiteral("[2001:db8::1]:6881"));
    QCOMPARE(delta.peers.at(1).connection, std::optional<QString>(QStringLiteral("BT")));
    QVERIFY(TT::peers(recorded(kPack)).peers.isEmpty());
    QCOMPARE(TT::webSeeds(recorded(kPack)),
             QStringList({QStringLiteral("http://127.0.0.1:9/seed/")}));
}

void TestTransmissionTranslate::featuresFollowBoard47()
{
    const EngineFeatures f = TT::features({QStringLiteral("4.1.3"), QStringLiteral("6.0.1")});
    QVERIFY(f.tags);
    QVERIFY(f.tagsAreLabels); // B, K13–K14
    QVERIFY(!f.categories);
    QVERIFY(!f.categorySavePaths);
    QCOMPARE(f.filePriorities,
             QList<int>({int(FilePriority::Skip),
                         int(FilePriority::Low),
                         int(FilePriority::Normal),
                         int(FilePriority::High)}));
    QVERIFY(f.shareLimitActions.isEmpty());
    QVERIFY(!f.shareLimitAction());
    QVERIFY(!f.seedingTimeLimit);
    QVERIFY(f.altSpeeds);
    QVERIFY(f.queue);
    QVERIFY(f.forceStart);
    QVERIFY(f.sequentialDownload);
    QVERIFY(f.renameOnDisk);
    QVERIFY(f.sessionSettings);
    QVERIFY(f.moveStopsOnFailure);
    QVERIFY(f.movesHoldAnswers);
    QVERIFY(f.roomOnServer);
    QVERIFY(f.moveReplacesFile);
    for (const bool off : {f.moveProgress,
                           f.listFolders,
                           f.firstLastPiece,
                           f.superSeeding,
                           f.autoManagement,
                           f.fetchMetadata,
                           f.addOptions,
                           f.v2Only,
                           f.peerControl,
                           f.editWebSeeds,
                           f.engineLog,
                           f.qbtSettings,
                           f.torrentsBackup,
                           f.rss,
                           f.addAgain,
                           f.search,
                           f.createTorrent})
        QVERIFY(!off);
}

void TestTransmissionTranslate::speedsLeaveInKilobytes()
{
    QCOMPARE(TT::speedUnits(0), qint64(0));
    QCOMPARE(TT::speedUnits(-1), qint64(0));
    QCOMPARE(TT::speedUnits(1), qint64(1)); // a small limit stays a limit
    QCOMPARE(TT::speedUnits(250000), qint64(250));
    QCOMPARE(TT::speedUnits(250001), qint64(251));
    QCOMPARE(TT::speedUnits(2048, 1024), qint64(2));
}

void TestTransmissionTranslate::trackerListsEditedWhole()
{
    const QString list = recorded(kPack).value(QStringLiteral("tracker_list")).toString();
    QCOMPARE(list, QStringLiteral("http://127.0.0.1:9/announce\nudp://127.0.0.1:9/announce\n"));
    QCOMPARE(TT::trackerListWith(list,
                                 {QStringLiteral("http://new/announce"),
                                  QStringLiteral("http://127.0.0.1:9/announce")}),
             QStringLiteral("http://127.0.0.1:9/announce\nudp://127.0.0.1:9/announce\n\n"
                            "http://new/announce\n"));
    QCOMPARE(TT::trackerListWith(QString(), {QStringLiteral("a"), QStringLiteral("b")}),
             QStringLiteral("a\n\nb\n"));
    QCOMPARE(TT::trackerListWithout(QStringLiteral("a\nb\n\nc\n"), {QStringLiteral("c")}),
             QStringLiteral("a\nb\n"));
    QCOMPARE(TT::trackerListWithout(QStringLiteral("a\n\nc\n"), {QStringLiteral("a")}),
             QStringLiteral("c\n"));
    QCOMPARE(TT::trackerListWithout(QStringLiteral("a\n"), {QStringLiteral("a")}), QString());
}

QTEST_GUILESS_MAIN(TestTransmissionTranslate)
#include "tst_transmissiontranslate.moc"
