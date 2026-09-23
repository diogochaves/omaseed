// TorrentDetails without a server: the General tab's strings (spec §2 Fig. 1's `.kv` block)
// formatted out of a recorded `torrents/properties`, the demo door that fills all four
// models at once, and the share limit it has to fetch from the torrent row because
// torrents/properties does not carry it.

#include "core/Format.hpp"
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
    void the_docs12_rows();
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
    QCOMPARE(properties.value(QStringLiteral("pieces")).toString(), QStringLiteral("1 × 16 KiB"));
    QCOMPARE(properties.value(QStringLiteral("piecesHave")).toString(), QStringLiteral("0 of 1"));
    // docs/12 G2 on a stopped torrent that never ran: -1 averages, no announce, no seeding,
    // never seen complete, and not private.
    QCOMPARE(properties.value(QStringLiteral("connections")).toString(),
             QStringLiteral("0 of 100"));
    QCOMPARE(properties.value(QStringLiteral("avgSpeed")).toString(), QString());
    QCOMPARE(properties.value(QStringLiteral("reannounce")).toString(), QString());
    QCOMPARE(properties.value(QStringLiteral("seedingTime")).toString(), QString());
    QCOMPARE(properties.value(QStringLiteral("completedOn")).toString(), QString());
    QCOMPARE(properties.value(QStringLiteral("lastSeen")).toString(), QStringLiteral("never"));
    QCOMPARE(properties.value(QStringLiteral("private")).toString(), QStringLiteral("no"));
    // No torrent row, so no magnet.
    QCOMPARE(properties.value(QStringLiteral("magnet")).toString(), QString());
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
             QStringLiteral("1 264 × 1 MiB"));
    QCOMPARE(properties.value(QStringLiteral("piecesHave")).toString(),
             QStringLiteral("617 of 1 264"));
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

void TestTorrentDetails::the_docs12_rows()
{
    TorrentModel torrents;
    QJsonObject row;
    row.insert(QStringLiteral("name"), QStringLiteral("archlinux"));
    row.insert(QStringLiteral("magnet_uri"),
               QStringLiteral("magnet:?xt=urn:btih:3f6c9a1b7e2d4c8f0a5b6e7d8c9f0a1b2c3d4e5f"
                              "&dn=archlinux&tr=https%3A%2F%2Ftracker.example.org%2Fannounce"));
    QJsonObject map;
    map.insert(QStringLiteral("0000a8b4"), row);
    torrents.applyFullUpdate(map);

    TorrentDetails details;
    details.setTorrentModel(&torrents);

    QJsonObject properties = fixture(QStringLiteral("properties-fig1.json")).object();
    QJsonObject demo;
    demo.insert(QStringLiteral("hash"), QStringLiteral("0000a8b4"));
    demo.insert(QStringLiteral("properties"), properties);
    details.applyDemo(demo);

    QVariantMap general = details.properties();
    QCOMPARE(general.value(QStringLiteral("connections")).toString(), QStringLiteral("63 of 100"));
    QCOMPARE(general.value(QStringLiteral("avgSpeed")).toString(),
             QStringLiteral("↓ 1.72 MB/s · ↑ 96 KB/s"));
    QCOMPARE(general.value(QStringLiteral("reannounce")).toString(), QStringLiteral("in 27 min"));
    QCOMPARE(general.value(QStringLiteral("lastSeen")).toString(),
             omaseed::core::Format::relativeTime(QDateTime::fromSecsSinceEpoch(1789822500)));
    QCOMPARE(general.value(QStringLiteral("private")).toString(), QStringLiteral("no"));
    // Still downloading: neither seeding time nor a completion date.
    QCOMPARE(general.value(QStringLiteral("seedingTime")).toString(), QString());
    QCOMPARE(general.value(QStringLiteral("completedOn")).toString(), QString());
    // The magnet is the torrent row's, whole; the short one is only for the eye.
    QCOMPARE(general.value(QStringLiteral("magnet")).toString(),
             row.value(QStringLiteral("magnet_uri")).toString());
    QCOMPARE(general.value(QStringLiteral("magnetText")).toString(),
             QStringLiteral("magnet:?xt=urn:btih:3f6c9a1b…"));

    // Finished and seeding on a private tracker.
    properties.insert(QStringLiteral("seeding_time"), 7260);
    properties.insert(QStringLiteral("completion_date"), 1789822500);
    properties.insert(QStringLiteral("private"), true);
    properties.insert(QStringLiteral("nb_connections_limit"), -1);
    demo.insert(QStringLiteral("properties"), properties);
    details.applyDemo(demo);
    general = details.properties();
    QCOMPARE(general.value(QStringLiteral("seedingTime")).toString(), QStringLiteral("2h 1m"));
    QVERIFY(!general.value(QStringLiteral("completedOn")).toString().isEmpty());
    QCOMPARE(general.value(QStringLiteral("private")).toString(), QStringLiteral("yes"));
    QCOMPARE(general.value(QStringLiteral("connections")).toString(), QStringLiteral("63 of ∞"));

    // A 5.x magnet with no metadata: `private` is null, and every piece count is -1, so
    // neither Private nor "never seen complete" can be said yet.
    QJsonObject magnetOnly;
    magnetOnly.insert(QStringLiteral("private"), QJsonValue::Null);
    magnetOnly.insert(QStringLiteral("is_private"), false);
    magnetOnly.insert(QStringLiteral("last_seen"), -1);
    magnetOnly.insert(QStringLiteral("pieces_num"), -1);
    magnetOnly.insert(QStringLiteral("dl_speed_avg"), -1);
    magnetOnly.insert(QStringLiteral("up_speed_avg"), -1);
    demo.insert(QStringLiteral("properties"), magnetOnly);
    details.applyDemo(demo);
    general = details.properties();
    QCOMPARE(general.value(QStringLiteral("private")).toString(), QString());
    QCOMPARE(general.value(QStringLiteral("lastSeen")).toString(), QString());
    QCOMPARE(general.value(QStringLiteral("avgSpeed")).toString(), QString());
    QCOMPARE(general.value(QStringLiteral("pieces")).toString(), QString());
    QCOMPARE(general.value(QStringLiteral("connections")).toString(), QString());
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
