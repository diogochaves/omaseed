// TrackerModel: the `{trackerUrl: [hashes]}` map of sync/maindata grouped into the rail's
// rows — one per host, DHT/PeX/LSD named, trackerless last (spec §1).

#include "QbtFeed.hpp"
#include "core/Torrent.hpp"
#include "core/TrackerModel.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

class TestTrackerModel : public QObject
{
    Q_OBJECT

private slots:
    void hostFromUrl_data();
    void hostFromUrl();
    void fullUpdateGroupsUrlsByHost();
    void rolesCarryHostUrlsAndHashes();
    void partialMergesAndRemovals();
    void trackerlessRowIsLastAndOptional();
    void clearEmptiesIt();
    void aBigHostRebuildsInLinearTime();
};

void TestTrackerModel::hostFromUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("host");

    QTest::newRow("udp") << QStringLiteral("udp://tracker.opentrackr.org:1337/announce")
                         << QStringLiteral("tracker.opentrackr.org");
    QTest::newRow("https") << QStringLiteral("https://open.stealth.si:443/announce")
                           << QStringLiteral("open.stealth.si");
    QTest::newRow("dht") << QStringLiteral("** [DHT] **") << QStringLiteral("DHT");
    QTest::newRow("pex") << QStringLiteral("** [PeX] **") << QStringLiteral("PeX");
    QTest::newRow("lsd") << QStringLiteral("** [LSD] **") << QStringLiteral("LSD");
    QTest::newRow("empty") << QString() << QStringLiteral("trackerless");
    QTest::newRow("garbage") << QStringLiteral("not a url") << QStringLiteral("not a url");
}

void TestTrackerModel::hostFromUrl()
{
    QFETCH(QString, url);
    QFETCH(QString, host);
    QCOMPARE(trackerHost(url), host);
}

void TestTrackerModel::fullUpdateGroupsUrlsByHost()
{
    TrackerModel model;
    qbtfeed::full(model,
                  QJsonObject{
                      {QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                       QJsonArray({QStringLiteral("aaaa"), QStringLiteral("bbbb")})},
                      {QStringLiteral("https://tracker.opentrackr.org:443/announce"),
                       QJsonArray({QStringLiteral("bbbb"), QStringLiteral("cccc")})},
                      {QStringLiteral("http://tracker.archlinux.org:6969/announce"),
                       QJsonArray({QStringLiteral("aaaa")})},
                      {QStringLiteral("** [DHT] **"), QJsonArray({QStringLiteral("dddd")})},
                  });

    QCOMPARE(model.hosts(),
             QStringList({QStringLiteral("DHT"),
                          QStringLiteral("tracker.archlinux.org"),
                          QStringLiteral("tracker.opentrackr.org")}));
    // Two announce URLs, one host, the hash lists merged without duplicates.
    QCOMPARE(model.hashesFor(QStringLiteral("tracker.opentrackr.org")),
             QStringList({QStringLiteral("aaaa"), QStringLiteral("bbbb"), QStringLiteral("cccc")}));
    QCOMPARE(model.hashesFor(QStringLiteral("nope")), QStringList());
}

void TestTrackerModel::rolesCarryHostUrlsAndHashes()
{
    TrackerModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                               QJsonArray({QStringLiteral("aaaa")})}});

    const QModelIndex first = model.index(0);
    QCOMPARE(model.data(first, TrackerModel::HostRole).toString(),
             QStringLiteral("tracker.opentrackr.org"));
    QCOMPARE(model.data(first, TrackerModel::UrlsRole).toStringList(),
             QStringList({QStringLiteral("udp://tracker.opentrackr.org:1337/announce")}));
    QCOMPARE(model.data(first, TrackerModel::HashesRole).toStringList(),
             QStringList({QStringLiteral("aaaa")}));
    QCOMPARE(model.data(first, TrackerModel::TorrentCountRole).toInt(), 1);
    QCOMPARE(model.data(first, TrackerModel::IsTrackerlessRole).toBool(), false);
    QCOMPARE(model.roleNames().value(TrackerModel::TorrentCountRole), QByteArray("torrentCount"));
}

