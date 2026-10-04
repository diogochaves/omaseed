#include "core/Format.hpp"
#include "core/GlobalState.hpp"
#include "core/SeedConsole.hpp"
#include "core/SeedLedger.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonArray>
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

    // A cross-seed: a second torrent, on another tracker, on the files Nosferatu already has.
    // It took nothing (downloaded 0) and gave 1 200 in its own swarm.
    static QJsonObject crossSeedOf(const QString &name, qint64 size, qint64 uploaded, int quietDays = 0)
    {
        QJsonObject o = row(name, size, double(uploaded) / double(size), 3, 0, QStringLiteral("films"), 0,
                            false, quietDays);
        o[QStringLiteral("downloaded")] = 0;
        o[QStringLiteral("uploaded")] = uploaded;
        o[QStringLiteral("tracker")] = QStringLiteral("https://other.example.net/announce");
        return o;
    }

    void crossSeedsCountTheirFilesOnce()
    {
        Fixture f;
        f.console.refresh();
        const QString offerBefore = f.console.totals().value(QStringLiteral("offerText")).toString();
        f.model.applyPartial(QJsonObject{{QStringLiteral("fff"), crossSeedOf(QStringLiteral("Nosferatu"), 4000, 1200)}},
                             QJsonArray());
        f.console.refresh();
        // The disk holds the files once: what you offer does not grow.
        QCOMPARE(f.console.totals().value(QStringLiteral("offerText")).toString(), offerBefore);

        // Both selected: the files once, what both gave, what was taken once.
        const QVariantMap s = f.console.selectionSummary({QStringLiteral("aaa"), QStringLiteral("fff")});
        QCOMPARE(s.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(s.value(QStringLiteral("sizeText")).toString(), Format::size(4000));
        QCOMPARE(s.value(QStringLiteral("givenText")).toString(), Format::size(2800));
        QCOMPARE(s.value(QStringLiteral("ratioText")).toString(), QStringLiteral("0.70"));

        // The ladder: two dots below 1.0, but the files owe what is left of 4 KB after both
        // gave (1.2 KB), not each torrent its own 4 KB again.
        const QString ladder = f.console.ladder().value(QStringLiteral("summary")).toString();
        QVERIFY2(ladder.startsWith(QStringLiteral("2 below 1.0 · %1 still to give back").arg(Format::size(1200))),
                 qPrintable(ladder));
    }

    void quietCrossSeedsHoldTheirFilesOnce()
    {
        Fixture f;
        f.model.applyPartial(QJsonObject{{QStringLiteral("ggg"), crossSeedOf(QStringLiteral("dataset"), 90000, 0, 94)}},
                             QJsonArray());
        f.console.refresh();
        const QVariantMap holding = f.console.holding();
        QCOMPARE(holding.value(QStringLiteral("rows")).toList().size(), 2);
        QCOMPARE(holding.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("%1 in 2, quiet for 30 days or more").arg(Format::size(90000)));
    }

    // A torrent removed from qBittorrent: the year keeps what it gave (the server's days), no
    // block lists it by a bare hash, and "lately" keeps its name.
    void aRemovedTorrentKeepsItsHistoryNotItsRow()
    {
        Fixture f;
        const qint64 t = kNow.toSecsSinceEpoch();
        f.ledger.observe(t - 7200, 0, 0, {{QStringLiteral("zzz"), 0, 0}, {QStringLiteral("ccc"), 0, 0}});
        f.ledger.observe(t - 7198, 5000, 0, {{QStringLiteral("zzz"), 4000, 0}, {QStringLiteral("ccc"), 1000, 0}});
        f.ledger.observe(t - 7196, 5000, 0, {{QStringLiteral("ccc"), 1000, 0}}); // zzz removed
        f.console.setRecentProvider([t] {
            return QVariantList{QVariantMap{{QStringLiteral("kind"), QStringLiteral("milestone")},
                                            {QStringLiteral("hash"), QStringLiteral("zzz")},
                                            {QStringLiteral("name"), QStringLiteral("Gone film")},
                                            {QStringLiteral("ratio"), 1},
                                            {QStringLiteral("at"), t - 7197}}};
        });
        f.console.refresh();
        QCOMPARE(f.console.totals().value(QStringLiteral("todayText")).toString(),
                 QStringLiteral("+") + Format::size(5000));
        const QVariantList earning = f.console.earning().value(QStringLiteral("rows")).toList();
        for (const QVariant &r : earning)
            QVERIFY(r.toMap().value(QStringLiteral("hash")).toString() != QLatin1String("zzz"));
        const QVariantList lately = f.console.lately();
        QCOMPARE(lately.size(), 1);
        QCOMPARE(lately.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Gone film"));
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
