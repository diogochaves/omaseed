// TorrentFilterModel: the rail's five filters, the hero's search box, and sorting — natural
// for names, total so a speed tick never shuffles the table (spec §1).

#include "QbtFeed.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
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

/// `count` idle seeds, "torrent-000" up, in hash order a shuffle of their name order so a
/// poll's rows are scattered in the proxy. Speeds start at `speedStep` times the name's number.
QJsonObject seedLibrary(int count, qint64 speedStep = 0)
{
    QJsonObject torrents;
    for (int i = 0; i < count; ++i) {
        const int n = (i * 37) % count; // 37 is prime to the counts used
        torrents.insert(QStringLiteral("h%1").arg(i, 4, 10, QLatin1Char('0')),
                        QJsonObject{
                            {QStringLiteral("name"),
                             QStringLiteral("torrent-%1").arg(n, 3, 10, QLatin1Char('0'))},
                            {QStringLiteral("state"), QStringLiteral("stalledUP")},
                            {QStringLiteral("progress"), 1.0},
                            {QStringLiteral("upspeed"), speedStep * n},
                        });
    }
    return torrents;
}

/// Whether the proxy's rows read in its sort: upspeed descending, ties by name a to z.
bool sortedBySpeedDescending(const TorrentFilterModel &proxy)
{
    for (int row = 1; row < proxy.rowCount(); ++row) {
        const QModelIndex above = proxy.index(row - 1, 0);
        const QModelIndex below = proxy.index(row, 0);
        const qint64 a = proxy.data(above, TorrentModel::UpspeedRole).toLongLong();
        const qint64 b = proxy.data(below, TorrentModel::UpspeedRole).toLongLong();
        if (a < b)
            return false;
        if (a == b && proxy.data(above, TorrentModel::NameRole).toString() >
                          proxy.data(below, TorrentModel::NameRole).toString())
            return false;
    }
    return true;
}

QSet<QString> selectedHashes(const QItemSelectionModel &selection)
{
    QSet<QString> hashes;
    for (const QModelIndex &index : selection.selectedRows())
        hashes.insert(index.data(TorrentModel::HashRole).toString());
    return hashes;
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
    void folderFilterTakesTheFoldersBelow();
    void onlyHashesKeepsThoseTorrents();
    void trackerFilterGroupsByHostAndTrackerless();
    void trackerFilterListsWhatTheRailCounts();
    void textMatchesNameHashCategoryAndTags();
    void textMatchesWordsAcrossSeparators();
    void textRanksPhraseThenOrderThenAny();
    void textRankHoldsEitherSortOrder();
    void filtersCombine();
    void sortsNamesNaturally();
    void sortByRoleNameAndOrder();
    void sortIsStableAcrossSpeedTicks();
    void tiesReadAToZEitherWay();
    void selectRowsIsOneRange();
    void countFollowsTheFilter();
    void liveSortRelayoutsOncePerPoll_data();
    void liveSortRelayoutsOncePerPoll();
    void liveSortKeepingTheOrderDoesNotRelayout();
    void heldSortStillFilters();
    void tiesFollowRenamesAndNewRows();
    void heldOrderKeepsRowsWhileValuesMove();
    void heldOrderStillFiltersAndTakesNewRows();
    void heldOrderPlacesAddsAtOnce();
    void explicitChangesApplyWhileHeld();
    void heldOrderOutlastsAPollBracket();

private:
    TorrentModel m_source;
    TorrentFilterModel m_proxy;
};

