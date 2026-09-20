// TorrentModel: the full map, the partial merge, removals, the shape of dataChanged and the
// derived roles the table binds to. The JSON here has the same shape as the recorded
// fixtures in tests/fixtures/maindata/.

#include "core/Format.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

namespace {

QJsonObject
torrentJson(const QString &name, const QString &state, double progress, qint64 dlspeed = 0)
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("state"), state},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("dlspeed"), dlspeed},
        {QStringLiteral("upspeed"), 0},
        {QStringLiteral("size"), 1404304589},
        {QStringLiteral("eta"), 160},
        {QStringLiteral("num_seeds"), 21},
        {QStringLiteral("num_leechs"), 42},
        {QStringLiteral("ratio"), 0.15},
        {QStringLiteral("category"), QStringLiteral("linux")},
        {QStringLiteral("tags"), QStringLiteral("keep, private")},
        {QStringLiteral("tracker"), QStringLiteral("udp://tracker.opentrackr.org:1337/announce")},
        {QStringLiteral("added_on"), 1789833476},
        {QStringLiteral("completion_on"), -1},
    };
}

QJsonObject threeTorrents()
{
    return QJsonObject{
        {QStringLiteral("aaaa"),
         torrentJson(QStringLiteral("alpha"), QStringLiteral("downloading"), 0.489, 1929379)},
        {QStringLiteral("bbbb"),
         torrentJson(QStringLiteral("bravo"), QStringLiteral("uploading"), 1.0)},
        {QStringLiteral("cccc"),
         torrentJson(QStringLiteral("charlie"), QStringLiteral("stoppedDL"), 0.0)},
    };
}

int roleOf(const TorrentModel &model, const char *name)
{
    const QHash<int, QByteArray> names = model.roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (it.value() == name)
            return it.key();
    }
    return -1;
}

} // namespace

class TestTorrentModel : public QObject
{
    Q_OBJECT

private slots:
    void fullUpdateFillsRowsAndIndex();
    void rolesCoverTheTableAndTheDetails();
    void partialMergesOnlyThePresentKeys();
    void shareLimitActionParsesAndDefaults();
    void partialAppendsUnknownHashes();
    void partialRemovesInContiguousRuns();
    void dataChangedIsBatchedPerRun();
    void dataChangedNamesOnlyTheChangedRoles();
    void statusGroupsFollowTheRail();
    void stateTextAndColourRole_data();
    void stateTextAndColourRole();
};

void TestTorrentModel::fullUpdateFillsRowsAndIndex()
{
    TorrentModel model;
    QSignalSpy countSpy(&model, &TorrentModel::countChanged);
    QSignalSpy updatedSpy(&model, &TorrentModel::updated);

    model.applyFullUpdate(threeTorrents());

    QCOMPARE(model.count(), 3);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(updatedSpy.count(), 1);
    QCOMPARE(model.hashes(),
             QStringList({QStringLiteral("aaaa"), QStringLiteral("bbbb"), QStringLiteral("cccc")}));
    QCOMPARE(model.indexOfHash(QStringLiteral("bbbb")), 1);
    QCOMPARE(model.indexOfHash(QStringLiteral("nope")), -1);
    QCOMPARE(model.torrent(QStringLiteral("aaaa")).name, QStringLiteral("alpha"));
    QVERIFY(model.torrent(QStringLiteral("nope")).hash.isEmpty());

    // A second full update replaces, it does not append.
    model.applyFullUpdate(
        QJsonObject{{QStringLiteral("dddd"),
                     torrentJson(QStringLiteral("delta"), QStringLiteral("stalledUP"), 1.0)}});
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.indexOfHash(QStringLiteral("aaaa")), -1);
}

