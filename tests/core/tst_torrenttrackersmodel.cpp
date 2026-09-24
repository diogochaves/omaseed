// TorrentTrackersModel: host stripping, the pseudo-trackers, the health dot and the Status
// column's wording (spec §2 Fig. 3). Fixtures are one recording from the :8081 playground
// (a private torrent, three tiers, DHT/PeX/LSD disabled) and one hand-built from the WebUI
// API docs with the figure's own statuses.

#include "core/TorrentTrackersModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::TorrentTrackersModel;
using omaseed::core::TrackerStatus;

namespace {

QJsonArray fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/details/") + name);
    [&]() { QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName())); }();
    return QJsonDocument::fromJson(file.readAll()).array();
}

QVariant at(const TorrentTrackersModel &model, int row, TorrentTrackersModel::Role role)
{
    return model.data(model.index(row), role);
}

} // namespace

class TestTorrentTrackersModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void hosts_drop_the_protocol_prefix();
    void pseudo_trackers_keep_their_api_names();
    void a_shared_host_shows_its_ports();
    void status_text_says_what_the_figure_says();
    void the_figure_row_by_row();
    void the_playground_recording();
    void error_badge_ignores_the_pseudo_trackers();
    void a_same_size_update_does_not_reset();
};

void TestTorrentTrackersModel::hosts_drop_the_protocol_prefix()
{
    QCOMPARE(
        TorrentTrackersModel::hostOf(QStringLiteral("udp://tracker.example.net:1337/announce")),
        QStringLiteral("tracker.example.net"));
    QCOMPARE(TorrentTrackersModel::hostOf(QStringLiteral("https://tracker.example.org/announce")),
             QStringLiteral("tracker.example.org"));
    QCOMPARE(TorrentTrackersModel::hostOf(QStringLiteral("http://tracker.example.com/announce")),
             QStringLiteral("tracker.example.com"));
    // Nothing recognisable comes back untouched rather than empty.
    QCOMPARE(TorrentTrackersModel::hostOf(QStringLiteral("not a url")),
             QStringLiteral("not a url"));
}

