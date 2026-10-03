#include "core/GlobalState.hpp"
#include "core/SeedConsole.hpp"
#include "core/SeedLedger.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonObject>
#include <QTest>

using namespace omaseed::core;

namespace {

const QDateTime kNow(QDate(2026, 10, 3), QTime(12, 0));

QJsonObject row(const QString &name,
                qint64 size,
                double ratio,
                int swarmSeeds,
                int waiting,
                const QString &category,
                qint64 upspeed = 0,
                bool trackerError = false,
                int quietDays = 0)
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("size"), size},
        {QStringLiteral("total_size"), size},
        {QStringLiteral("progress"), 1.0},
        {QStringLiteral("state"), upspeed > 0 ? QStringLiteral("uploading") : QStringLiteral("stalledUP")},
        {QStringLiteral("ratio"), ratio},
        {QStringLiteral("uploaded"), qint64(double(size) * ratio)},
        {QStringLiteral("downloaded"), size},
        {QStringLiteral("num_complete"), swarmSeeds},
        {QStringLiteral("num_incomplete"), waiting},
        {QStringLiteral("num_leechs"), upspeed > 0 ? 2 : 0},
        {QStringLiteral("upspeed"), upspeed},
        {QStringLiteral("category"), category},
        {QStringLiteral("content_path"), QStringLiteral("/data/") + name},
        {QStringLiteral("has_tracker_error"), trackerError},
        {QStringLiteral("tracker"), QStringLiteral("https://t.example.org/announce")},
        {QStringLiteral("trackers_count"), 1},
        {QStringLiteral("added_on"), kNow.addDays(-400).toSecsSinceEpoch()},
        {QStringLiteral("last_activity"), kNow.addDays(-quietDays).toSecsSinceEpoch()},
    };
}

struct Fixture
{
    TorrentModel model;
    TorrentFilterModel filter;
    GlobalState state;
    SeedLedger ledger;
    SeedConsole console;

    Fixture()
    {
        model.applyFullUpdate(QJsonObject{
            {QStringLiteral("aaa"), row(QStringLiteral("Nosferatu"), 4000, 0.4, 1, 3, QStringLiteral("films"))},
            {QStringLiteral("bbb"), row(QStringLiteral("Chopin"), 1000, 6.8, 2, 0, QStringLiteral("audio"))},
            {QStringLiteral("ccc"), row(QStringLiteral("debian.iso"), 700, 12.0, 2400, 31, QStringLiteral("linux"), 1000)},
            {QStringLiteral("ddd"), row(QStringLiteral("dataset"), 90000, 1.5, 40, 0, QStringLiteral("data"), 0, false, 94)},
            {QStringLiteral("eee"), row(QStringLiteral("krita"), 400, 2.0, 1, 0, QStringLiteral("linux"), 0, true)},
        });
        filter.setSourceModel(&model);
        state.applyServerState(QJsonObject{{QStringLiteral("alltime_ul"), 1000000},
                                           {QStringLiteral("alltime_dl"), 250000},
                                           {QStringLiteral("global_ratio"), QStringLiteral("4.00")},
                                           {QStringLiteral("connection_status"), QStringLiteral("connected")}});
        console.setClock([] { return kNow; });
        console.setSources(&model, &filter, nullptr, &state, &ledger);
    }
};

} // namespace