void TestTorrentModel::rolesCoverTheTableAndTheDetails()
{
    TorrentModel model;
    model.applyFullUpdate(threeTorrents());
    const QModelIndex first = model.index(0);

    // Spec §1 Fig. 1 columns: Name (+ dim category), Size, Progress, Status, S/P, ↓, ↑, ETA.
    QCOMPARE(model.data(first, TorrentModel::NameRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(model.data(first, TorrentModel::CategoryRole).toString(), QStringLiteral("linux"));
    QCOMPARE(model.data(first, TorrentModel::SizeTextRole).toString(), QStringLiteral("1.31 GB"));
    QCOMPARE(model.data(first, TorrentModel::ProgressTextRole).toString(), QStringLiteral("48.9%"));
    QCOMPARE(model.data(first, TorrentModel::ProgressPercentRole).toString(),
             QStringLiteral("48.9 %"));
    QCOMPARE(model.data(first, TorrentModel::StateTextRole).toString(),
             QStringLiteral("downloading"));
    QCOMPARE(model.data(first, TorrentModel::SeedsPeersTextRole).toString(),
             QStringLiteral("21 / 42"));
    QCOMPARE(model.data(first, TorrentModel::DlspeedTextRole).toString(),
             QStringLiteral("1.84 MB/s"));
    QCOMPARE(model.data(first, TorrentModel::UpspeedTextRole).toString(), QStringLiteral("0"));
    QCOMPARE(model.data(first, TorrentModel::EtaTextRole).toString(), QStringLiteral("2m 40s"));
    QCOMPARE(model.data(first, TorrentModel::RatioTextRole).toString(), QStringLiteral("0.15"));
    QCOMPARE(model.data(first, TorrentModel::TagsRole).toStringList(),
             QStringList({QStringLiteral("keep"), QStringLiteral("private")}));
    QCOMPARE(model.data(first, TorrentModel::TrackerHostRole).toString(),
             QStringLiteral("tracker.opentrackr.org"));
    QVERIFY(!model.data(first, TorrentModel::AddedOnRole).toDateTime().isNull());

    // A completed torrent shows no estimate.
    QCOMPARE(model.data(model.index(1), TorrentModel::EtaTextRole).toString(), QStringLiteral("—"));

    // Every role has a name and every name resolves back.
    QCOMPARE(model.roleNames().size(), 60);
    QCOMPARE(roleOf(model, "stateText"), int(TorrentModel::StateTextRole));
    QCOMPARE(roleOf(model, "stateRole"), int(TorrentModel::StateColorRoleRole));
    QCOMPARE(roleOf(model, "seedsPeersText"), int(TorrentModel::SeedsPeersTextRole));
}

void TestTorrentModel::partialMergesOnlyThePresentKeys()
{
    TorrentModel model;
    model.applyFullUpdate(threeTorrents());

    model.applyPartial(
        QJsonObject{{QStringLiteral("aaaa"), QJsonObject{{QStringLiteral("dlspeed"), 4414997}}}},
        QJsonArray());

    const Torrent &t = model.torrent(QStringLiteral("aaaa"));
    QCOMPARE(t.dlspeed, 4414997);
    QCOMPARE(t.name, QStringLiteral("alpha"));     // untouched
    QCOMPARE(t.category, QStringLiteral("linux")); // untouched
    QCOMPARE(t.progress, 0.489);                   // untouched
    QCOMPARE(model.count(), 3);
}

void TestTorrentModel::shareLimitActionParsesAndDefaults()
{
    // T5.5 Q14: `share_limit_action` (API 2.15.1) rides along as a plain string. A server
    // that never sends the key (pre-2.15.1, and the JSON above) leaves it at "Default".
    TorrentModel model;
    model.applyFullUpdate(threeTorrents());
    const int role = roleOf(model, "shareLimitAction");
    QCOMPARE(role, int(TorrentModel::ShareLimitActionRole));
    QCOMPARE(model.torrent(QStringLiteral("aaaa")).shareLimitAction, QStringLiteral("Default"));
    QCOMPARE(model.data(model.index(0), role).toString(), QStringLiteral("Default"));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.applyPartial(QJsonObject{{QStringLiteral("aaaa"),
                                    QJsonObject{{QStringLiteral("share_limit_action"),
                                                 QStringLiteral("RemoveWithContent")}}}},
                       QJsonArray());
    QCOMPARE(model.torrent(QStringLiteral("aaaa")).shareLimitAction,
             QStringLiteral("RemoveWithContent"));
    QCOMPARE(model.data(model.index(0), role).toString(), QStringLiteral("RemoveWithContent"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(changed.first().at(2).value<QList<int>>().contains(role));
    // The other rows are untouched.
    QCOMPARE(model.torrent(QStringLiteral("bbbb")).shareLimitAction, QStringLiteral("Default"));
}

void TestTorrentModel::partialAppendsUnknownHashes()
{
    TorrentModel model;
    model.applyFullUpdate(threeTorrents());
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    model.applyPartial(
        QJsonObject{{QStringLiteral("dddd"),
                     torrentJson(QStringLiteral("delta"), QStringLiteral("metaDL"), 0.0)}},
        QJsonArray());

    QCOMPARE(model.count(), 4);
    QCOMPARE(model.indexOfHash(QStringLiteral("dddd")), 3);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 3);
    QCOMPARE(inserted.first().at(2).toInt(), 3);
}

void TestTorrentModel::partialRemovesInContiguousRuns()
{
    TorrentModel model;
    QJsonObject five = threeTorrents();
    five.insert(QStringLiteral("dddd"),
                torrentJson(QStringLiteral("delta"), QStringLiteral("uploading"), 1.0));
    five.insert(QStringLiteral("eeee"),
                torrentJson(QStringLiteral("echo"), QStringLiteral("uploading"), 1.0));
    model.applyFullUpdate(five);
    QCOMPARE(model.count(), 5);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.applyPartial(
        QJsonObject(),
        QJsonArray({QStringLiteral("bbbb"), QStringLiteral("cccc"), QStringLiteral("eeee")}));

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.hashes(), QStringList({QStringLiteral("aaaa"), QStringLiteral("dddd")}));
    QCOMPARE(model.indexOfHash(QStringLiteral("dddd")), 1); // the index map followed the shift
    QCOMPARE(removed.count(), 2);                           // {4} and {1,2}, not three signals
}

void TestTorrentModel::dataChangedIsBatchedPerRun()
{
    TorrentModel model;
    model.applyFullUpdate(threeTorrents());
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    // Rows 0 and 1 are contiguous, row 2 is untouched: one signal covering 0..1.
    model.applyPartial(
        QJsonObject{{QStringLiteral("aaaa"), QJsonObject{{QStringLiteral("dlspeed"), 10}}},
                    {QStringLiteral("bbbb"), QJsonObject{{QStringLiteral("upspeed"), 20}}}},
        QJsonArray());

    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).toModelIndex().row(), 0);
    QCOMPARE(changed.first().at(1).toModelIndex().row(), 1);

    // Rows 0 and 2 are not contiguous: two signals.
    changed.clear();
    model.applyPartial(
        QJsonObject{{QStringLiteral("aaaa"), QJsonObject{{QStringLiteral("dlspeed"), 11}}},
                    {QStringLiteral("cccc"), QJsonObject{{QStringLiteral("dlspeed"), 12}}}},
        QJsonArray());
    QCOMPARE(changed.count(), 2);

    // A key that did not move emits nothing at all.
    changed.clear();
    model.applyPartial(
        QJsonObject{{QStringLiteral("aaaa"), QJsonObject{{QStringLiteral("dlspeed"), 11}}}},
        QJsonArray());
    QCOMPARE(changed.count(), 0);
}