void TestFilterModel::init()
{
    qbtfeed::full(m_source, library());
    m_proxy.setSourceModel(&m_source);
    m_proxy.setStatusFilter(TorrentFilterModel::All);
    m_proxy.clearCategory();
    m_proxy.clearTag();
    m_proxy.clearTracker();
    m_proxy.setText(QString());
    m_proxy.setFolder(QString());
    m_proxy.setOnlyHashes({});
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

void TestFilterModel::folderFilterTakesTheFoldersBelow()
{
    // Board 42 F1: a picked folder lists its torrents and those of the save paths below it,
    // and only at a path-component boundary ("games-old" is not under "games").
    QJsonObject partial;
    const auto at = [&partial](const char *hash, const char *path) {
        partial.insert(QLatin1String(hash),
                       QJsonObject{{QStringLiteral("save_path"), QLatin1String(path)}});
    };
    at("1111", "/mnt/archive/games");
    at("2222", "/mnt/archive/games/handheld/");
    at("3333", "/mnt/archive/games-old");
    at("4444", "/mnt/archive");
    at("5555", "/srv/media/films");
    at("6666", "/mnt/archive/games");
    qbtfeed::partial(m_source, partial, {});

    QSignalSpy changed(&m_proxy, &TorrentFilterModel::folderChanged);
    m_proxy.setFolder(QStringLiteral("/mnt/archive/games/"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_proxy.folder(), QStringLiteral("/mnt/archive/games"));
    QStringList hashes = visibleHashes(m_proxy);
    hashes.sort();
    QCOMPARE(hashes,
             QStringList({QStringLiteral("1111"), QStringLiteral("2222"), QStringLiteral("6666")}));

    m_proxy.setFolder(QStringLiteral("/mnt/archive"));
    QCOMPARE(m_proxy.rowCount(), 5);

    // It ANDs with the others, like every rail filter.
    m_proxy.setCategory(QStringLiteral("linux"));
    hashes = visibleHashes(m_proxy);
    hashes.sort();
    QCOMPARE(hashes,
             QStringList({QStringLiteral("1111"), QStringLiteral("3333"), QStringLiteral("6666")}));

    m_proxy.setFolder(QString());
    m_proxy.clearCategory();
    QCOMPARE(m_proxy.rowCount(), 6);
}

void TestFilterModel::onlyHashesKeepsThoseTorrents()
{
    // Board 42 J1: a move's "show" lists the job's torrents and no others.
    m_proxy.setOnlyHashes({QStringLiteral("2222"), QStringLiteral("5555"), QStringLiteral("9999")});
    QStringList hashes = visibleHashes(m_proxy);
    hashes.sort();
    QCOMPARE(hashes, QStringList({QStringLiteral("2222"), QStringLiteral("5555")}));
    m_proxy.setStatusFilter(TorrentFilterModel::Errored);
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("5555")}));
    m_proxy.setStatusFilter(TorrentFilterModel::All);
    m_proxy.setOnlyHashes({});
    QCOMPARE(m_proxy.rowCount(), 6);
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
    qbtfeed::partial(
        m_source,
        QJsonObject{{QStringLiteral("4444"), QJsonObject{{QStringLiteral("trackers_count"), 1}}}},
        QJsonArray());
    const QString arch = QStringLiteral("udp://tracker.archlinux.org:6969/announce");
    const QString open = QStringLiteral("https://tracker.opentrackr.org:443/announce");
    TrackerModel trackers;
    qbtfeed::full(trackers,
                  QJsonObject{
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
    qbtfeed::partial(
        trackers,
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

void TestFilterModel::textMatchesWordsAcrossSeparators()
{
    // DECISIONS C22 (#125): `.`, `_`, `-` and spaces fold to one space on both sides.
    m_proxy.setText(QStringLiteral("archlinux 2026 09")); // the name has dots and dashes
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("1111")}));

    m_proxy.setText(QStringLiteral("x86.64")); // "x86_64": the query folds too
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("1111")}));

    m_proxy.setText(QStringLiteral("bunny big")); // every word, any order
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("2222")}));

    m_proxy.setText(QStringLiteral("bunny movies")); // a word from the name, one from the category
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("2222")}));

    m_proxy.setText(QStringLiteral("bunny sintel")); // one word missing
    QCOMPARE(m_proxy.rowCount(), 0);

    m_proxy.setText(QStringLiteral("1111")); // a hash prefix, as typed
    QCOMPARE(visibleHashes(m_proxy), QStringList({QStringLiteral("1111")}));
    m_proxy.setText(QStringLiteral("11.11")); // is never folded into one
    QCOMPARE(m_proxy.rowCount(), 0);

    m_proxy.setText(QStringLiteral(" ._- ")); // nothing left to look for: every row
    QCOMPARE(m_proxy.rowCount(), 6);
}

