// FilterCounts: the rail's numbers, checked against the recorded playground fixture, and the
// cost of a poll at 10 000 torrents — the plan's budget is 20 ms per pass in Release.

#include "core/FilterCounts.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

namespace {

QJsonObject readFixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/maindata/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

/// A library big enough to measure: five states, twenty categories, three tags, four
/// trackers, all interleaved so no counter gets a free ride.
QJsonObject syntheticLibrary(int count)
{
    static const QStringList states{QStringLiteral("downloading"),
                                    QStringLiteral("uploading"),
                                    QStringLiteral("stalledDL"),
                                    QStringLiteral("stoppedUP"),
                                    QStringLiteral("error")};
    static const QStringList trackers{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                                      QStringLiteral("http://tracker.archlinux.org:6969/announce"),
                                      QStringLiteral("https://open.stealth.si:443/announce"),
                                      QString()};

    QJsonObject torrents;
    for (int i = 0; i < count; ++i) {
        torrents.insert(QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0')),
                        QJsonObject{
                            {QStringLiteral("name"), QStringLiteral("torrent %1").arg(i)},
                            {QStringLiteral("state"), states.at(i % states.size())},
                            {QStringLiteral("progress"), (i % 3 == 0) ? 1.0 : 0.5},
                            {QStringLiteral("dlspeed"), (i % 7 == 0) ? 1024 : 0},
                            {QStringLiteral("upspeed"), 0},
                            {QStringLiteral("category"),
                             (i % 20 == 0) ? QString() : QStringLiteral("cat%1").arg(i % 20)},
                            {QStringLiteral("tags"),
                             (i % 4 == 0) ? QString() : QStringLiteral("tag%1").arg(i % 3)},
                            {QStringLiteral("tracker"), trackers.at(i % trackers.size())},
                            {QStringLiteral("size"), 1000 + i},
                        });
    }
    return torrents;
}

} // namespace

class TestFilterCounts : public QObject
{
    Q_OBJECT

private slots:
    void countsTheRecordedFixture();
    void countsEveryStatusGroup();
    void countsCategoriesTagsAndTrackers();
    void recomputesWhenTheModelChanges();
    void revisionMovesWhenOnlyACategoryCountDoes();
    void tenThousandTorrentsStayUnderTwentyMilliseconds();
    void benchmarkRecompute();
};

void TestFilterCounts::countsTheRecordedFixture()
{
    const QJsonObject full = readFixture(QStringLiteral("01-full.json"));
    QVERIFY2(!full.isEmpty(), "tests/fixtures/maindata/01-full.json is missing");

    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);
    model.applyFullUpdate(full.value(QStringLiteral("torrents")).toObject());

    // The playground holds four stopped, metadata-less torrents: three in "fixtures", one in
    // "linux" with two announce URLs.
    QCOMPARE(counts.total(), 4);
    QCOMPARE(counts.stopped(), 4);
    QCOMPARE(counts.downloading(), 4); // stoppedDL is still a download
    QCOMPARE(counts.inProgress(), 0);  // ... but not one in progress, for the footer
    QCOMPARE(counts.seeding(), 0);
    QCOMPARE(counts.active(), 0);
    QCOMPARE(counts.errored(), 0);
    QCOMPARE(counts.categoryCount(QStringLiteral("fixtures")), 3);
    QCOMPARE(counts.categoryCount(QStringLiteral("linux")), 1);
    QCOMPARE(counts.tagCount(QStringLiteral("keep")), 1);
    QCOMPARE(counts.untagged(), 3);
    // Three of the four magnets carry no tracker at all; the fourth already names its
    // announce URL, so it is counted under that host.
    QCOMPARE(counts.trackerless(), 3);
    QCOMPARE(counts.trackerlessHashes().size(), 3);
    QCOMPARE(counts.trackerCount(QStringLiteral("tracker.opentrackr.org")), 1);
}