void TestTorrentModel::dataChangedNamesOnlyTheChangedRoles()
{
    TorrentModel model;
    model.applyFullUpdate(threeTorrents());
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.applyPartial(
        QJsonObject{{QStringLiteral("aaaa"), QJsonObject{{QStringLiteral("dlspeed"), 4414997}}}},
        QJsonArray());

    QCOMPARE(changed.count(), 1);
    const QList<int> roles = changed.first().at(2).value<QList<int>>();
    QVERIFY(!roles.isEmpty());
    QVERIFY(roles.contains(int(TorrentModel::DlspeedRole)));
    QVERIFY(roles.contains(int(TorrentModel::DlspeedTextRole)));
    QVERIFY(roles.contains(int(TorrentModel::StatusGroupsRole))); // active follows the speed
    QVERIFY(roles.contains(int(TorrentModel::TorrentRole)));
    QVERIFY(!roles.contains(int(TorrentModel::NameRole)));

    // Many keys at once: the run falls back to "every role".
    changed.clear();
    QJsonObject everything =
        torrentJson(QStringLiteral("alpha renamed"), QStringLiteral("stalledUP"), 1.0, 0);
    everything.insert(QStringLiteral("uploaded"), 4096);
    everything.insert(QStringLiteral("downloaded"), 8192);
    everything.insert(QStringLiteral("time_active"), 862);
    model.applyPartial(QJsonObject{{QStringLiteral("aaaa"), everything}}, QJsonArray());
    QCOMPARE(changed.count(), 1);
    QVERIFY(changed.first().at(2).value<QList<int>>().isEmpty());
}