void TestFilterModel::textRanksPhraseThenOrderThenAny()
{
    TorrentModel source;
    const auto row = [](const QString &name, qint64 size) {
        return torrentJson(name, QStringLiteral("stoppedUP"), 1.0, {}, {}, {}, 0, size);
    };
    qbtfeed::full(
        source,
        QJsonObject{
            // Name order: a, b, c, d; size order: d, c, b, a.
            {QStringLiteral("aaaa"), row(QStringLiteral("a Bunny.Buck.Big"), 4)},       // any order
            {QStringLiteral("bbbb"), row(QStringLiteral("b Big.Buck.Bunny.1080p"), 3)}, // phrase
            {QStringLiteral("cccc"), row(QStringLiteral("c big_old_buck_bunny"), 2)},   // in order
            {QStringLiteral("dddd"), row(QStringLiteral("d Sintel"), 1)},
        });
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::SizeRole);
    proxy.setSortOrder(Qt::AscendingOrder);
    QCOMPARE(visibleHashes(proxy),
             QStringList({QStringLiteral("dddd"),
                          QStringLiteral("cccc"),
                          QStringLiteral("bbbb"),
                          QStringLiteral("aaaa")}));

    proxy.setText(QStringLiteral("big buck bunny"));
    QCOMPARE(visibleHashes(proxy),
             QStringList({QStringLiteral("bbbb"), QStringLiteral("cccc"), QStringLiteral("aaaa")}));

    // Rows of one rank keep the sort among them.
    proxy.setText(QStringLiteral("b"));
    QCOMPARE(visibleHashes(proxy),
             QStringList({QStringLiteral("cccc"), QStringLiteral("bbbb"), QStringLiteral("aaaa")}));

    // Cleared, the table's own sort is back.
    proxy.setText(QString());
    QCOMPARE(visibleHashes(proxy),
             QStringList({QStringLiteral("dddd"),
                          QStringLiteral("cccc"),
                          QStringLiteral("bbbb"),
                          QStringLiteral("aaaa")}));
}

void TestFilterModel::textRankHoldsEitherSortOrder()
{
    TorrentModel source;
    const auto row = [](const QString &name, qint64 size) {
        return torrentJson(name, QStringLiteral("stoppedUP"), 1.0, {}, {}, {}, 0, size);
    };
    qbtfeed::full(source,
                  QJsonObject{
                      {QStringLiteral("aaaa"), row(QStringLiteral("bunny big"), 1)},
                      {QStringLiteral("bbbb"), row(QStringLiteral("big.bunny"), 2)},
                      {QStringLiteral("cccc"), row(QStringLiteral("big x bunny"), 3)},
                  });
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::SizeRole);
    proxy.setText(QStringLiteral("big bunny"));
    const QStringList ranked{
        QStringLiteral("bbbb"), QStringLiteral("cccc"), QStringLiteral("aaaa")};
    proxy.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(visibleHashes(proxy), ranked);
    proxy.setSortOrder(Qt::AscendingOrder);
    QCOMPARE(visibleHashes(proxy), ranked);

    // While the pointer holds the order, a new query still ranks at once.
    proxy.setHoldOrder(true);
    proxy.setText(QStringLiteral("bunny big"));
    QCOMPARE(visibleHashes(proxy),
             QStringList({QStringLiteral("aaaa"), QStringLiteral("bbbb"), QStringLiteral("cccc")}));
    proxy.setHoldOrder(false);
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
    qbtfeed::partial(
        m_source,
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
    qbtfeed::partial(
        m_source,
        QJsonObject{{QStringLiteral("6666"), QJsonObject{{QStringLiteral("upspeed"), 99}}}},
        QJsonArray());
    QCOMPARE(visibleHashes(m_proxy), descending);
}