void TestTrackerModel::partialMergesAndRemovals()
{
    TrackerModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                               QJsonArray({QStringLiteral("aaaa")})},
                              {QStringLiteral("http://tracker.archlinux.org:6969/announce"),
                               QJsonArray({QStringLiteral("bbbb")})}});

    // A changed hash list replaces, it does not append.
    qbtfeed::partial(model,
                     QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                                  QJsonArray({QStringLiteral("cccc")})}},
                     QJsonArray());
    QCOMPARE(model.hashesFor(QStringLiteral("tracker.opentrackr.org")),
             QStringList({QStringLiteral("cccc")}));

    qbtfeed::partial(model,
                     QJsonObject(),
                     QJsonArray({QStringLiteral("http://tracker.archlinux.org:6969/announce")}));
    QCOMPARE(model.hosts(), QStringList({QStringLiteral("tracker.opentrackr.org")}));

    // An empty hash list means the server dropped that announce URL too.
    qbtfeed::partial(
        model,
        QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"), QJsonArray()}},
        QJsonArray());
    QCOMPARE(model.count(), 0);
}

void TestTrackerModel::trackerlessRowIsLastAndOptional()
{
    TrackerModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("udp://zzz.example.org:1337/announce"),
                               QJsonArray({QStringLiteral("aaaa")})}});
    QCOMPARE(model.count(), 1);

    model.setTrackerlessHashes(QStringList({QStringLiteral("bbbb"), QStringLiteral("cccc")}));
    QCOMPARE(model.hosts(),
             QStringList({QStringLiteral("zzz.example.org"), QStringLiteral("trackerless")}));
    QCOMPARE(model.data(model.index(1), TrackerModel::IsTrackerlessRole).toBool(), true);
    QCOMPARE(model.data(model.index(1), TrackerModel::TorrentCountRole).toInt(), 2);

    model.setTrackerlessHashes(QStringList());
    QCOMPARE(model.hosts(), QStringList({QStringLiteral("zzz.example.org")}));
}

void TestTrackerModel::clearEmptiesIt()
{
    TrackerModel model;
    qbtfeed::full(model,
                  QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                               QJsonArray({QStringLiteral("aaaa")})}});
    model.setTrackerlessHashes(QStringList({QStringLiteral("bbbb")}));
    model.clear();
    QCOMPARE(model.count(), 0);
}

void TestTrackerModel::aBigHostRebuildsInLinearTime()
{
    // One tracker, two announce URLs (udp + http), 20 000 torrents on both: the de-dupe per
    // host used to be a QStringList::contains, quadratic in the torrents of the host.
    constexpr int kTorrents = 20000;
    QJsonArray hashes;
    for (int i = 0; i < kTorrents; ++i)
        hashes.append(QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0')));
    const QJsonObject map{
        {QStringLiteral("udp://tracker.opentrackr.org:1337/announce"), hashes},
        {QStringLiteral("https://tracker.opentrackr.org:443/announce"), hashes},
    };

    TrackerModel model;
    QElapsedTimer timer;
    timer.start();
    qbtfeed::full(model, map);
    const double ms = double(timer.nsecsElapsed()) / 1e6;
    qInfo("TrackerModel rebuild, 2 URLs x %d torrents: %.1f ms", kTorrents, ms);

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.hashesFor(QStringLiteral("tracker.opentrackr.org")).size(), kTorrents);
    // The quadratic version took seconds here even in Release; linear is tens of ms in Debug.
    QVERIFY2(ms < 1000.0, qPrintable(QStringLiteral("rebuild took %1 ms").arg(ms)));
}

QTEST_MAIN(TestTrackerModel)
#include "tst_trackermodel.moc"
