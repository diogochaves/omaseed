// TorrentFilterModel: the rail's five filters, the hero's search box, and sorting — natural
// for names, total so a speed tick never shuffles the table (spec §1).

#include "core/Torrent.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace omaseed::core;

namespace {

QJsonObject torrentJson(const QString &name,
                        const QString &state,
                        double progress,
                        const QString &category,
                        const QString &tags,
                        const QString &tracker,
                        qint64 dlspeed = 0,
                        qint64 size = 1000)
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("state"), state},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("category"), category},
        {QStringLiteral("tags"), tags},
        {QStringLiteral("tracker"), tracker},
        // One announce URL, or none; a test that needs failing trackers raises it.
        {QStringLiteral("trackers_count"), tracker.isEmpty() ? 0 : 1},
        {QStringLiteral("dlspeed"), dlspeed},
        {QStringLiteral("size"), size},
    };
}

/// Six torrents covering every rail row the spec draws.
QJsonObject library()
{
    const QString arch = QStringLiteral("udp://tracker.archlinux.org:6969/announce");
    const QString open = QStringLiteral("https://tracker.opentrackr.org:443/announce");
    return QJsonObject{
        {QStringLiteral("1111"),
         torrentJson(QStringLiteral("archlinux-2026.09.01-x86_64.iso"),
                     QStringLiteral("downloading"),
                     0.489,
                     QStringLiteral("linux"),
                     QStringLiteral("keep,private"),
                     arch,
                     1929379,
                     1404304589)},
        {QStringLiteral("2222"),
         torrentJson(QStringLiteral("Big Buck Bunny 4K"),
                     QStringLiteral("uploading"),
                     1.0,
                     QStringLiteral("movies"),
                     QStringLiteral("4k"),
                     open,
                     0,
                     4209067950)},
        {QStringLiteral("3333"),
         torrentJson(QStringLiteral("debian part 10"),
                     QStringLiteral("stalledDL"),
                     0.031,
                     QStringLiteral("linux"),
                     QString(),
                     arch,
                     0,
                     100)},
        {QStringLiteral("4444"),
         torrentJson(QStringLiteral("debian part 2"),
                     QStringLiteral("stoppedUP"),
                     1.0,
                     QString(),
                     QStringLiteral("keep"),
                     QString(),
                     0,
                     200)},
        {QStringLiteral("5555"),
         torrentJson(QStringLiteral("Elephants Dream"),
                     QStringLiteral("missingFiles"),
                     0.0,
                     QStringLiteral("movies"),
                     QString(),
                     open,
                     0,
                     300)},
        {QStringLiteral("6666"),
         torrentJson(QStringLiteral("NixOS minimal"),
                     QStringLiteral("checkingUP"),
                     1.0,
                     QStringLiteral("linux"),
                     QStringLiteral("keep"),
                     QString(),
                     0,
                     400)},
    };
}

QStringList visibleHashes(const TorrentFilterModel &proxy)
{
    QStringList list;
    for (int row = 0; row < proxy.rowCount(); ++row)
        list.append(proxy.hashAt(row));
    return list;
}

QStringList visibleNames(const TorrentFilterModel &proxy)
{
    QStringList list;
    for (int row = 0; row < proxy.rowCount(); ++row)
        list.append(proxy.data(proxy.index(row, 0), TorrentModel::NameRole).toString());
    return list;
}

} // namespace

class TestFilterModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void statusFilters_data();
    void statusFilters();
    void categoryFilterIsThreeValued();
    void tagFilterIsThreeValued();
    void trackerFilterGroupsByHostAndTrackerless();
    void trackerFilterListsWhatTheRailCounts();
    void textMatchesNameHashCategoryAndTags();
    void filtersCombine();
    void sortsNamesNaturally();
    void sortByRoleNameAndOrder();
    void sortIsStableAcrossSpeedTicks();
    void countFollowsTheFilter();

private:
    TorrentModel m_source;
    TorrentFilterModel m_proxy;
};