void TestFilterModel::tiesReadAToZEitherWay()
{
    // Five of the six are at 0 B/s: under "↓" (the default, descending) the downloading one
    // leads and the idle ones follow by name, a to z, not z to a.
    const QStringList idleByName{QStringLiteral("Big Buck Bunny 4K"),
                                 QStringLiteral("debian part 2"),
                                 QStringLiteral("debian part 10"),
                                 QStringLiteral("Elephants Dream"),
                                 QStringLiteral("NixOS minimal")};
    m_proxy.setSortRole(TorrentModel::DlspeedRole);
    m_proxy.setSortOrder(Qt::DescendingOrder);
    QStringList names = visibleNames(m_proxy);
    QCOMPARE(names.takeFirst(), QStringLiteral("archlinux-2026.09.01-x86_64.iso"));
    QCOMPARE(names, idleByName);

    // Ascending, the ties still read a to z, and the downloading one goes last.
    m_proxy.setSortOrder(Qt::AscendingOrder);
    names = visibleNames(m_proxy);
    QCOMPARE(names.takeLast(), QStringLiteral("archlinux-2026.09.01-x86_64.iso"));
    QCOMPARE(names, idleByName);
}

void TestFilterModel::selectRowsIsOneRange()
{
    QItemSelectionModel selection(&m_proxy);
    QSignalSpy changed(&selection, &QItemSelectionModel::selectionChanged);
    m_proxy.selectRows(&selection, 0, m_proxy.rowCount() - 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(selection.selectedRows().size(), m_proxy.rowCount());
    QCOMPARE(selection.selection().size(), 1); // one range, not one per row

    // Either order, clamped to the rows there are; added to what is selected.
    selection.clearSelection();
    m_proxy.selectRows(&selection, 4, 2);
    QCOMPARE(selection.selectedRows().size(), 3);
    m_proxy.selectRows(&selection, -5, 0);
    QCOMPARE(selection.selectedRows().size(), 4);
    m_proxy.selectRows(&selection, 5, 99);
    QCOMPARE(selection.selectedRows().size(), 5);

    // `replace` puts the range in place of what is selected, in one change: a shift+click.
    changed.clear();
    m_proxy.selectRows(&selection, 3, 2, true);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(selection.selectedRows().size(), 2);
    QVERIFY(selection.isRowSelected(2) && selection.isRowSelected(3));
    QVERIFY(!selection.isRowSelected(0));

    // A selection model over another model is left alone.
    QItemSelectionModel other(&m_source);
    m_proxy.selectRows(&other, 0, 3);
    QVERIFY(!other.hasSelection());
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

void TestFilterModel::liveSortRelayoutsOncePerPoll_data()
{
    QTest::addColumn<int>("busy");
    QTest::newRow("one") << 1;
    QTest::newRow("two") << 2;
    QTest::newRow("ten") << 10;
    QTest::newRow("sixty") << 60;
    QTest::newRow("all") << 200; // one run: Qt's own re-sort
}

void TestFilterModel::liveSortRelayoutsOncePerPoll()
{
    // Sorted by upload speed, a poll moving the speed of scattered rows re-sorts once, not
    // once per run of changed rows (#48), and the selection follows its torrents.
    QFETCH(int, busy);
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(200));
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    QItemSelectionModel selection(&proxy);
    proxy.selectRows(&selection, 0, 19);
    const QSet<QString> selected = selectedHashes(selection);

    QSignalSpy layouts(&proxy, &QAbstractItemModel::layoutChanged);
    for (int poll = 1; poll <= 3; ++poll) {
        QJsonObject diff;
        for (int i = 0; i < busy; ++i) {
            const int row = (i * 200 / busy + poll) % 200; // spread over the source rows
            diff.insert(
                source.torrentAt(row).hash,
                QJsonObject{{QStringLiteral("upspeed"), (i * 7919 + poll * 13) % 5000 + 1}});
        }
        layouts.clear();
        qbtfeed::partial(source, diff, QJsonArray());
        QVERIFY2(layouts.count() <= 1, qPrintable(QString::number(layouts.count())));
        QVERIFY(sortedBySpeedDescending(proxy));
        QCOMPARE(selectedHashes(selection), selected);
        QCOMPARE(proxy.rowCount(), 200);
        QVERIFY(proxy.dynamicSortFilter());
    }
}

void TestFilterModel::liveSortKeepingTheOrderDoesNotRelayout()
{
    // Speeds a step apart, each nudged by less than a step: the order stands, so no layout
    // change reaches the table, held or not.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(100, 10));
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    const QStringList before = visibleHashes(proxy);

    QSignalSpy layouts(&proxy, &QAbstractItemModel::layoutChanged);
    QSignalSpy changed(&proxy, &QAbstractItemModel::dataChanged);
    QJsonObject diff;
    for (int row = 0; row < 100; row += 3) {
        const Torrent &t = source.torrentAt(row);
        diff.insert(t.hash, QJsonObject{{QStringLiteral("upspeed"), t.upspeed + 3}});
    }
    qbtfeed::partial(source, diff, QJsonArray());
    QCOMPARE(layouts.count(), 0);
    QVERIFY(changed.count() > 0);
    QCOMPARE(visibleHashes(proxy), before);
}

void TestFilterModel::heldSortStillFilters()
{
    // Under "active", one poll starts scattered idle seeds and stops busy ones: while the sort
    // is held the rows still come and go, in order.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(120));
    QJsonObject start;
    for (int row = 0; row < 120; row += 4)
        start.insert(source.torrentAt(row).hash, QJsonObject{{QStringLiteral("upspeed"), row + 1}});
    qbtfeed::partial(source, start, QJsonArray());

    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setStatusFilter(TorrentFilterModel::Active);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(proxy.rowCount(), 30);

    QSignalSpy layouts(&proxy, &QAbstractItemModel::layoutChanged);
    QJsonObject diff;
    QSet<QString> expected;
    for (int row = 0; row < 120; ++row) {
        const QString hash = source.torrentAt(row).hash;
        if (row % 8 == 0) {
            diff.insert(hash, QJsonObject{{QStringLiteral("upspeed"), 0}}); // stops
        } else if (row % 4 == 0) {
            diff.insert(hash, QJsonObject{{QStringLiteral("upspeed"), 500 - row}}); // reorders
            expected.insert(hash);
        } else if (row % 10 == 1) {
            diff.insert(hash, QJsonObject{{QStringLiteral("upspeed"), 1000 + row}}); // starts
            expected.insert(hash);
        }
    }
    qbtfeed::partial(source, diff, QJsonArray());
    QVERIFY(layouts.count() <= 1);
    const QStringList visible = visibleHashes(proxy);
    QCOMPARE(QSet<QString>(visible.cbegin(), visible.cend()), expected);
    QVERIFY(sortedBySpeedDescending(proxy));
}

void TestFilterModel::tiesFollowRenamesAndNewRows()
{
    // The name tie-break is cached; a rename or a new torrent must not read a stale rank.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(50));
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(visibleNames(proxy).first(), QStringLiteral("torrent-000"));

    // A rename alone, every speed tied at 0.
    const QString first = proxy.hashAt(0);
    qbtfeed::partial(
        source,
        QJsonObject{{first, QJsonObject{{QStringLiteral("name"), QStringLiteral("zz last")}}}},
        QJsonArray());
    QCOMPARE(proxy.hashAt(proxy.rowCount() - 1), first);
    QVERIFY(sortedBySpeedDescending(proxy));

    // A rename in a poll that also re-sorts scattered rows (the held sort).
    const QString second = proxy.hashAt(0);
    QJsonObject diff;
    for (int row = 0; row < 50; row += 5)
        diff.insert(source.torrentAt(row).hash, QJsonObject{{QStringLiteral("upspeed"), row + 1}});
    diff.insert(second, QJsonObject{{QStringLiteral("name"), QStringLiteral("zzz later")}});
    qbtfeed::partial(source, diff, QJsonArray());
    QVERIFY(sortedBySpeedDescending(proxy));
    QCOMPARE(proxy.hashAt(proxy.rowCount() - 1), second);

    // A new torrent lands among the ties by name.
    qbtfeed::partial(
        source,
        QJsonObject{{QStringLiteral("new"),
                     QJsonObject{{QStringLiteral("name"), QStringLiteral("aaa new")},
                                 {QStringLiteral("state"), QStringLiteral("stalledUP")}}}},
        QJsonArray());
    QVERIFY(sortedBySpeedDescending(proxy));
    QCOMPARE(proxy.rowCount(), 51);
}

namespace {

/// Every row's upload speed turned around (`base` less it), so a sort by speed turns the
/// table upside down.
QJsonObject reverseSpeeds(const TorrentModel &source, qint64 base)
{
    QJsonObject diff;
    for (int row = 0; row < source.rowCount(); ++row) {
        const Torrent &t = source.torrentAt(row);
        diff.insert(t.hash, QJsonObject{{QStringLiteral("upspeed"), base - t.upspeed}});
    }
    return diff;
}

} // namespace

void TestFilterModel::heldOrderKeepsRowsWhileValuesMove()
{
    // #51 D9: while the pointer is over the rows, a poll that re-sorts by speed changes the
    // values but not the order; letting go sorts once, and the selection follows its torrents.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(60, 10));
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    QItemSelectionModel selection(&proxy);
    proxy.selectRows(&selection, 0, 4);
    const QSet<QString> selected = selectedHashes(selection);
    const QStringList before = visibleHashes(proxy);

    QSignalSpy holds(&proxy, &TorrentFilterModel::holdOrderChanged);
    proxy.setHoldOrder(true);
    QVERIFY(proxy.holdOrder());
    QCOMPARE(holds.count(), 1);

    QSignalSpy layouts(&proxy, &QAbstractItemModel::layoutChanged);
    QSignalSpy changed(&proxy, &QAbstractItemModel::dataChanged);
    qbtfeed::partial(source, reverseSpeeds(source, 1000), QJsonArray());
    QCOMPARE(visibleHashes(proxy), before);
    QVERIFY(changed.count() > 0);
    QCOMPARE(layouts.count(), 0);
    // The values did change: the top row is now the slowest.
    QCOMPARE(proxy.data(proxy.index(0, 0), TorrentModel::UpspeedRole).toLongLong(), 1000 - 590);

    proxy.setHoldOrder(false);
    QCOMPARE(layouts.count(), 1);
    QVERIFY(sortedBySpeedDescending(proxy));
    QVERIFY(visibleHashes(proxy) != before);
    QCOMPARE(selectedHashes(selection), selected);
    QVERIFY(proxy.dynamicSortFilter());
}

void TestFilterModel::heldOrderStillFiltersAndTakesNewRows()
{
    // Held under "active": a torrent that stops leaves, one that starts comes in below the
    // held rows (nothing above it moves), and a new torrent takes its sorted place at once
    // (DECISIONS C20); on release all sort.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(40, 10)); // torrent-000 idle, the rest busy
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setStatusFilter(TorrentFilterModel::Active);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    QCOMPARE(proxy.rowCount(), 39);
    QStringList before = visibleHashes(proxy);
    proxy.setHoldOrder(true);

    const QString stopping = before.at(10);
    QString starting;
    for (int row = 0; row < source.rowCount(); ++row) {
        if (source.torrentAt(row).upspeed == 0)
            starting = source.torrentAt(row).hash;
    }
    QVERIFY(!starting.isEmpty());
    qbtfeed::partial(source,
                     QJsonObject{
                         {stopping, QJsonObject{{QStringLiteral("upspeed"), 0}}},
                         {starting, QJsonObject{{QStringLiteral("upspeed"), 99999}}},
                         {QStringLiteral("fresh"),
                          QJsonObject{{QStringLiteral("name"), QStringLiteral("fresh")},
                                      {QStringLiteral("state"), QStringLiteral("uploading")},
                                      {QStringLiteral("progress"), 1.0},
                                      {QStringLiteral("upspeed"), 88888}}},
                     },
                     QJsonArray());

    before.removeOne(stopping);
    const QStringList held = visibleHashes(proxy);
    QCOMPARE(held.size(), 40);
    // The fastest of all, so above every held row; the starting one below them.
    QCOMPARE(held.first(), QStringLiteral("fresh"));
    QCOMPARE(held.mid(1, 38), before);
    QCOMPARE(held.last(), starting);

    proxy.setHoldOrder(false);
    QVERIFY(sortedBySpeedDescending(proxy));
    QCOMPARE(proxy.hashAt(0), starting);
    QCOMPARE(proxy.hashAt(1), QStringLiteral("fresh"));
    QCOMPARE(proxy.rowOfHash(stopping), -1);
}

void TestFilterModel::heldOrderPlacesAddsAtOnce()
{
    // #124 (DECISIONS C20): sorted by date added, newest first, with the pointer over the
    // rows. Adds one after another each land where the sort puts them on the poll that brings
    // them, never at the end first; the held rows keep their order around them.
    const auto torrent = [](const QString &name, qint64 added, qint64 upspeed = 0) {
        return QJsonObject{{QStringLiteral("name"), name},
                           {QStringLiteral("state"), QStringLiteral("stalledUP")},
                           {QStringLiteral("progress"), 1.0},
                           {QStringLiteral("added_on"), added},
                           {QStringLiteral("upspeed"), upspeed}};
    };
    TorrentModel source;
    QJsonObject library;
    for (int i = 0; i < 20; ++i)
        library.insert(QStringLiteral("h%1").arg(i, 2, 10, QLatin1Char('0')),
                       torrent(QStringLiteral("old-%1").arg(i), 1000 + 10 * i));
    qbtfeed::full(source, library);
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::AddedOnRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    const QStringList before = visibleHashes(proxy);
    QCOMPARE(before.first(), QStringLiteral("h19"));
    proxy.setHoldOrder(true);

    // Three adds, one poll each: every one is the newest, so each goes to the top.
    for (int n = 1; n <= 3; ++n) {
        const QString hash = QStringLiteral("new%1").arg(n);
        qbtfeed::partial(source, QJsonObject{{hash, torrent(hash, 2000 + n)}}, QJsonArray());
        QCOMPARE(proxy.hashAt(0), hash);
        QCOMPARE(proxy.rowCount(), 20 + n);
    }
    QCOMPARE(visibleHashes(proxy).mid(3), before);

    // One added between two held rows (an older date) goes between them.
    qbtfeed::partial(source,
                     QJsonObject{{QStringLiteral("mid"), torrent(QStringLiteral("mid"), 1055)}},
                     QJsonArray());
    const int at = proxy.rowOfHash(QStringLiteral("mid"));
    QCOMPARE(proxy.hashAt(at - 1), QStringLiteral("h06"));
    QCOMPARE(proxy.hashAt(at + 1), QStringLiteral("h05"));
    // Two in one poll, the same slot: by their own order.
    qbtfeed::partial(source,
                     QJsonObject{{QStringLiteral("a"), torrent(QStringLiteral("a"), 3001)},
                                 {QStringLiteral("b"), torrent(QStringLiteral("b"), 3002)}},
                     QJsonArray());
    QCOMPARE(proxy.hashAt(0), QStringLiteral("b"));
    QCOMPARE(proxy.hashAt(1), QStringLiteral("a"));

    // The held rows still hold while their values move (sorted by speed now, ascending).
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::AscendingOrder);
    const QStringList bySpeed = visibleHashes(proxy);
    QJsonObject faster;
    for (int i = 0; i < 20; ++i)
        faster.insert(QStringLiteral("h%1").arg(i, 2, 10, QLatin1Char('0')),
                      QJsonObject{{QStringLiteral("upspeed"), 100 - i}});
    faster.insert(QStringLiteral("quick"), torrent(QStringLiteral("quick"), 4000, 1000000));
    qbtfeed::partial(source, faster, QJsonArray());
    QStringList held = visibleHashes(proxy);
    QCOMPARE(held.last(), QStringLiteral("quick")); // the fastest: last, ascending
    held.removeLast();
    QCOMPARE(held, bySpeed);

    proxy.setHoldOrder(false);
    QCOMPARE(proxy.hashAt(proxy.rowCount() - 1), QStringLiteral("quick"));
}