class TestSeedConsole : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void wholeLibraryUsesTheServersTotals()
    {
        Fixture f;
        f.console.refresh();
        QVERIFY(!f.console.scoped());
        const QVariantMap t = f.console.totals();
        QCOMPARE(t.value(QStringLiteral("givenText")).toString(), QStringLiteral("977 KB"));
        QCOMPARE(t.value(QStringLiteral("ratioText")).toString(), QStringLiteral("4.00"));
        QCOMPARE(t.value(QStringLiteral("bufferNegative")).toBool(), false);
    }

    void aScopeSumsItsTorrents()
    {
        Fixture f;
        f.filter.setCategory(QStringLiteral("films"));
        f.console.refresh();
        QVERIFY(f.console.scoped());
        QCOMPARE(f.console.scopeCount(), 1);
        const QVariantMap t = f.console.totals();
        QCOMPARE(t.value(QStringLiteral("ratioText")).toString(), QStringLiteral("0.40"));
        QCOMPARE(t.value(QStringLiteral("bufferNegative")).toBool(), true);
        QCOMPARE(t.value(QStringLiteral("takenNote")).toString(), QStringLiteral("by these torrents"));
    }

    void keepingAliveBucketsAndRarestFirst()
    {
        Fixture f;
        f.console.refresh();
        const QVariantMap alive = f.console.alive();
        const QVariantList buckets = alive.value(QStringLiteral("buckets")).toList();
        QCOMPARE(buckets.at(0).toMap().value(QStringLiteral("count")).toInt(), 1); // only you
        QCOMPARE(buckets.at(1).toMap().value(QStringLiteral("count")).toInt(), 1); // 2 seeds
        QCOMPARE(buckets.at(5).toMap().value(QStringLiteral("count")).toInt(), 1); // krita: error
        const QVariantList rows = alive.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Nosferatu"));
        QCOMPARE(rows.first().toMap().value(QStringLiteral("seedsText")).toString(), QStringLiteral("only you"));
        QCOMPARE(rows.first().toMap().value(QStringLiteral("waitingText")).toString(), QStringLiteral("3 waiting"));
    }

    void attentionNamesTheTrackerThatFails()
    {
        Fixture f;
        f.console.refresh();
        const QVariantMap a = f.console.attention();
        QVERIFY(a.value(QStringLiteral("visible")).toBool());
        QCOMPARE(a.value(QStringLiteral("errors")).toInt(), 1);
        QCOMPARE(a.value(QStringLiteral("title")).toString(), QStringLiteral("1 seeding torrent earn nothing"));
    }

    void holdingSkipsRareTorrents()
    {
        Fixture f;
        f.console.refresh();
        const QVariantList rows = f.console.holding().value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("dataset"));
        QCOMPARE(rows.first().toMap().value(QStringLiteral("quietText")).toString(), QStringLiteral("quiet 94 d"));
    }

    void onAirAndLadder()
    {
        Fixture f;
        f.console.refresh();
        const QVariantList air = f.console.onAir().value(QStringLiteral("rows")).toList();
        QCOMPARE(air.size(), 1);
        QCOMPARE(air.first().toMap().value(QStringLiteral("peersText")).toString(), QStringLiteral("2 peers"));
        QCOMPARE(f.console.ladder().value(QStringLiteral("points")).toList().size(), 5);
        QVERIFY(f.console.ladder().value(QStringLiteral("summary")).toString().startsWith(QStringLiteral("1 below 1.0")));
    }

    void theTideReadsTheLedger()
    {
        Fixture f;
        const qint64 t = kNow.toSecsSinceEpoch();
        f.ledger.observe(t - 1200, 0, 0, {{QStringLiteral("ccc"), 0, 0}});
        f.ledger.observe(t - 1198, 6000, 0, {{QStringLiteral("ccc"), 6000, 0}});
        f.console.setRange(QStringLiteral("24h"));
        f.console.refresh();
        const QVariantMap tide = f.console.tide();
        const QVariantList points = tide.value(QStringLiteral("points")).toList();
        QCOMPARE(points.size(), 144);
        QVERIFY(!points.first().toMap().value(QStringLiteral("up")).isValid()); // before the ledger
        QCOMPARE(tide.value(QStringLiteral("upText")).toString(), QStringLiteral("6 KB"));
        QCOMPARE(f.console.totals().value(QStringLiteral("todayText")).toString(), QStringLiteral("+6 KB"));
    }

    void selectionSummaryAndRarity()
    {
        Fixture f;
        const QVariantMap s = f.console.selectionSummary({QStringLiteral("aaa"), QStringLiteral("bbb")});
        QCOMPARE(s.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(s.value(QStringLiteral("sizeText")).toString(), QStringLiteral("5 KB"));
        const QVariantMap r = f.console.rarity({QStringLiteral("aaa")});
        QCOMPARE(r.value(QStringLiteral("onlyYou")).toInt(), 1);
        QCOMPARE(r.value(QStringLiteral("waiting")).toInt(), 3);
        QCOMPARE(r.value(QStringLiteral("name")).toString(), QStringLiteral("Nosferatu"));
        QCOMPARE(SeedConsole::swarmText(1), QStringLiteral("only you"));
        QCOMPARE(SeedConsole::swarmText(0), QString());
    }

    void rangesStep()
    {
        Fixture f;
        f.console.stepRange(1);
        QCOMPARE(f.console.range(), QStringLiteral("7d"));
        f.console.stepRange(5);
        QCOMPARE(f.console.range(), QStringLiteral("1y"));
        f.console.stepRange(-9);
        QCOMPARE(f.console.range(), QStringLiteral("1h"));
    }
};

QTEST_GUILESS_MAIN(TestSeedConsole)
#include "tst_seedconsole.moc"