void TestFilterModel::init()
{
    m_source.applyFullUpdate(library());
    m_proxy.setSourceModel(&m_source);
    m_proxy.setStatusFilter(TorrentFilterModel::All);
    m_proxy.clearCategory();
    m_proxy.clearTag();
    m_proxy.clearTracker();
    m_proxy.setText(QString());
    m_proxy.setSortRole(TorrentModel::NameRole);
    m_proxy.setSortOrder(Qt::AscendingOrder);
}

void TestFilterModel::statusFilters_data()
{
    QTest::addColumn<int>("filter");
    QTest::addColumn<QStringList>("hashes");

    QTest::newRow("all") << int(TorrentFilterModel::All)
                         << QStringList({QStringLiteral("1111"),
                                         QStringLiteral("2222"),
                                         QStringLiteral("3333"),
                                         QStringLiteral("4444"),
                                         QStringLiteral("5555"),
                                         QStringLiteral("6666")});
    QTest::newRow("downloading") << int(TorrentFilterModel::Downloading)
                                 << QStringList({QStringLiteral("1111"), QStringLiteral("3333")});
    QTest::newRow("seeding") << int(TorrentFilterModel::Seeding)
                             << QStringList({QStringLiteral("2222"), QStringLiteral("6666")});
    QTest::newRow("completed") << int(TorrentFilterModel::Completed)
                               << QStringList({QStringLiteral("2222"),
                                               QStringLiteral("4444"),
                                               QStringLiteral("6666")});
    QTest::newRow("stopped") << int(TorrentFilterModel::Stopped)
                             << QStringList({QStringLiteral("4444")});
    QTest::newRow("active") << int(TorrentFilterModel::Active)
                            << QStringList({QStringLiteral("1111"), QStringLiteral("2222")});
    QTest::newRow("stalled") << int(TorrentFilterModel::Stalled)
                             << QStringList({QStringLiteral("3333")});
    QTest::newRow("checking") << int(TorrentFilterModel::Checking)
                              << QStringList({QStringLiteral("6666")});
    QTest::newRow("errored") << int(TorrentFilterModel::Errored)
                             << QStringList({QStringLiteral("5555")});
}

void TestFilterModel::statusFilters()
{
    QFETCH(int, filter);
    QFETCH(QStringList, hashes);

    m_proxy.setStatusFilter(TorrentFilterModel::StatusFilter(filter));
    QStringList visible = visibleHashes(m_proxy);
    visible.sort();
    QStringList expected = hashes;
    expected.sort();
    QCOMPARE(visible, expected);
}

void TestFilterModel::categoryFilterIsThreeValued()
{
    QCOMPARE(m_proxy.rowCount(), 6); // unset: any category

    m_proxy.setCategory(QStringLiteral("linux"));
    QCOMPARE(m_proxy.rowCount(), 3);

    m_proxy.setCategory(QString()); // the rail's "none" row
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("4444")}));

    m_proxy.clearCategory();
    QCOMPARE(m_proxy.rowCount(), 6);
    QVERIFY(!m_proxy.category().isValid());
}

void TestFilterModel::tagFilterIsThreeValued()
{
    m_proxy.setTag(QStringLiteral("keep"));
    QCOMPARE(m_proxy.rowCount(), 3);

    m_proxy.setTag(QStringLiteral("4k"));
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("2222")}));

    m_proxy.setTag(QString()); // untagged
    QStringList untagged = visibleHashes(m_proxy);
    untagged.sort();
    QCOMPARE(untagged, QStringList({QStringLiteral("3333"), QStringLiteral("5555")}));

    m_proxy.clearTag();
    QCOMPARE(m_proxy.rowCount(), 6);
}