void TestFilterModel::explicitChangesApplyWhileHeld()
{
    // A new sort order, sort role or filter, asked for while the order is held, applies at
    // once; the hold then starts again from the new order.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(30, 10));
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    proxy.setHoldOrder(true);
    qbtfeed::partial(source, reverseSpeeds(source, 1000), QJsonArray());
    QVERIFY(!sortedBySpeedDescending(proxy));

    // The direction flips twice: back where it was, but sorted by the new speeds.
    proxy.setSortOrder(Qt::AscendingOrder);
    proxy.setSortOrder(Qt::DescendingOrder);
    QVERIFY(sortedBySpeedDescending(proxy));
    QVERIFY(proxy.holdOrder());
    // Still held: the next poll moves nothing.
    const QStringList afterSort = visibleHashes(proxy);
    qbtfeed::partial(source, reverseSpeeds(source, 1000), QJsonArray());
    QCOMPARE(visibleHashes(proxy), afterSort);

    // The filter text: sorted at once, and held again. (One word, so every match ranks the
    // same and the speed sort shows: 001 and 010 to 019.)
    proxy.setText(QStringLiteral("01"));
    QCOMPARE(proxy.rowCount(), 11);
    QVERIFY(sortedBySpeedDescending(proxy));
    const QStringList afterText = visibleHashes(proxy);
    qbtfeed::partial(source, reverseSpeeds(source, 1000), QJsonArray());
    QCOMPARE(visibleHashes(proxy), afterText);

    // A rail scope, likewise.
    proxy.setText(QString());
    proxy.setStatusFilter(TorrentFilterModel::Seeding);
    QCOMPARE(proxy.rowCount(), 30);
    QVERIFY(sortedBySpeedDescending(proxy));

    // The sort role: names, a to z.
    proxy.setSortRole(TorrentModel::NameRole);
    proxy.setSortOrder(Qt::AscendingOrder);
    QCOMPARE(visibleNames(proxy).first(), QStringLiteral("torrent-000"));
    QCOMPARE(visibleNames(proxy).last(), QStringLiteral("torrent-029"));
}

