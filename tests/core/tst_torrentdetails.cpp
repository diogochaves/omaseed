// TorrentDetails without a server: the General tab's strings (spec §2 Fig. 1's `.kv` block)
// formatted out of a recorded `torrents/properties`, the demo door that fills all four
// models at once, and the share limit it has to fetch from the torrent row because
// torrents/properties does not carry it.

#include "core/TorrentDetails.hpp"
#include "core/TorrentModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::TorrentDetails;
using omaseed::core::TorrentFilesModel;
using omaseed::core::TorrentModel;
using omaseed::core::TorrentPeersModel;
using omaseed::core::TorrentTrackersModel;

namespace {

QJsonDocument fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/details/") + name);
    [&]() { QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName())); }();
    return QJsonDocument::fromJson(file.readAll());
}

/// The shape TorrentDetails::applyDemo() takes, built from the fixtures.
QJsonObject demoFor(const QString &hash,
                    const QString &properties,
                    const QString &files,
                    const QString &trackers,
                    const QString &peers)
{
    QJsonObject details;
    details.insert(QStringLiteral("hash"), hash);
    details.insert(QStringLiteral("properties"), fixture(properties).object());
    details.insert(QStringLiteral("files"), fixture(files).array());
    details.insert(QStringLiteral("trackers"), fixture(trackers).array());
    details.insert(QStringLiteral("peers"), fixture(peers).object());
    return details;
}

} // namespace

class TestTorrentDetails : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void general_tab_strings_from_a_recorded_properties();
    void the_figure_ones_general_rows();
    void share_limit_comes_from_the_torrent_row();
    void apply_demo_fills_every_model();
    void selecting_another_torrent_clears_everything();
    void nothing_polls_without_a_client();
};

void TestTorrentDetails::general_tab_strings_from_a_recorded_properties()
{
    // Recorded from :8081 for tests/fixtures/multi_file_v1_unicode.torrent, added stopped.
    TorrentDetails details;
    QJsonObject demo;
    demo.insert(QStringLiteral("hash"), QStringLiteral("6f04b6c7642d15dc5cfa6cd2478e7f128a0e556f"));
    demo.insert(QStringLiteral("properties"),
                fixture(QStringLiteral("properties-unicode-pack.json")).object());
    details.applyDemo(demo);

    const QVariantMap properties = details.properties();
    QCOMPARE(properties.value(QStringLiteral("name")).toString(), QStringLiteral("Unicode Pack"));
    QCOMPARE(properties.value(QStringLiteral("hashV1")).toString(),
             QStringLiteral("6f04b6c7642d15dc5cfa6cd2478e7f128a0e556f"));
    QCOMPARE(properties.value(QStringLiteral("hashV2")).toString(), QString());
    QCOMPARE(properties.value(QStringLiteral("downloaded")).toString(),
             QStringLiteral("0 of 4 KB"));
    QCOMPARE(properties.value(QStringLiteral("uploaded")).toString(), QStringLiteral("0"));
    QCOMPARE(properties.value(QStringLiteral("pieces")).toString(),
             QStringLiteral("1 × 16 KiB · 0 have"));
    QCOMPARE(properties.value(QStringLiteral("availability")).toString(), QStringLiteral("0.00"));
    QCOMPARE(properties.value(QStringLiteral("seeds")).toString(),
             QStringLiteral("0 connected · 0 total"));
    QCOMPARE(properties.value(QStringLiteral("timeActive")).toString(), QStringLiteral("0 s"));
    // The fixture's torrent carries a creation date but no comment or creator.
    QCOMPARE(properties.value(QStringLiteral("comment")).toString(), QString());
    QVERIFY(properties.value(QStringLiteral("createdOn"))
                .toString()
                .contains(QStringLiteral("Nov 2023")));
    // No torrent model was handed over, so the limits fall back to the global ones.
    QCOMPARE(properties.value(QStringLiteral("shareLimit")).toString(), QStringLiteral("global"));
}