void TestFilterCounts::countsEveryStatusGroup()
{
    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);

    model.applyFullUpdate(QJsonObject{
        {QStringLiteral("a"),
         QJsonObject{{QStringLiteral("state"), QStringLiteral("downloading")},
                     {QStringLiteral("dlspeed"), 1024},
                     {QStringLiteral("progress"), 0.5}}},
        {QStringLiteral("b"),
         QJsonObject{{QStringLiteral("state"), QStringLiteral("uploading")},
                     {QStringLiteral("progress"), 1.0}}},
        {QStringLiteral("c"),
         QJsonObject{{QStringLiteral("state"), QStringLiteral("stalledDL")},
                     {QStringLiteral("progress"), 0.1}}},
        {QStringLiteral("d"),
         QJsonObject{{QStringLiteral("state"), QStringLiteral("stoppedUP")},
                     {QStringLiteral("progress"), 1.0}}},
        {QStringLiteral("e"),
         QJsonObject{{QStringLiteral("state"), QStringLiteral("error")},
                     {QStringLiteral("progress"), 0.0}}},
        {QStringLiteral("f"),
         QJsonObject{{QStringLiteral("state"), QStringLiteral("checkingUP")},
                     {QStringLiteral("progress"), 1.0}}},
    });

    QCOMPARE(counts.total(), 6);
    QCOMPARE(counts.downloading(), 2); // a, c
    QCOMPARE(counts.inProgress(), 2);  // a, c
    QCOMPARE(counts.seeding(), 2);     // b, f
    QCOMPARE(counts.completed(), 3);   // b, d, f
    QCOMPARE(counts.stopped(), 1);     // d
    QCOMPARE(counts.active(), 2);      // a (speed), b (uploading)
    QCOMPARE(counts.stalled(), 1);     // c
    QCOMPARE(counts.checking(), 1);    // f
    QCOMPARE(counts.errored(), 1);     // e
    QCOMPARE(counts.statusCount(int(StatusGroup::NoGroup)), 6);
    QCOMPARE(counts.statusCount(int(StatusGroup::Errored)), 1);
}

void TestFilterCounts::countsCategoriesTagsAndTrackers()
{
    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);

    model.applyFullUpdate(QJsonObject{
        {QStringLiteral("a"),
         QJsonObject{{QStringLiteral("category"), QStringLiteral("linux")},
                     {QStringLiteral("tags"), QStringLiteral("keep,private")},
                     {QStringLiteral("tracker"),
                      QStringLiteral("udp://tracker.opentrackr.org:1337/announce")}}},
        {QStringLiteral("b"),
         QJsonObject{{QStringLiteral("category"), QStringLiteral("linux")},
                     {QStringLiteral("tags"), QStringLiteral("keep")},
                     {QStringLiteral("tracker"),
                      QStringLiteral("https://tracker.opentrackr.org:443/announce")}}},
        {QStringLiteral("c"),
         QJsonObject{{QStringLiteral("category"), QString()},
                     {QStringLiteral("tags"), QString()},
                     {QStringLiteral("tracker"), QString()}}},
    });

    QCOMPARE(counts.categoryCount(QStringLiteral("linux")), 2);
    QCOMPARE(counts.uncategorised(), 1);
    QCOMPARE(counts.categoryCount(QString()), 1);
    QCOMPARE(counts.tagCount(QStringLiteral("keep")), 2);
    QCOMPARE(counts.tagCount(QStringLiteral("private")), 1);
    QCOMPARE(counts.untagged(), 1);
    // Two announce URLs, one host.
    QCOMPARE(counts.trackerCount(QStringLiteral("tracker.opentrackr.org")), 2);
    QCOMPARE(counts.trackerless(), 1);
    QCOMPARE(counts.trackerlessHashes(), QStringList({QStringLiteral("c")}));
}