void TestFilterModel::trackerFilterGroupsByHostAndTrackerless()
{
    m_proxy.setTracker(QStringLiteral("tracker.archlinux.org"));
    QStringList arch = visibleHashes(m_proxy);
    arch.sort();
    QCOMPARE(arch, QStringList({QStringLiteral("1111"), QStringLiteral("3333")}));

    m_proxy.setTracker(QStringLiteral("trackerless"));
    QStringList none = visibleHashes(m_proxy);
    none.sort();
    QCOMPARE(none, QStringList({QStringLiteral("4444"), QStringLiteral("6666")}));

    m_proxy.clearTracker();
    QCOMPARE(m_proxy.rowCount(), 6);
}

void TestFilterModel::trackerFilterListsWhatTheRailCounts()
{
    // 1111 announces to archlinux now and also lists opentrackr; 5555's only tracker is
    // opentrackr. 4444's trackers all fail, so its current tracker is "", but it still has an
    // opentrackr URL. The rail counts opentrackr 4; the filter lists 4. Review Q11: 4444 has
    // a tracker, so it is not trackerless; only 6666 (trackers_count 0) is.
    m_source.applyPartial(
        QJsonObject{{QStringLiteral("4444"), QJsonObject{{QStringLiteral("trackers_count"), 1}}}},
        QJsonArray());
    const QString arch = QStringLiteral("udp://tracker.archlinux.org:6969/announce");
    const QString open = QStringLiteral("https://tracker.opentrackr.org:443/announce");
    TrackerModel trackers;
    trackers.applyFullUpdate(QJsonObject{
        {arch, QJsonArray{QStringLiteral("1111"), QStringLiteral("3333")}},
        {open,
         QJsonArray{QStringLiteral("1111"),
                    QStringLiteral("2222"),
                    QStringLiteral("4444"),
                    QStringLiteral("5555")}},
    });
    trackers.setTrackerlessHashes({QStringLiteral("6666")});
    m_proxy.setTrackerModel(&trackers);

    const QString host = QStringLiteral("tracker.opentrackr.org");
    m_proxy.setTracker(host);
    QStringList shown = visibleHashes(m_proxy);
    shown.sort();
    QCOMPARE(shown, trackers.hashesFor(host));
    QCOMPARE(m_proxy.count(), 4);

    // The trackerless row lists what it counts: no tracker at all. 4444 is only under its
    // host.
    m_proxy.setTracker(trackerlessKey());
    shown = visibleHashes(m_proxy);
    shown.sort();
    QCOMPARE(shown, trackers.hashesFor(trackerlessKey()));
    QCOMPARE(shown, QStringList({QStringLiteral("6666")}));
    m_proxy.setTracker(QStringLiteral("")); // the three-valued "none" is the same row
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("6666")}));

    // A poll that drops 1111's opentrackr URL re-filters the open row.
    m_proxy.setTracker(host);
    QSignalSpy count(&m_proxy, &TorrentFilterModel::countChanged);
    trackers.applyPartial(
        QJsonObject{
            {open,
             QJsonArray{QStringLiteral("2222"), QStringLiteral("4444"), QStringLiteral("5555")}}},
        QJsonArray());
    QCOMPARE(m_proxy.count(), 3);
    QVERIFY(count.count() > 0);
    QVERIFY(!visibleHashes(m_proxy).contains(QStringLiteral("1111")));

    m_proxy.setTrackerModel(nullptr);
}

void TestFilterModel::textMatchesNameHashCategoryAndTags()
{
    m_proxy.setText(QStringLiteral("ARCHLINUX")); // case-insensitive on the name
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("1111")}));

    m_proxy.setText(QStringLiteral("22")); // hash prefix, not a substring anywhere
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("2222")}));

    m_proxy.setText(QStringLiteral("movies")); // category
    QCOMPARE(m_proxy.rowCount(), 2);

    m_proxy.setText(QStringLiteral("4k")); // tag
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("2222")}));

    m_proxy.setText(QStringLiteral("fedora 45")); // spec §7 Fig. 24
    QCOMPARE(m_proxy.rowCount(), 0);

    m_proxy.setText(QString());
    QCOMPARE(m_proxy.rowCount(), 6);
}