void TestTorrentDetails::the_figure_ones_general_rows()
{
    TorrentDetails details;
    QJsonObject demo;
    demo.insert(QStringLiteral("hash"), QStringLiteral("0000a8b4"));
    demo.insert(QStringLiteral("properties"),
                fixture(QStringLiteral("properties-fig1.json")).object());
    details.applyDemo(demo);

    const QVariantMap properties = details.properties();
    // Spec §2 Fig. 1's General tab, row by row. Byte counts print to three significant
    // digits through Format (the project's Q16 rule), so the figure's "641.2 MB" and
    // "2.1 MB" read "641 MB" and "2.10 MB" here.
    QCOMPARE(properties.value(QStringLiteral("savePath")).toString(),
             QStringLiteral("/data/torrents/linux/"));
    QCOMPARE(properties.value(QStringLiteral("hashV1")).toString(),
             QStringLiteral("3f6c9a1b7e2d4c8f0a5b6e7d8c9f0a1b2c3d4e5f"));
    QCOMPARE(properties.value(QStringLiteral("downloaded")).toString(),
             QStringLiteral("641 MB of 1.31 GB"));
    QCOMPARE(properties.value(QStringLiteral("uploaded")).toString(), QStringLiteral("98.4 MB"));
    QCOMPARE(properties.value(QStringLiteral("wasted")).toString(), QStringLiteral("2.10 MB"));
    QCOMPARE(properties.value(QStringLiteral("pieces")).toString(),
             QStringLiteral("1 264 × 1 MiB · 617 have"));
    QCOMPARE(properties.value(QStringLiteral("availability")).toString(), QStringLiteral("38.20"));
    QCOMPARE(properties.value(QStringLiteral("seeds")).toString(),
             QStringLiteral("21 connected · 1 204 total"));
    QCOMPARE(properties.value(QStringLiteral("peers")).toString(),
             QStringLiteral("42 connected · 88 total"));
    QCOMPARE(properties.value(QStringLiteral("timeActive")).toString(),
             QStringLiteral("14 min 22 s"));
    QCOMPARE(properties.value(QStringLiteral("createdBy")).toString(),
             QStringLiteral("mktorrent 1.1"));
    QCOMPARE(properties.value(QStringLiteral("comment")).toString(),
             QStringLiteral("Arch Linux 2026.09.01 release"));
    QCOMPARE(properties.value(QStringLiteral("createdOn")).toString(), QString());
}

void TestTorrentDetails::share_limit_comes_from_the_torrent_row()
{
    TorrentModel torrents;
    QJsonObject row;
    row.insert(QStringLiteral("name"), QStringLiteral("Fixture"));
    row.insert(QStringLiteral("ratio_limit"), 2.0);
    row.insert(QStringLiteral("seeding_time_limit"), -1);
    QJsonObject map;
    map.insert(QStringLiteral("abc123"), row);
    torrents.applyFullUpdate(map);

    TorrentDetails details;
    details.setTorrentModel(&torrents);

    QJsonObject demo;
    demo.insert(QStringLiteral("hash"), QStringLiteral("abc123"));
    demo.insert(QStringLiteral("properties"),
                fixture(QStringLiteral("properties-fig1.json")).object());
    details.applyDemo(demo);

    QCOMPARE(details.properties().value(QStringLiteral("shareLimit")).toString(),
             QStringLiteral("ratio 2.0"));
}

void TestTorrentDetails::apply_demo_fills_every_model()
{
    TorrentDetails details;
    QSignalSpy refreshed(&details, &TorrentDetails::refreshed);
    QSignalSpy badge(&details, &TorrentDetails::hasTrackerErrorChanged);

    details.applyDemo(demoFor(QStringLiteral("deadbeef"),
                              QStringLiteral("properties-fig1.json"),
                              QStringLiteral("files-fig2.json"),
                              QStringLiteral("trackers-fig3.json"),
                              QStringLiteral("peers-full.json")));

    QCOMPARE(details.hash(), QStringLiteral("deadbeef"));
    QCOMPARE(details.files()->count(), 7);
    QCOMPARE(details.trackers()->count(), 6);
    QCOMPARE(details.peers()->count(), 4);
    QCOMPARE(refreshed.count(), 1);

    // Spec §2 Fig. 1's `Trackers 3 ●` badge.
    QVERIFY(details.hasTrackerError());
    QCOMPARE(badge.count(), 1);
    QVERIFY(details.lastError().isEmpty());
}

void TestTorrentDetails::selecting_another_torrent_clears_everything()
{
    TorrentDetails details;
    details.applyDemo(demoFor(QStringLiteral("deadbeef"),
                              QStringLiteral("properties-fig1.json"),
                              QStringLiteral("files-fig2.json"),
                              QStringLiteral("trackers-fig3.json"),
                              QStringLiteral("peers-full.json")));

    QSignalSpy hashChanged(&details, &TorrentDetails::hashChanged);
    details.applyDemo(demoFor(QStringLiteral("cafebabe"),
                              QStringLiteral("properties-unicode-pack.json"),
                              QStringLiteral("files-unicode-pack.json"),
                              QStringLiteral("trackers-private.json"),
                              QStringLiteral("peers-diff.json")));

    QCOMPARE(hashChanged.count(), 1);
    QCOMPARE(details.files()->count(), 7);
    QCOMPARE(details.trackers()->count(), 6);
    // peers-diff is a partial update against a model that was just cleared, so only the one
    // peer it names is there; the removed one was never known.
    QCOMPARE(details.peers()->count(), 2);
    QVERIFY(!details.hasTrackerError());
}

void TestTorrentDetails::nothing_polls_without_a_client()
{
    TorrentDetails details;
    details.setPollInterval(20);
    details.setHash(QStringLiteral("deadbeef"));
    details.setPeersActive(true);
    details.setVisible(true);

    // No client: setHash is accepted, the models stay empty, and nothing crashes.
    QTest::qWait(60);
    QCOMPARE(details.hash(), QStringLiteral("deadbeef"));
    QCOMPARE(details.files()->count(), 0);
    QCOMPARE(details.properties().size(), 0);

    details.setHash(QString());
    QCOMPARE(details.hash(), QString());
}

QTEST_MAIN(TestTorrentDetails)
#include "tst_torrentdetails.moc"