void TestTorrentTrackersModel::a_shared_host_shows_its_ports()
{
    auto entry = [](const QString &url) {
        return QJsonObject{{QStringLiteral("url"), url},
                           {QStringLiteral("tier"), 0},
                           {QStringLiteral("status"), int(TrackerStatus::Working)}};
    };
    TorrentTrackersModel model;
    model.applyTrackers(QJsonArray{
        entry(QStringLiteral("** [DHT] **")),
        entry(QStringLiteral("http://127.0.0.1:26996/announce")),
        entry(QStringLiteral("http://127.0.0.1:9/announce")),
        entry(QStringLiteral("udp://tracker.example.net:1337/announce")),
    });
    QCOMPARE(model.count(), 4);
    QCOMPARE(at(model, 0, TorrentTrackersModel::HostRole).toString(), QStringLiteral("DHT"));
    QCOMPARE(at(model, 1, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("127.0.0.1:26996"));
    QCOMPARE(at(model, 2, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("127.0.0.1:9"));
    // A host on its own keeps the figure's short form, port or not.
    QCOMPARE(at(model, 3, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("tracker.example.net"));
}

void TestTorrentTrackersModel::pseudo_trackers_keep_their_api_names()
{
    QVERIFY(TorrentTrackersModel::isPseudoTracker(QStringLiteral("** [DHT] **")));
    QVERIFY(TorrentTrackersModel::isPseudoTracker(QStringLiteral("** [PeX] **")));
    QVERIFY(TorrentTrackersModel::isPseudoTracker(QStringLiteral("** [LSD] **")));
    QVERIFY(!TorrentTrackersModel::isPseudoTracker(
        QStringLiteral("udp://tracker.example.net:1337/announce")));

    QCOMPARE(TorrentTrackersModel::hostOf(QStringLiteral("** [DHT] **")), QStringLiteral("DHT"));
    QCOMPARE(TorrentTrackersModel::hostOf(QStringLiteral("** [PeX] **")), QStringLiteral("PeX"));
    QCOMPARE(TorrentTrackersModel::hostOf(QStringLiteral("** [LSD] **")), QStringLiteral("LSD"));
}

void TestTorrentTrackersModel::status_text_says_what_the_figure_says()
{
    using M = TorrentTrackersModel;
    QCOMPARE(M::statusText(int(TrackerStatus::Working), QString(), 1620),
             QStringLiteral("working · 27 min"));
    QCOMPARE(M::statusText(int(TrackerStatus::Working), QString(), 0), QStringLiteral("working"));
    QCOMPARE(M::statusText(int(TrackerStatus::NotWorking), QStringLiteral("timed out"), 240),
             QStringLiteral("timed out · retry 4 min"));
    QCOMPARE(M::statusText(int(TrackerStatus::NotWorking), QString(), 0),
             QStringLiteral("not working"));
    QCOMPARE(M::statusText(int(TrackerStatus::NotContacted), QString(), 0),
             QStringLiteral("not contacted"));
    QCOMPARE(M::statusText(int(TrackerStatus::Updating), QString(), 0), QStringLiteral("updating"));
    QCOMPARE(M::statusText(int(TrackerStatus::Disabled), QString(), 0), QStringLiteral("disabled"));
    // The server's own words win over the generic wording.
    QCOMPARE(
        M::statusText(int(TrackerStatus::Disabled), QStringLiteral("This torrent is private"), 0),
        QStringLiteral("This torrent is private"));
}

void TestTorrentTrackersModel::the_figure_row_by_row()
{
    TorrentTrackersModel model;
    model.applyTrackers(fixture(QStringLiteral("trackers-fig3.json")));
    QCOMPARE(model.count(), 6);

    QCOMPARE(at(model, 0, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("tracker.example.org"));
    QCOMPARE(at(model, 0, TorrentTrackersModel::SeedsRole).toInt(), 1204);
    QCOMPARE(at(model, 0, TorrentTrackersModel::LeechesRole).toInt(), 88);
    QCOMPARE(at(model, 0, TorrentTrackersModel::StatusTextRole).toString(),
             QStringLiteral("working · 27 min"));
    QCOMPARE(at(model, 0, TorrentTrackersModel::HealthyRole).toBool(), true);
    QCOMPARE(at(model, 0, TorrentTrackersModel::DotRoleRole).toString(), QStringLiteral("green"));

    QCOMPARE(at(model, 1, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("tracker.example.net"));
    QCOMPARE(at(model, 1, TorrentTrackersModel::StatusTextRole).toString(),
             QStringLiteral("timed out · retry 4 min"));
    QCOMPARE(at(model, 1, TorrentTrackersModel::HealthyRole).toBool(), false);
    QCOMPARE(at(model, 1, TorrentTrackersModel::DotRoleRole).toString(), QStringLiteral("yellow"));
    QCOMPARE(at(model, 1, TorrentTrackersModel::SeedsRole).toInt(), -1);

    QCOMPARE(at(model, 2, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("open.example.si"));
    QCOMPARE(at(model, 2, TorrentTrackersModel::StatusTextRole).toString(),
             QStringLiteral("working"));

    QCOMPARE(at(model, 3, TorrentTrackersModel::HostRole).toString(), QStringLiteral("DHT"));
    QCOMPARE(at(model, 3, TorrentTrackersModel::IsPseudoRole).toBool(), true);
    QCOMPARE(at(model, 3, TorrentTrackersModel::SeedsRole).toInt(), 22);
    // A pseudo-tracker is dimmed whatever it reports (spec §2 Fig. 3 `.dot.off`).
    QCOMPARE(at(model, 3, TorrentTrackersModel::DotRoleRole).toString(),
             QStringLiteral("dimForeground"));
    QCOMPARE(at(model, 4, TorrentTrackersModel::HostRole).toString(), QStringLiteral("PeX"));
    QCOMPARE(at(model, 5, TorrentTrackersModel::StatusTextRole).toString(),
             QStringLiteral("disabled"));

    // The full URL stays for the tooltip and for `y` copy.
    QCOMPARE(model.urlAt(1), QStringLiteral("udp://tracker.example.net:1337/announce"));
    QCOMPARE(model.urlAt(99), QString());
}

void TestTorrentTrackersModel::the_playground_recording()
{
    TorrentTrackersModel model;
    model.applyTrackers(fixture(QStringLiteral("trackers-private.json")));
    QCOMPARE(model.count(), 6);

    // qBittorrent lists the three pseudo-trackers first, then the announce URLs by tier.
    QCOMPARE(at(model, 0, TorrentTrackersModel::HostRole).toString(), QStringLiteral("DHT"));
    QCOMPARE(at(model, 0, TorrentTrackersModel::MsgRole).toString(),
             QStringLiteral("This torrent is private"));
    QCOMPARE(at(model, 3, TorrentTrackersModel::HostRole).toString(),
             QStringLiteral("tracker1.example.com"));
    QCOMPARE(at(model, 3, TorrentTrackersModel::TierRole).toInt(), 0);
    QCOMPARE(at(model, 4, TorrentTrackersModel::TierRole).toInt(), 1);
    QCOMPARE(at(model, 3, TorrentTrackersModel::StatusTextRole).toString(),
             QStringLiteral("not contacted"));
    QVERIFY(!model.hasError());
}

void TestTorrentTrackersModel::error_badge_ignores_the_pseudo_trackers()
{
    TorrentTrackersModel model;
    QVERIFY(!model.hasError());
    model.applyTrackers(fixture(QStringLiteral("trackers-fig3.json")));
    QVERIFY(model.hasError()); // the timed-out announce URL badges the tab

    model.clear();
    QCOMPARE(model.count(), 0);
    QVERIFY(!model.hasError());
}

void TestTorrentTrackersModel::a_same_size_update_does_not_reset()
{
    TorrentTrackersModel model;
    model.applyTrackers(fixture(QStringLiteral("trackers-fig3.json")));

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    // The same list again is a no-op; one row moving is a dataChanged, never a reset.
    model.applyTrackers(fixture(QStringLiteral("trackers-fig3.json")));
    QCOMPARE(changed.count(), 0);

    QJsonArray moved = fixture(QStringLiteral("trackers-fig3.json"));
    QJsonObject first = moved.at(0).toObject();
    first.insert(QStringLiteral("num_seeds"), 1205);
    moved.replace(0, first);
    model.applyTrackers(moved);

    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(at(model, 0, TorrentTrackersModel::SeedsRole).toInt(), 1205);
}

QTEST_MAIN(TestTorrentTrackersModel)
#include "tst_torrenttrackersmodel.moc"