void TestTorrentModel::statusGroupsFollowTheRail()
{
    Torrent downloading;
    downloading.applyJson(
        torrentJson(QStringLiteral("d"), QStringLiteral("downloading"), 0.5, 100));
    QVERIFY(downloading.inGroup(StatusGroup::Downloading));
    QVERIFY(downloading.inGroup(StatusGroup::Active));
    QVERIFY(!downloading.inGroup(StatusGroup::Completed));
    QVERIFY(!downloading.inGroup(StatusGroup::Seeding));

    Torrent stalled;
    stalled.applyJson(torrentJson(QStringLiteral("s"), QStringLiteral("stalledDL"), 0.227));
    QVERIFY(stalled.inGroup(StatusGroup::Downloading));
    QVERIFY(stalled.inGroup(StatusGroup::Stalled));
    QVERIFY(!stalled.inGroup(StatusGroup::Active));

    Torrent seeding;
    seeding.applyJson(torrentJson(QStringLiteral("u"), QStringLiteral("uploading"), 1.0));
    QVERIFY(seeding.inGroup(StatusGroup::Seeding));
    QVERIFY(seeding.inGroup(StatusGroup::Completed));
    QVERIFY(seeding.inGroup(StatusGroup::Active)); // uploading counts as active

    Torrent stoppedComplete;
    stoppedComplete.applyJson(torrentJson(QStringLiteral("p"), QStringLiteral("stoppedUP"), 1.0));
    QVERIFY(stoppedComplete.inGroup(StatusGroup::Stopped));
    QVERIFY(stoppedComplete.inGroup(StatusGroup::Completed));
    QVERIFY(!stoppedComplete.inGroup(StatusGroup::Seeding));
    QVERIFY(!stoppedComplete.inGroup(StatusGroup::Downloading));

    Torrent stoppedIncomplete;
    stoppedIncomplete.applyJson(torrentJson(QStringLiteral("q"), QStringLiteral("stoppedDL"), 0.0));
    QVERIFY(stoppedIncomplete.inGroup(StatusGroup::Stopped));
    QVERIFY(stoppedIncomplete.inGroup(StatusGroup::Downloading));
    QVERIFY(!stoppedIncomplete.inGroup(StatusGroup::Completed));

    Torrent checking;
    checking.applyJson(torrentJson(QStringLiteral("c"), QStringLiteral("checkingUP"), 1.0));
    QVERIFY(checking.inGroup(StatusGroup::Checking));
    QVERIFY(checking.inGroup(StatusGroup::Seeding));

    Torrent broken;
    broken.applyJson(torrentJson(QStringLiteral("e"), QStringLiteral("missingFiles"), 0.0));
    QVERIFY(broken.inGroup(StatusGroup::Errored));

    // The pre-5.0 spellings behave exactly like the new ones.
    Torrent paused;
    paused.applyJson(torrentJson(QStringLiteral("x"), QStringLiteral("pausedUP"), 1.0));
    QCOMPARE(paused.statusGroups(), stoppedComplete.statusGroups());
}

void TestTorrentModel::stateTextAndColourRole_data()
{
    QTest::addColumn<QString>("state");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("colourRole");

    QTest::newRow("downloading") << QStringLiteral("downloading") << QStringLiteral("downloading")
                                 << QStringLiteral("stateDownloading");
    QTest::newRow("forcedDL") << QStringLiteral("forcedDL") << QStringLiteral("downloading")
                              << QStringLiteral("stateDownloading");
    QTest::newRow("metaDL") << QStringLiteral("metaDL") << QStringLiteral("metadata")
                            << QStringLiteral("stateDownloading");
    QTest::newRow("uploading") << QStringLiteral("uploading") << QStringLiteral("seeding")
                               << QStringLiteral("stateSeeding");
    QTest::newRow("stalledDL") << QStringLiteral("stalledDL") << QStringLiteral("stalled")
                               << QStringLiteral("stateStalled");
    QTest::newRow("queuedDL") << QStringLiteral("queuedDL") << QStringLiteral("queued")
                              << QStringLiteral("stateQueued");
    QTest::newRow("checkingDL") << QStringLiteral("checkingDL") << QStringLiteral("checking")
                                << QStringLiteral("stateChecking");
    QTest::newRow("checkingResumeData")
        << QStringLiteral("checkingResumeData") << QStringLiteral("checking")
        << QStringLiteral("stateChecking");
    QTest::newRow("moving") << QStringLiteral("moving") << QStringLiteral("moving")
                            << QStringLiteral("stateChecking");
    QTest::newRow("stoppedDL") << QStringLiteral("stoppedDL") << QStringLiteral("stopped")
                               << QStringLiteral("stateStopped");
    QTest::newRow("stoppedUP") << QStringLiteral("stoppedUP") << QStringLiteral("completed")
                               << QStringLiteral("stateSeeding");
    QTest::newRow("error") << QStringLiteral("error") << QStringLiteral("error")
                           << QStringLiteral("stateError");
    QTest::newRow("nonsense") << QStringLiteral("wat") << QStringLiteral("unknown")
                              << QStringLiteral("stateStopped");
}

void TestTorrentModel::stateTextAndColourRole()
{
    QFETCH(QString, state);
    QFETCH(QString, text);
    QFETCH(QString, colourRole);

    Torrent t;
    t.applyJson(QJsonObject{{QStringLiteral("state"), state}});
    QCOMPARE(t.stateText(), text);
    QCOMPARE(t.stateRole(), colourRole);
    if (state != QStringLiteral("wat"))
        QCOMPARE(torrentStateKey(t.state), state);
}

QTEST_MAIN(TestTorrentModel)
#include "tst_torrentmodel.moc"