void TestFilterModel::filtersCombine()
{
    m_proxy.setStatusFilter(TorrentFilterModel::Completed);
    m_proxy.setCategory(QStringLiteral("linux"));
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("6666")}));

    m_proxy.setTag(QStringLiteral("4k"));
    QCOMPARE(m_proxy.rowCount(), 0);
}

void TestFilterModel::sortsNamesNaturally()
{
    m_proxy.setSortRole(TorrentModel::NameRole);
    const QStringList names = visibleNames(m_proxy);

    // Case-insensitive and numeric: "part 2" before "part 10", "Big" between "arch" and "deb".
    QCOMPARE(names,
             QStringList({QStringLiteral("archlinux-2026.09.01-x86_64.iso"),
                          QStringLiteral("Big Buck Bunny 4K"),
                          QStringLiteral("debian part 2"),
                          QStringLiteral("debian part 10"),
                          QStringLiteral("Elephants Dream"),
                          QStringLiteral("NixOS minimal")}));
}

void TestFilterModel::sortByRoleNameAndOrder()
{
    m_proxy.setSortByRoleName(QStringLiteral("size"));
    QCOMPARE(m_proxy.sortRole(), int(TorrentModel::SizeRole));
    QCOMPARE(m_proxy.sortRoleName(), QStringLiteral("size"));
    QCOMPARE(visibleHashes(m_proxy).first(), QStringLiteral("3333")); // 100 bytes

    m_proxy.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(visibleHashes(m_proxy).first(), QStringLiteral("2222")); // the biggest

    // A new role re-sorts by itself, in the order already chosen.
    m_proxy.setSortByRoleName(QStringLiteral("name"));
    QCOMPARE(visibleHashes(m_proxy).first(), QStringLiteral("6666")); // NixOS, z to a
    m_proxy.setSortByRoleName(QStringLiteral("size"));

    m_proxy.setSortByRoleName(QStringLiteral("nonsense"));
    QCOMPARE(m_proxy.sortRole(), int(TorrentModel::SizeRole)); // unchanged
}

void TestFilterModel::sortIsStableAcrossSpeedTicks()
{
    m_proxy.setSortRole(TorrentModel::NameRole);
    const QStringList before = visibleHashes(m_proxy);

    // Every torrent gets a different speed: sorted by name, nothing may move.
    m_source.applyPartial(
        QJsonObject{{QStringLiteral("1111"), QJsonObject{{QStringLiteral("dlspeed"), 5}}},
                    {QStringLiteral("2222"), QJsonObject{{QStringLiteral("dlspeed"), 4}}},
                    {QStringLiteral("3333"), QJsonObject{{QStringLiteral("dlspeed"), 3}}},
                    {QStringLiteral("4444"), QJsonObject{{QStringLiteral("dlspeed"), 2}}},
                    {QStringLiteral("5555"), QJsonObject{{QStringLiteral("dlspeed"), 1}}}},
        QJsonArray());
    QCOMPARE(visibleHashes(m_proxy), before);

    // Sorting by the speed itself does move them, and ties keep a deterministic order.
    m_proxy.setSortRole(TorrentModel::DlspeedRole);
    m_proxy.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(visibleHashes(m_proxy).first(), QStringLiteral("1111"));
    const QStringList descending = visibleHashes(m_proxy);
    m_source.applyPartial(
        QJsonObject{{QStringLiteral("6666"), QJsonObject{{QStringLiteral("upspeed"), 99}}}},
        QJsonArray());
    QCOMPARE(visibleHashes(m_proxy), descending);
}

void TestFilterModel::countFollowsTheFilter()
{
    QSignalSpy spy(&m_proxy, &TorrentFilterModel::countChanged);
    m_proxy.setStatusFilter(TorrentFilterModel::Errored);
    QCOMPARE(m_proxy.count(), 1);
    QVERIFY(spy.count() > 0);

    m_proxy.setStatusFilter(TorrentFilterModel::All);
    QCOMPARE(m_proxy.count(), 6);
}

QTEST_MAIN(TestFilterModel)
#include "tst_filtermodel.moc"