void TestFilterModel::heldOrderOutlastsAPollBracket()
{
    // A poll scattered over many runs takes #48's bracket; the hover hold lasts through the
    // bracket's own sort, and the release still sorts once with the selection following.
    TorrentModel source;
    qbtfeed::full(source, seedLibrary(200));
    TorrentFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.setSortRole(TorrentModel::UpspeedRole);
    proxy.setSortOrder(Qt::DescendingOrder);
    QItemSelectionModel selection(&proxy);
    proxy.selectRows(&selection, 3, 9);
    const QSet<QString> selected = selectedHashes(selection);
    const QStringList before = visibleHashes(proxy);
    proxy.setHoldOrder(true);

    QSignalSpy layouts(&proxy, &QAbstractItemModel::layoutChanged);
    for (int poll = 1; poll <= 3; ++poll) {
        QJsonObject diff;
        for (int i = 0; i < 60; ++i) {
            const int row = (i * 200 / 60 + poll) % 200;
            diff.insert(
                source.torrentAt(row).hash,
                QJsonObject{{QStringLiteral("upspeed"), (i * 7919 + poll * 13) % 5000 + 1}});
        }
        qbtfeed::partial(source, diff, QJsonArray());
        QCOMPARE(visibleHashes(proxy), before);
        QVERIFY(proxy.dynamicSortFilter());
    }
    QCOMPARE(layouts.count(), 0);

    proxy.setHoldOrder(false);
    QCOMPARE(layouts.count(), 1);
    QVERIFY(sortedBySpeedDescending(proxy));
    QCOMPARE(selectedHashes(selection), selected);
}

QTEST_MAIN(TestFilterModel)
#include "tst_filtermodel.moc"