void TestFilterCounts::recomputesWhenTheModelChanges()
{
    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);
    model.applyFullUpdate(
        QJsonObject{{QStringLiteral("a"),
                     QJsonObject{{QStringLiteral("state"), QStringLiteral("stoppedDL")}}}});
    QCOMPARE(counts.stopped(), 1);

    QSignalSpy spy(&counts, &FilterCounts::changed);
    model.applyPartial(
        QJsonObject{{QStringLiteral("a"),
                     QJsonObject{{QStringLiteral("state"), QStringLiteral("downloading")},
                                 {QStringLiteral("dlspeed"), 1024}}}},
        QJsonArray());
    QCOMPARE(counts.stopped(), 0);
    QCOMPARE(counts.active(), 1);
    QCOMPARE(spy.count(), 1);

    // A poll that changes nothing must not wake the rail up.
    spy.clear();
    model.applyPartial(
        QJsonObject{{QStringLiteral("a"), QJsonObject{{QStringLiteral("dlspeed"), 1024}}}},
        QJsonArray());
    QCOMPARE(spy.count(), 0);

    model.applyPartial(QJsonObject(), QJsonArray({QStringLiteral("a")}));
    QCOMPARE(counts.total(), 0);
}

void TestFilterCounts::revisionMovesWhenOnlyACategoryCountDoes()
{
    // The rail's category and tag rows re-read their counts on `revision` (a method call in a
    // binding is not re-run on `changed`), so it has to move for a change no property shows:
    // here the total, the groups and "uncategorised" all stay the same.
    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);
    model.applyFullUpdate(
        QJsonObject{{QStringLiteral("a"),
                     QJsonObject{{QStringLiteral("state"), QStringLiteral("stoppedDL")},
                                 {QStringLiteral("category"), QStringLiteral("linux")}}}});
    QCOMPARE(counts.categoryCount(QStringLiteral("swarm")), 0);
    const int before = counts.revision();

    model.applyPartial(
        QJsonObject{{QStringLiteral("a"),
                     QJsonObject{{QStringLiteral("category"), QStringLiteral("swarm")}}}},
        QJsonArray());
    QCOMPARE(counts.categoryCount(QStringLiteral("swarm")), 1);
    QVERIFY(counts.revision() != before);

    const int after = counts.revision();
    model.applyPartial(
        QJsonObject{{QStringLiteral("a"), QJsonObject{{QStringLiteral("dlspeed"), 0}}}},
        QJsonArray());
    QCOMPARE(counts.revision(), after);
}

void TestFilterCounts::tenThousandTorrentsStayUnderTwentyMilliseconds()
{
    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);
    model.applyFullUpdate(syntheticLibrary(10000));
    QCOMPARE(counts.total(), 10000);

    // Warm the host cache the way a running engine would have.
    counts.recompute();

    QElapsedTimer timer;
    qint64 worst = 0;
    for (int run = 0; run < 5; ++run) {
        timer.start();
        counts.recompute();
        worst = qMax(worst, timer.nsecsElapsed());
    }
    const double worstMs = double(worst) / 1e6;
    qInfo("FilterCounts::recompute over 10 000 torrents: worst of 5 runs %.2f ms", worstMs);

#ifdef QT_NO_DEBUG
    QVERIFY2(worstMs < 20.0,
             qPrintable(QStringLiteral("recompute took %1 ms, budget is 20 ms").arg(worstMs)));
#else
    // A debug build carries Qt's container checks; the budget is a Release-build promise.
    QVERIFY2(
        worstMs < 200.0,
        qPrintable(QStringLiteral("recompute took %1 ms even for a debug build").arg(worstMs)));
#endif
}

void TestFilterCounts::benchmarkRecompute()
{
    TorrentModel model;
    FilterCounts counts;
    counts.setModel(&model);
    model.applyFullUpdate(syntheticLibrary(10000));
    counts.recompute();

    QBENCHMARK
    {
        counts.recompute();
    }
}

QTEST_MAIN(TestFilterCounts)
#include "tst_filtercounts.moc"
