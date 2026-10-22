// TorrentFilesModel against recorded `torrents/files` answers: the tree it builds out of
// slash-separated names, the aggregates a folder shows, the fold state, and the in-place
// update that keeps the rows (and the cursor) while the numbers move.

#include "core/TorrentFilesModel.hpp"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::FilePriority;
using omaseed::core::TorrentFilesModel;

namespace {

QJsonArray fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/details/") + name);
    [&]() { QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName())); }();
    return QJsonDocument::fromJson(file.readAll()).array();
}

QVariant at(const TorrentFilesModel &model, int row, TorrentFilesModel::Role role)
{
    return model.data(model.index(row), role);
}

} // namespace

class TestTorrentFilesModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void builds_the_tree_from_slash_separated_names();
    void folders_aggregate_size_progress_and_priority();
    void priority_text_covers_every_value();
    void folding_hides_the_subtree_and_survives_a_refresh();
    void updates_in_place_when_the_shape_is_unchanged();
    void a_new_shape_resets_the_model();
    void unicode_paths_from_the_playground();
    void raise_and_lower_walk_the_ladder();
    void empty_input_clears();
    void sorting_by_name_keeps_the_tree_and_puts_folders_first();
    void sorting_by_a_number_mixes_folders_and_files();
    void a_poll_that_reorders_moves_rows_instead_of_resetting();
    void a_long_list_reorders_in_one_layout_change();
    void a_priority_on_every_file_of_a_huge_torrent_is_linear();
    void sort_by_flips_the_current_key_and_picks_a_direction_for_a_new_one();
    void a_header_cycle_goes_back_to_the_torrent_order();
    void the_filter_keeps_the_folders_of_a_match();
    void a_folder_acts_on_the_files_the_filter_shows();
    void remaining_availability_and_pieces_add_up();
    void a_local_priority_reaggregates_the_folders();
    void selection_targets_its_files_and_survives_a_refresh();
    void shown_order_splits_the_files_in_thirds();
    void a_local_rename_moves_the_subtree();
    void the_server_path_follows_the_content_path();
    void the_summary_counts_files_folders_size_and_skips();
    void select_all_files_takes_what_the_filter_shows();
    void the_filter_takes_wildcards_and_alternatives_data();
    void the_filter_takes_wildcards_and_alternatives();
    void a_wildcard_filter_keeps_the_folders_of_a_match();
    void hiding_skipped_files_drops_them_and_their_empty_folders();
    void a_priority_change_hides_or_shows_a_file_at_once();
    void hiding_skipped_files_is_a_preference_not_the_torrents();
    void rename_targets_are_the_selected_files_else_every_shown_one();
    void the_tick_reads_wanted_over_each_subtree();
    void the_head_tick_covers_what_the_filter_shows();
    void on_disk_shows_what_skipped_files_hold();
};

void TestTorrentFilesModel::builds_the_tree_from_slash_separated_names()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));

    // Spec §2 Fig. 2, row for row.
    QCOMPARE(model.count(), 7);
    QCOMPARE(model.fileCount(), 5);

    const QStringList names{QStringLiteral("Big Buck Bunny 4K/"),
                            QStringLiteral("bbb_sunflower_2160p_60fps.mp4"),
                            QStringLiteral("bbb_sunflower_1080p_30fps.mp4"),
                            QStringLiteral("subs/"),
                            QStringLiteral("bbb.en.srt"),
                            QStringLiteral("bbb.pt.srt"),
                            QStringLiteral("LICENSE.txt")};
    const QList<int> depths{0, 1, 1, 1, 2, 2, 1};
    const QList<bool> folders{true, false, false, true, false, false, false};

    for (int row = 0; row < model.count(); ++row) {
        QCOMPARE(at(model, row, TorrentFilesModel::NameRole).toString(), names.at(row));
        QCOMPARE(at(model, row, TorrentFilesModel::DepthRole).toInt(), depths.at(row));
        QCOMPARE(at(model, row, TorrentFilesModel::IsFolderRole).toBool(), folders.at(row));
    }

    // A file's path is the whole path inside the torrent; `o` opens its folder from it.
    QCOMPARE(at(model, 4, TorrentFilesModel::PathRole).toString(),
             QStringLiteral("Big Buck Bunny 4K/subs/bbb.en.srt"));
}

void TestTorrentFilesModel::folders_aggregate_size_progress_and_priority()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));

    // subs/ holds two skipped files of 9 KB each, neither downloaded.
    QCOMPARE(at(model, 3, TorrentFilesModel::SizeTextRole).toString(), QStringLiteral("18 KB"));
    QCOMPARE(at(model, 3, TorrentFilesModel::ProgressTextRole).toString(), QStringLiteral("0 %"));
    QCOMPARE(at(model, 3, TorrentFilesModel::PriorityRole).toInt(), int(FilePriority::Skip));
    QCOMPARE(at(model, 3, TorrentFilesModel::PriorityTextRole).toString(), QStringLiteral("skip"));

    // The root mixes high, normal and skip, so it shows "mixed" (spec §2 Fig. 2's rule).
    QCOMPARE(at(model, 0, TorrentFilesModel::PriorityRole).toInt(), int(FilePriority::Mixed));
    QCOMPARE(at(model, 0, TorrentFilesModel::PriorityTextRole).toString(), QStringLiteral("mixed"));
    QCOMPARE(at(model, 0, TorrentFilesModel::SizeRole).toLongLong(), 4199502437LL);

    // Progress is weighted by size: 18 KB of missing subtitles do not dent 3.9 GB.
    QVERIFY(at(model, 0, TorrentFilesModel::ProgressRole).toDouble() > 0.999);
    QCOMPARE(at(model, 0, TorrentFilesModel::ProgressTextRole).toString(), QStringLiteral("100 %"));

    // A folder carries every file id under it, sorted, so `+`/`-` can set them in one call.
    const QVariantList rootIds = at(model, 0, TorrentFilesModel::FileIndexesRole).toList();
    QCOMPARE(rootIds.size(), 5);
    QCOMPARE(rootIds.first().toInt(), 0);
    QCOMPARE(rootIds.last().toInt(), 4);
    QCOMPARE(model.fileIndexes(3), (QVariantList{2, 3}));
    QCOMPARE(model.fileIndexes(4), (QVariantList{2}));
}

void TestTorrentFilesModel::priority_text_covers_every_value()
{
    QCOMPARE(TorrentFilesModel::priorityText(0), QStringLiteral("skip"));
    QCOMPARE(TorrentFilesModel::priorityText(1), QStringLiteral("normal"));
    QCOMPARE(TorrentFilesModel::priorityText(6), QStringLiteral("high"));
    QCOMPARE(TorrentFilesModel::priorityText(7), QStringLiteral("maximal"));
    QCOMPARE(TorrentFilesModel::priorityText(-1), QStringLiteral("mixed"));
    QCOMPARE(TorrentFilesModel::priorityText(4), QStringLiteral("normal"));
}

void TestTorrentFilesModel::folding_hides_the_subtree_and_survives_a_refresh()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(model.count(), 7);
    QVERIFY(model.isExpanded(0));
    QVERIFY(!model.isFolder(1));
    QVERIFY(!model.isExpanded(1));

    // `h` on subs/ drops its two children.
    model.setExpanded(3, false);
    QCOMPARE(model.count(), 5);
    QCOMPARE(at(model, 3, TorrentFilesModel::NameRole).toString(), QStringLiteral("subs/"));
    QCOMPARE(at(model, 3, TorrentFilesModel::ExpandedRole).toBool(), false);
    QCOMPARE(at(model, 4, TorrentFilesModel::NameRole).toString(), QStringLiteral("LICENSE.txt"));

    // A refresh with the same shape must not reopen it.
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(model.count(), 5);

    // `l` opens it again; folding the root hides everything but the root.
    model.toggle(3);
    QCOMPARE(model.count(), 7);
    model.setExpanded(0, false);
    QCOMPARE(model.count(), 1);
    model.expandAll();
    QCOMPARE(model.count(), 7);
    model.collapseAll();
    QCOMPARE(model.count(), 1);
}

void TestTorrentFilesModel::updates_in_place_when_the_shape_is_unchanged()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-unicode-pack.json")));
    QCOMPARE(model.count(), 7);
    QCOMPARE(at(model, 0, TorrentFilesModel::ProgressTextRole).toString(), QStringLiteral("0 %"));

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.applyFiles(fixture(QStringLiteral("files-unicode-pack-progress.json")));

    QCOMPARE(reset.count(), 0); // the cursor survives a poll
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.count(), 7);

    // 120 B done and 10 B done; the 4096 B file half done is skipped, so, as in qBittorrent's
    // content view, the folder has everything it wants: 100 %, and nothing remaining.
    QCOMPARE(at(model, 0, TorrentFilesModel::PriorityTextRole).toString(), QStringLiteral("mixed"));
    QCOMPARE(at(model, 0, TorrentFilesModel::ProgressRole).toDouble(), 1.0);
    QCOMPARE(at(model, 0, TorrentFilesModel::RemainingRole).toLongLong(), 0LL);

    // Skip everything and the folder shows what is on disk over its full size:
    // 120 + 2048 + 10 of 4226.
    model.setLocalPriority(QVariantList{0, 2}, int(FilePriority::Skip));
    QVERIFY(qAbs(at(model, 0, TorrentFilesModel::ProgressRole).toDouble() - 0.5154) < 0.001);
}

void TestTorrentFilesModel::a_new_shape_resets_the_model()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-unicode-pack.json")));
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy counted(&model, &TorrentFilesModel::countChanged);

    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));

    QCOMPARE(reset.count(), 1);
    QCOMPARE(counted.count(), 1);
    QCOMPARE(model.count(), 7);
}

void TestTorrentFilesModel::unicode_paths_from_the_playground()
{
    // Recorded from :8081 after adding tests/fixtures/multi_file_v1_unicode.torrent stopped.
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-unicode-pack.json")));

    QCOMPARE(model.count(), 7);
    QCOMPARE(model.fileCount(), 3);
    QCOMPARE(at(model, 0, TorrentFilesModel::NameRole).toString(), QStringLiteral("Unicode Pack/"));
    QCOMPARE(at(model, 1, TorrentFilesModel::NameRole).toString(), QStringLiteral("café/"));
    QCOMPARE(at(model, 2, TorrentFilesModel::NameRole).toString(), QStringLiteral("notes.txt"));
    QCOMPARE(at(model, 3, TorrentFilesModel::NameRole).toString(), QStringLiteral("日本語/"));
    QCOMPARE(at(model, 4, TorrentFilesModel::NameRole).toString(), QStringLiteral("sub フォルダ/"));
    QCOMPARE(at(model, 4, TorrentFilesModel::DepthRole).toInt(), 2);
    QCOMPARE(at(model, 5, TorrentFilesModel::DepthRole).toInt(), 3);
    QCOMPARE(at(model, 0, TorrentFilesModel::SizeRole).toLongLong(), 4226LL);
}

void TestTorrentFilesModel::raise_and_lower_walk_the_ladder()
{
    QCOMPARE(TorrentFilesModel::raisedPriority(0), 1);
    QCOMPARE(TorrentFilesModel::raisedPriority(1), 6);
    QCOMPARE(TorrentFilesModel::raisedPriority(6), 7);
    QCOMPARE(TorrentFilesModel::raisedPriority(7), 7);
    QCOMPARE(TorrentFilesModel::raisedPriority(-1), 6);

    QCOMPARE(TorrentFilesModel::loweredPriority(7), 6);
    QCOMPARE(TorrentFilesModel::loweredPriority(6), 1);
    QCOMPARE(TorrentFilesModel::loweredPriority(1), 0);
    QCOMPARE(TorrentFilesModel::loweredPriority(0), 0);
    QCOMPARE(TorrentFilesModel::loweredPriority(-1), 1);
}

void TestTorrentFilesModel::empty_input_clears()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(model.count(), 7);

    model.applyFiles(QJsonArray());
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.fileCount(), 0);
    QVERIFY(!model.isFolder(0));
    QCOMPARE(model.fileIndexes(0), QVariantList());
}

namespace {

QStringList names(const TorrentFilesModel &model)
{
    QStringList out;
    for (int row = 0; row < model.count(); ++row)
        out.append(at(model, row, TorrentFilesModel::NameRole).toString());
    return out;
}

} // namespace

void TestTorrentFilesModel::sorting_by_name_keeps_the_tree_and_puts_folders_first()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QSignalSpy sorted(&model, &TorrentFilesModel::sortChanged);

    model.setSortKey(QStringLiteral("name"));
    QCOMPARE(sorted.count(), 1);
    // Natural order inside each folder ("1080p" before "2160p"), folders ahead of files.
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Big Buck Bunny 4K/"),
                          QStringLiteral("subs/"),
                          QStringLiteral("bbb.en.srt"),
                          QStringLiteral("bbb.pt.srt"),
                          QStringLiteral("bbb_sunflower_1080p_30fps.mp4"),
                          QStringLiteral("bbb_sunflower_2160p_60fps.mp4"),
                          QStringLiteral("LICENSE.txt")}));

    // Reversed, the folder still leads: qBittorrent's content view does the same.
    model.setSortDescending(true);
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Big Buck Bunny 4K/"),
                          QStringLiteral("subs/"),
                          QStringLiteral("bbb.pt.srt"),
                          QStringLiteral("bbb.en.srt"),
                          QStringLiteral("LICENSE.txt"),
                          QStringLiteral("bbb_sunflower_2160p_60fps.mp4"),
                          QStringLiteral("bbb_sunflower_1080p_30fps.mp4")}));
    // Depth is the tree's, whatever the order.
    QCOMPARE(at(model, 2, TorrentFilesModel::DepthRole).toInt(), 2);

    // Back to the torrent's own order.
    model.setSortKey(QString());
    QCOMPARE(at(model, 1, TorrentFilesModel::NameRole).toString(),
             QStringLiteral("bbb_sunflower_2160p_60fps.mp4"));
}

void TestTorrentFilesModel::sorting_by_a_number_mixes_folders_and_files()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    model.setSortKey(QStringLiteral("size"));
    model.setSortDescending(false);
    // Smallest first: the 1 KB licence, then the 18 KB subs folder, then the two videos.
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Big Buck Bunny 4K/"),
                          QStringLiteral("LICENSE.txt"),
                          QStringLiteral("subs/"),
                          QStringLiteral("bbb.en.srt"),
                          QStringLiteral("bbb.pt.srt"),
                          QStringLiteral("bbb_sunflower_1080p_30fps.mp4"),
                          QStringLiteral("bbb_sunflower_2160p_60fps.mp4")}));

    // A poll that changes nothing keeps the order; equal sizes keep the torrent's order.
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(at(model, 3, TorrentFilesModel::NameRole).toString(), QStringLiteral("bbb.en.srt"));
}

void TestTorrentFilesModel::a_poll_that_reorders_moves_rows_instead_of_resetting()
{
    TorrentFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QJsonArray files = fixture(QStringLiteral("files-fig2.json"));
    model.applyFiles(files);
    model.setSortKey(QStringLiteral("progress"));
    model.setSortDescending(true);

    // Both videos are done, the 2160p one first; the 1080p one overtakes it. A poll that
    // only moves numbers.
    QJsonArray next = files;
    int a = -1;
    int b = -1;
    for (int i = 0; i < next.size(); ++i) {
        const QString name = next.at(i).toObject().value(QStringLiteral("name")).toString();
        if (name.endsWith(QStringLiteral("1080p_30fps.mp4")))
            a = i;
        else if (name.endsWith(QStringLiteral("2160p_60fps.mp4")))
            b = i;
    }
    QVERIFY(a >= 0 && b >= 0);
    QJsonObject fileA = next.at(a).toObject();
    QJsonObject fileB = next.at(b).toObject();
    fileA.insert(QStringLiteral("progress"), 0.99);
    fileB.insert(QStringLiteral("progress"), 0.01);
    next.replace(a, fileA);
    next.replace(b, fileB);

    const QString heldPath = fileA.value(QStringLiteral("name")).toString();
    const QString otherPath = fileB.value(QStringLiteral("name")).toString();
    QVERIFY(model.rowOfPath(heldPath) > model.rowOfPath(otherPath));

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy reordered(&model, &TorrentFilesModel::reordered);
    model.applyFiles(next);

    QCOMPARE(reset.count(), 0); // the delegates, the scroll and the cursor survive
    QVERIFY(moved.count() > 0);
    QCOMPARE(reordered.count(), 1);
    // It overtook the other video, and rowOfPath finds it where it went.
    QVERIFY(model.rowOfPath(heldPath) >= 0);
    QVERIFY(model.rowOfPath(heldPath) < model.rowOfPath(otherPath));
    QCOMPARE(model.count(), 7);

    // A local priority change that re-sorts moves rows too.
    model.setSortKey(QStringLiteral("remaining"));
    reset.clear();
    model.setLocalPriority(QVariantList{fileB.value(QStringLiteral("index")).toInt()},
                           int(FilePriority::Skip));
    QCOMPARE(reset.count(), 0);
}

void TestTorrentFilesModel::a_long_list_reorders_in_one_layout_change()
{
    // Past a couple of thousand rows the moves give way to one layoutChanged, which must
    // still carry persistent indexes to their files.
    constexpr int kFiles = 2500;
    const auto build = [](int fastest) {
        QJsonArray files;
        for (int i = 0; i < kFiles; ++i) {
            files.append(QJsonObject{
                {QStringLiteral("index"), i},
                {QStringLiteral("name"), QStringLiteral("f%1.bin").arg(i, 5, 10, QLatin1Char('0'))},
                {QStringLiteral("size"), 1000},
                {QStringLiteral("progress"), i == fastest ? 0.9 : 0.1},
                {QStringLiteral("priority"), 1},
            });
        }
        return files;
    };

    TorrentFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.applyFiles(build(10));
    model.setSortKey(QStringLiteral("progress"));
    model.setSortDescending(true);
    QCOMPARE(model.rowOfPath(QStringLiteral("f00010.bin")), 0);

    const QPersistentModelIndex held = model.index(model.rowOfPath(QStringLiteral("f00020.bin")));
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy layout(&model, &QAbstractItemModel::layoutChanged);
    QSignalSpy reordered(&model, &TorrentFilesModel::reordered);

    model.applyFiles(build(20));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(layout.count(), 1);
    QCOMPARE(reordered.count(), 1);
    QCOMPARE(model.rowOfPath(QStringLiteral("f00020.bin")), 0);
    QVERIFY(held.isValid());
    QCOMPARE(held.row(), 0);
}

void TestTorrentFilesModel::a_priority_on_every_file_of_a_huge_torrent_is_linear()
{
    constexpr int kFiles = 20000;
    QJsonArray files;
    QVariantList ids;
    for (int i = 0; i < kFiles; ++i) {
        files.append(QJsonObject{
            {QStringLiteral("index"), i},
            {QStringLiteral("name"), QStringLiteral("pack/f%1.bin").arg(i)},
            {QStringLiteral("size"), 1000},
            {QStringLiteral("progress"), 0.0},
            {QStringLiteral("priority"), 1},
        });
        ids.append(i);
    }
    TorrentFilesModel model;
    model.applyFiles(files);

    QElapsedTimer timer;
    timer.start();
    model.setLocalPriority(ids, int(FilePriority::Skip));
    const double ms = double(timer.nsecsElapsed()) / 1e6;
    qInfo("setLocalPriority on %d files: %.1f ms", kFiles, ms);

    QCOMPARE(at(model, 0, TorrentFilesModel::PriorityRole).toInt(), int(FilePriority::Skip));
    // A scan of the tree per id took seconds here in a Debug build.
    QVERIFY2(ms < 1000.0, qPrintable(QStringLiteral("took %1 ms").arg(ms)));
}

void TestTorrentFilesModel::sort_by_flips_the_current_key_and_picks_a_direction_for_a_new_one()
{
    TorrentFilesModel model;
    model.sortBy(QStringLiteral("size"));
    QCOMPARE(model.sortKey(), QStringLiteral("size"));
    QVERIFY(model.sortDescending()); // numbers: largest first
    model.sortBy(QStringLiteral("size"));
    QVERIFY(!model.sortDescending());
    model.sortBy(QStringLiteral("name"));
    QVERIFY(!model.sortDescending()); // text: a to z
    model.sortBy(QStringLiteral("pieces"));
    QVERIFY(!model.sortDescending());
}

// #51 D14: first click as sortBy() starts it, second flips, third is the torrent's order;
// whichever way the key starts.
void TestTorrentFilesModel::a_header_cycle_goes_back_to_the_torrent_order()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    const QString first = model.pathAt(1);

    model.cycleSort(QStringLiteral("size"));
    QCOMPARE(model.sortKey(), QStringLiteral("size"));
    QVERIFY(model.sortDescending());
    model.cycleSort(QStringLiteral("size"));
    QCOMPARE(model.sortKey(), QStringLiteral("size"));
    QVERIFY(!model.sortDescending());
    model.cycleSort(QStringLiteral("size"));
    QCOMPARE(model.sortKey(), QString());
    QVERIFY(!model.sortDescending());
    QCOMPARE(model.pathAt(1), first);
    model.cycleSort(QStringLiteral("size"));
    QCOMPARE(model.sortKey(), QStringLiteral("size"));
    QVERIFY(model.sortDescending());

    model.cycleSort(QStringLiteral("name"));
    QVERIFY(!model.sortDescending());
    model.cycleSort(QStringLiteral("name"));
    QVERIFY(model.sortDescending());
    model.cycleSort(QStringLiteral("name"));
    QCOMPARE(model.sortKey(), QString());

    // A key set the other way round by the sort menu (`s`, picked twice) goes to the torrent
    // order on its next header click.
    model.sortBy(QStringLiteral("progress"));
    model.sortBy(QStringLiteral("progress"));
    QVERIFY(!model.sortDescending());
    model.cycleSort(QStringLiteral("progress"));
    QCOMPARE(model.sortKey(), QString());
}

void TestTorrentFilesModel::the_filter_keeps_the_folders_of_a_match()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(model.matchCount(), 5);

    // Fold the root first: a filter opens every folder, and clearing it restores the fold.
    model.setExpanded(0, false);
    QCOMPARE(model.count(), 1);

    model.setFilterText(QStringLiteral("SRT"));
    QCOMPARE(model.matchCount(), 2);
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Big Buck Bunny 4K/"),
                          QStringLiteral("subs/"),
                          QStringLiteral("bbb.en.srt"),
                          QStringLiteral("bbb.pt.srt")}));
    QVERIFY(model.isExpanded(0));
    model.setExpanded(0, false); // no folding while filtering
    QCOMPARE(model.count(), 4);

    // Every word must be in the path; a folder's name counts, since it is in the path.
    model.setFilterText(QStringLiteral("subs pt"));
    QCOMPARE(model.matchCount(), 1);
    QCOMPARE(model.count(), 3);

    model.setFilterText(QStringLiteral("nothing like this"));
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.matchCount(), 0);

    model.setFilterText(QString());
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.matchCount(), 5);

    // Another torrent starts unfiltered, but keeps the sort.
    model.setFilterText(QStringLiteral("srt"));
    model.setSortKey(QStringLiteral("size"));
    model.clear();
    QVERIFY(model.filterText().isEmpty());
    QCOMPARE(model.sortKey(), QStringLiteral("size"));
}

void TestTorrentFilesModel::a_folder_acts_on_the_files_the_filter_shows()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    model.setFilterText(QStringLiteral("en.srt"));
    QCOMPARE(model.fileIndexes(0), (QVariantList{2}));
    QCOMPARE(model.targetFileIndexes(0), (QVariantList{2}));
    QCOMPARE(at(model, 0, TorrentFilesModel::FileIndexesRole).toList(), (QVariantList{2}));
    // The folder's own figures stay the whole folder's.
    QCOMPARE(at(model, 0, TorrentFilesModel::SizeRole).toLongLong(), 4199502437LL);
}

void TestTorrentFilesModel::remaining_availability_and_pieces_add_up()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));

    // Everything wanted is complete, and a skipped file has nothing left to fetch.
    QCOMPARE(at(model, 0, TorrentFilesModel::RemainingRole).toLongLong(), 0LL);
    QCOMPARE(at(model, 4, TorrentFilesModel::RemainingRole).toLongLong(), 0LL);
    QCOMPARE(at(model, 0, TorrentFilesModel::RemainingTextRole).toString(), QStringLiteral("0"));

    // A folder's availability is over its wanted files; all skipped reads unknown.
    QCOMPARE(at(model, 0, TorrentFilesModel::AvailabilityTextRole).toString(),
             QStringLiteral("100 %"));
    QCOMPARE(at(model, 3, TorrentFilesModel::AvailabilityRole).toDouble(), -1.0);
    QCOMPARE(at(model, 3, TorrentFilesModel::AvailabilityTextRole).toString(), QStringLiteral("—"));

    QCOMPARE(at(model, 0, TorrentFilesModel::PiecesTextRole).toString(), QStringLiteral("0–3912"));
    QCOMPARE(at(model, 1, TorrentFilesModel::PiecesTextRole).toString(), QStringLiteral("0–3859"));
    QCOMPARE(at(model, 3, TorrentFilesModel::PiecesTextRole).toString(), QStringLiteral("3912"));
    QCOMPARE(at(model, 3, TorrentFilesModel::PieceFirstRole).toInt(), 3912);
}

void TestTorrentFilesModel::a_local_priority_reaggregates_the_folders()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.setLocalPriority({2}, int(FilePriority::Normal));
    QVERIFY(changed.count() > 0);
    QCOMPARE(model.priority(4), int(FilePriority::Normal));
    QCOMPARE(model.priority(3), int(FilePriority::Mixed));
    // Wanted now, and not downloaded: 9 KB to fetch, and nobody has its pieces.
    QCOMPARE(at(model, 4, TorrentFilesModel::RemainingRole).toLongLong(), 9216LL);
    QCOMPARE(at(model, 0, TorrentFilesModel::RemainingRole).toLongLong(), 9216LL);
    QCOMPARE(at(model, 3, TorrentFilesModel::AvailabilityRole).toDouble(), 0.0);

    // Setting the same value again changes nothing.
    changed.clear();
    model.setLocalPriority({2}, int(FilePriority::Normal));
    QCOMPARE(changed.count(), 0);
}

void TestTorrentFilesModel::selection_targets_its_files_and_survives_a_refresh()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QSignalSpy selection(&model, &TorrentFilesModel::selectionChanged);

    // Nothing selected: the cursor row is the target.
    QCOMPARE(model.targetFileIndexes(3), (QVariantList{2, 3}));
    QCOMPARE(model.targetPriority(3), int(FilePriority::Skip));

    model.toggleSelected(1);
    model.toggleSelected(6);
    QCOMPARE(model.selectedCount(), 2);
    QCOMPARE(selection.count(), 2);
    QVERIFY(at(model, 1, TorrentFilesModel::SelectedRole).toBool());
    // The selection wins over the cursor, wherever the cursor is.
    QCOMPARE(model.targetFileIndexes(3), (QVariantList{0, 4}));
    QCOMPARE(model.targetPriority(3), int(FilePriority::Mixed));

    // A folder and a file inside it count the file once.
    model.selectRange(3, 4);
    QCOMPARE(model.selectedCount(), 4);
    QCOMPARE(model.targetFileIndexes(0), (QVariantList{0, 2, 3, 4}));

    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QVERIFY(model.isSelected(1));
    QCOMPARE(model.selectedCount(), 4);

    model.clearSelection();
    QCOMPARE(model.selectedCount(), 0);
    QCOMPARE(model.targetFileIndexes(1), (QVariantList{0}));

    model.selectAll();
    QCOMPARE(model.selectedCount(), 7);
}

void TestTorrentFilesModel::shown_order_splits_the_files_in_thirds()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));

    // Five files: groups of one, so the first is maximal, the second high, the rest normal.
    const QVariantList groups = model.shownOrderGroups(0);
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.at(0).toMap().value(QStringLiteral("priority")).toInt(), 7);
    QCOMPARE(groups.at(0).toMap().value(QStringLiteral("fileIndexes")).toList(), (QVariantList{0}));
    QCOMPARE(groups.at(1).toMap().value(QStringLiteral("fileIndexes")).toList(), (QVariantList{1}));
    QCOMPARE(groups.at(2).toMap().value(QStringLiteral("priority")).toInt(), 1);
    QCOMPARE(groups.at(2).toMap().value(QStringLiteral("fileIndexes")).toList(),
             (QVariantList{2, 3, 4}));

    // "Shown" means the order on screen: smallest first puts the licence at the top.
    model.setSortKey(QStringLiteral("size"));
    model.setSortDescending(false);
    const QVariantList bySize = model.shownOrderGroups(0);
    QCOMPARE(bySize.at(0).toMap().value(QStringLiteral("fileIndexes")).toList(), (QVariantList{4}));

    // One file is one group.
    QCOMPARE(model.shownOrderGroups(1).size(), 1);
}

void TestTorrentFilesModel::a_local_rename_moves_the_subtree()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    model.setExpanded(3, false); // fold subs/
    model.setSelected(3, true);
    QCOMPARE(model.count(), 5);

    model.renameLocal(QStringLiteral("Big Buck Bunny 4K/subs"),
                      QStringLiteral("Big Buck Bunny 4K/subtitles"));
    QCOMPARE(model.nameAt(3), QStringLiteral("subtitles"));
    QCOMPARE(model.count(), 5); // still folded
    QVERIFY(model.isSelected(3));
    model.setExpanded(3, true);
    QCOMPARE(model.pathAt(4), QStringLiteral("Big Buck Bunny 4K/subtitles/bbb.en.srt"));

    model.renameLocal(QStringLiteral("Big Buck Bunny 4K/LICENSE.txt"),
                      QStringLiteral("Big Buck Bunny 4K/COPYING"));
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/COPYING")), 6);
    QCOMPARE(model.parentRow(6), 0);
    QCOMPARE(model.parentRow(0), -1);
}

void TestTorrentFilesModel::the_server_path_follows_the_content_path()
{
    // A folder torrent: the files' names start with the content path's last part.
    QCOMPARE(TorrentFilesModel::serverPathOf(QStringLiteral("/data/movies/Big Buck Bunny 4K"),
                                             QStringLiteral("/data/movies/"),
                                             QStringLiteral("Big Buck Bunny 4K/subs/bbb.en.srt")),
             QStringLiteral("/data/movies/Big Buck Bunny 4K/subs/bbb.en.srt"));
    // Still downloading into qBittorrent's incomplete folder: the content path knows.
    QCOMPARE(TorrentFilesModel::serverPathOf(QStringLiteral("/data/incomplete/Pack"),
                                             QStringLiteral("/data/done"),
                                             QStringLiteral("Pack/a.bin")),
             QStringLiteral("/data/incomplete/Pack/a.bin"));
    // A one-file torrent.
    QCOMPARE(TorrentFilesModel::serverPathOf(QStringLiteral("/data/linux/arch.iso"),
                                             QStringLiteral("/data/linux"),
                                             QStringLiteral("arch.iso")),
             QStringLiteral("/data/linux/arch.iso"));
    // No content path yet: the save path.
    QCOMPARE(TorrentFilesModel::serverPathOf(
                 QString(), QStringLiteral("/data/linux/"), QStringLiteral("arch.iso")),
             QStringLiteral("/data/linux/arch.iso"));
    // A Windows server keeps its own separator.
    QCOMPARE(TorrentFilesModel::serverPathOf(QStringLiteral("D:\\Torrents\\Pack"),
                                             QStringLiteral("D:\\Torrents"),
                                             QStringLiteral("Pack/a.bin")),
             QStringLiteral("D:\\Torrents\\Pack\\a.bin"));
    QCOMPARE(TorrentFilesModel::serverPathOf(QString(), QString(), QStringLiteral("a")), QString());
}

// docs/12 F1: the line above the headers, "5 files · 2 folders · 3.91 GB · 2 skipped".
void TestTorrentFilesModel::the_summary_counts_files_folders_size_and_skips()
{
    TorrentFilesModel model;
    QSignalSpy summary(&model, &TorrentFilesModel::summaryChanged);
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(summary.count(), 1);
    QCOMPARE(model.fileCount(), 5);
    QCOMPARE(model.folderCount(), 2);
    QCOMPARE(model.skippedCount(), 2);
    QCOMPARE(model.totalSize(), 4199502437LL);
    QCOMPARE(model.totalSizeText(), QStringLiteral("3.91 GB"));

    // The filter narrows the rows, not the summary.
    model.setFilterText(QStringLiteral("srt"));
    QCOMPARE(model.folderCount(), 2);
    QCOMPARE(model.totalSize(), 4199502437LL);

    // A priority change moves the skipped count; a poll with the same numbers does not emit.
    summary.clear();
    model.setLocalPriority({2}, int(FilePriority::Normal));
    QCOMPARE(model.skippedCount(), 1);
    QCOMPARE(summary.count(), 1);
    model.setLocalPriority({2}, int(FilePriority::Skip));
    summary.clear();
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(summary.count(), 0);

    model.clear();
    QCOMPARE(model.folderCount(), 0);
    QCOMPARE(model.skippedCount(), 0);
    QCOMPARE(model.totalSize(), 0);
    QCOMPARE(summary.count(), 1);
}

// docs/12 F3: `ctrl+a` selects the files the filter shows, and a priority then acts on them.
void TestTorrentFilesModel::select_all_files_takes_what_the_filter_shows()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QSignalSpy selection(&model, &TorrentFilesModel::selectionChanged);

    model.setFilterText(QStringLiteral("srt"));
    model.selectAllFiles();
    QCOMPARE(selection.count(), 1);
    QCOMPARE(model.selectedCount(), 2);
    QCOMPARE(model.targetFileIndexes(0), (QVariantList{2, 3}));
    // The folders around the matches are shown, not selected.
    QVERIFY(!model.isSelected(0));
    QVERIFY(!model.isSelected(1));
    QVERIFY(model.isSelected(2));
    QVERIFY(model.isSelected(3));

    // Again: nothing new, no signal.
    model.selectAllFiles();
    QCOMPARE(selection.count(), 1);

    // No filter: every file, including the ones inside a folded folder.
    model.clearSelection();
    model.setFilterText(QString());
    model.setExpanded(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs")), false);
    model.selectAllFiles();
    QCOMPARE(model.selectedCount(), 5);
    QCOMPARE(model.targetFileIndexes(0), (QVariantList{0, 1, 2, 3, 4}));
}

// docs/12 F4: a plain substring, `*` for any run of characters, `|` for or, against the path.
void TestTorrentFilesModel::the_filter_takes_wildcards_and_alternatives_data()
{
    QTest::addColumn<QString>("filter");
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("matches");

    const QString srt = QStringLiteral("Big Buck Bunny 4K/subs/bbb.en.srt");
    const QString mp4 = QStringLiteral("Big Buck Bunny 4K/bbb_sunflower_1080p_30fps.mp4");
    const QString mkv = QStringLiteral("Show/S01/Show.S01E02.mkv");

    QTest::newRow("empty") << QString() << srt << true;
    QTest::newRow("only separators") << QStringLiteral(" | ") << srt << true;
    QTest::newRow("substring") << QStringLiteral("en.srt") << srt << true;
    QTest::newRow("case") << QStringLiteral("BBB.EN") << srt << true;
    QTest::newRow("not there") << QStringLiteral("pt.srt") << srt << false;
    QTest::newRow("a folder in the path") << QStringLiteral("subs/") << srt << true;
    QTest::newRow("a folder, not a file") << QStringLiteral("subs/") << mp4 << false;
    QTest::newRow("star suffix") << QStringLiteral("*.srt") << srt << true;
    QTest::newRow("star suffix, other") << QStringLiteral("*.srt") << mp4 << false;
    QTest::newRow("star inside") << QStringLiteral("bbb*srt") << srt << true;
    QTest::newRow("star inside, in order") << QStringLiteral("srt*bbb") << srt << false;
    QTest::newRow("star across folders") << QStringLiteral("4k*en") << srt << true;
    QTest::newRow("star alone") << QStringLiteral("*") << mp4 << true;
    QTest::newRow("regex characters are literal") << QStringLiteral("bbb.en") << srt << true;
    QTest::newRow("a dot is not any character") << QStringLiteral("bbb.s*") << mp4 << false;
    // DECISIONS C22 (#125): a plain word folds `.`, `_` and `-` as the library's search does.
    QTest::newRow("separators fold") << QStringLiteral("bbb.sunflower") << mp4 << true;
    QTest::newRow("separators fold, both sides") << QStringLiteral("big_buck") << srt << true;
    QTest::newRow("a folded word's parts in any order") << QStringLiteral("en-bbb") << srt << true;
    QTest::newRow("a folded word, one part missing") << QStringLiteral("bbb.pt") << srt << false;
    QTest::newRow("brackets are literal") << QStringLiteral("[1080p]") << mp4 << false;
    QTest::newRow("or, first") << QStringLiteral("mkv|mp4") << mkv << true;
    QTest::newRow("or, second") << QStringLiteral("mkv|mp4") << mp4 << true;
    QTest::newRow("or, neither") << QStringLiteral("mkv|mp4") << srt << false;
    QTest::newRow("or, with spaces") << QStringLiteral("mkv | mp4") << mp4 << true;
    QTest::newRow("or, half typed") << QStringLiteral("mkv|") << mkv << true;
    QTest::newRow("or, half typed, other") << QStringLiteral("mkv|") << mp4 << false;
    QTest::newRow("or with a star") << QStringLiteral("*.srt|*.mkv") << mkv << true;
    QTest::newRow("words, all of them") << QStringLiteral("subs pt") << srt << false;
    QTest::newRow("words, in one alternative") << QStringLiteral("subs en|mkv") << srt << true;
}

void TestTorrentFilesModel::the_filter_takes_wildcards_and_alternatives()
{
    QFETCH(QString, filter);
    QFETCH(QString, path);
    QFETCH(bool, matches);
    QCOMPARE(TorrentFilesModel::filterMatches(filter, path), matches);
}

void TestTorrentFilesModel::a_wildcard_filter_keeps_the_folders_of_a_match()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));

    model.setFilterText(QStringLiteral("*.srt"));
    QCOMPARE(model.matchCount(), 2);
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Big Buck Bunny 4K/"),
                          QStringLiteral("subs/"),
                          QStringLiteral("bbb.en.srt"),
                          QStringLiteral("bbb.pt.srt")}));

    model.setFilterText(QStringLiteral("txt|2160p"));
    QCOMPARE(model.matchCount(), 2);
    QCOMPARE(names(model),
             (QStringList{QStringLiteral("Big Buck Bunny 4K/"),
                          QStringLiteral("bbb_sunflower_2160p_60fps.mp4"),
                          QStringLiteral("LICENSE.txt")}));

    // "subs/" is a folder's name in the path: its files, and the folders around them.
    model.setFilterText(QStringLiteral("subs/"));
    QCOMPARE(model.matchCount(), 2);
    QCOMPARE(model.count(), 4);
}

// #9: hiding the skipped files takes Fig. 2's two subtitles out, and with them `subs/`,
// which holds nothing else. The summary still counts them; the folds are left alone.
void TestTorrentFilesModel::hiding_skipped_files_drops_them_and_their_empty_folders()
{
    TorrentFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(model.count(), 7);
    QSignalSpy hide(&model, &TorrentFilesModel::hideSkippedChanged);
    QSignalSpy count(&model, &TorrentFilesModel::countChanged);

    model.setHideSkipped(true);
    QCOMPARE(hide.count(), 1);
    QVERIFY(count.count() >= 1);
    QCOMPARE(model.count(), 4);
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs")), -1);
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs/bbb.en.srt")), -1);
    QCOMPARE(model.matchCount(), 3);
    QCOMPARE(model.skippedCount(), 2);
    QCOMPARE(model.fileCount(), 5);
    // The root folder acts on the files it shows, as it does under a typed filter.
    QCOMPARE(model.fileIndexes(0), (QVariantList{0, 1, 4}));
    model.selectAllFiles();
    QCOMPARE(model.targetFileIndexes(0), (QVariantList{0, 1, 4}));
    model.clearSelection();

    // Unlike a typed filter it keeps the folds: a folded root stays folded.
    model.setExpanded(0, false);
    QCOMPARE(model.count(), 1);
    model.setHideSkipped(false);
    QCOMPARE(model.count(), 1);
    model.setExpanded(0, true);
    QCOMPARE(model.count(), 7);

    // With a typed filter on top, both apply: "srt" finds only skipped files.
    model.setHideSkipped(true);
    model.setFilterText(QStringLiteral("srt"));
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.matchCount(), 0);
    model.setHideSkipped(false);
    QCOMPARE(model.count(), 4); // the root, subs/ and the two subtitles
}

// A file skipped while they are hidden goes at once; one raised from skip comes back, and
// so does a change the 2 s poll brings from elsewhere.
void TestTorrentFilesModel::a_priority_change_hides_or_shows_a_file_at_once()
{
    TorrentFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QJsonArray files = fixture(QStringLiteral("files-fig2.json"));
    model.applyFiles(files);
    model.setHideSkipped(true);
    QCOMPARE(model.count(), 4);

    QSignalSpy count(&model, &TorrentFilesModel::countChanged);
    model.setLocalPriority({1}, int(FilePriority::Skip));
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/bbb_sunflower_1080p_30fps.mp4")),
             -1);
    QCOMPARE(model.matchCount(), 2);
    QVERIFY(count.count() >= 1);

    model.setLocalPriority({2}, int(FilePriority::Normal));
    QVERIFY(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs")) >= 0);
    QVERIFY(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs/bbb.en.srt")) >= 0);
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs/bbb.pt.srt")), -1);

    // The poll: the server says LICENSE.txt is skipped now, and the rest as the fixture has it.
    QJsonArray polled = files;
    QJsonObject license = polled.at(4).toObject();
    license.insert(QStringLiteral("priority"), 0);
    polled.replace(4, license);
    count.clear();
    model.applyFiles(polled);
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/LICENSE.txt")), -1);
    QCOMPARE(model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs")), -1);
    QCOMPARE(model.count(), 3); // the root and the two films
    QCOMPARE(model.matchCount(), 2);
    QVERIFY(count.count() >= 1);
}

// clear() is another torrent: the typed filter goes, hideSkipped stays (like the sort).
void TestTorrentFilesModel::hiding_skipped_files_is_a_preference_not_the_torrents()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    model.setHideSkipped(true);
    QSignalSpy hide(&model, &TorrentFilesModel::hideSkippedChanged);
    model.setHideSkipped(true);
    QCOMPARE(hide.count(), 0);

    model.clear();
    QVERIFY(model.hideSkipped());
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QCOMPARE(model.count(), 4);
    QCOMPARE(hide.count(), 0);
}

// #11 (docs/18 B1, E1): a batch rename takes the selected files the filter shows, in the
// tab's sorted order, else every file it shows, folded or not; never a folder.
void TestTorrentFilesModel::rename_targets_are_the_selected_files_else_every_shown_one()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    const QString root = QStringLiteral("Big Buck Bunny 4K/");
    model.sortBy(QStringLiteral("name"));
    model.setExpanded(model.rowOfPath(root + QStringLiteral("subs")), false);
    // Folders first (docs N1), then files by name; the folded subs still count.
    QCOMPARE(model.renameTargets(),
             QStringList({root + QStringLiteral("subs/bbb.en.srt"),
                          root + QStringLiteral("subs/bbb.pt.srt"),
                          root + QStringLiteral("bbb_sunflower_1080p_30fps.mp4"),
                          root + QStringLiteral("bbb_sunflower_2160p_60fps.mp4"),
                          root + QStringLiteral("LICENSE.txt")}));
    QCOMPARE(model.selectedFileCount(), 0);

    model.setFilterText(QStringLiteral("mp4"));
    QCOMPARE(model.renameTargets().size(), 2);

    // A selection wins, and a folder row in it adds nothing.
    model.setFilterText(QString());
    model.setSelected(model.rowOfPath(root + QStringLiteral("LICENSE.txt")), true);
    model.setSelected(model.rowOfPath(root.chopped(1)), true);
    QCOMPARE(model.selectedFileCount(), 1);
    QCOMPARE(model.renameTargets(), QStringList({root + QStringLiteral("LICENSE.txt")}));
}

// #42 (docs/27): the tick column. A file reads 2 unless skipped (0); a folder 2 with nothing
// skipped under it, 0 with everything skipped, 1 in between. Kept up by a local priority.
void TestTorrentFilesModel::the_tick_reads_wanted_over_each_subtree()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QAbstractItemModelTester tester(&model);

    const int root = model.rowOfPath(QStringLiteral("Big Buck Bunny 4K"));
    const int subs = model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs"));
    const int srt = model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/subs/bbb.en.srt"));
    const int license = model.rowOfPath(QStringLiteral("Big Buck Bunny 4K/LICENSE.txt"));
    QVERIFY(root >= 0 && subs >= 0 && srt >= 0 && license >= 0);
    QCOMPARE(at(model, root, TorrentFilesModel::WantedRole).toInt(), 1);
    QCOMPARE(at(model, subs, TorrentFilesModel::WantedRole).toInt(), 0);
    QCOMPARE(at(model, srt, TorrentFilesModel::WantedRole).toInt(), 0);
    QCOMPARE(at(model, license, TorrentFilesModel::WantedRole).toInt(), 2);
    QCOMPARE(model.headWanted(), 1);
    QCOMPARE(model.headFileCount(), 5);

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy head(&model, &TorrentFilesModel::headWantedChanged);
    model.setLocalPriority({2, 3}, int(FilePriority::Normal));
    QVERIFY(changed.count() > 0);
    QVERIFY(changed.last().at(2).value<QList<int>>().contains(TorrentFilesModel::WantedRole));
    QCOMPARE(at(model, subs, TorrentFilesModel::WantedRole).toInt(), 2);
    QCOMPARE(at(model, root, TorrentFilesModel::WantedRole).toInt(), 2);
    QCOMPARE(model.headWanted(), 2);
    QCOMPARE(head.count(), 1);

    model.setLocalPriority(model.headFileIndexes(), int(FilePriority::Skip));
    QCOMPARE(at(model, root, TorrentFilesModel::WantedRole).toInt(), 0);
    QCOMPARE(model.headWanted(), 0);
    QCOMPARE(model.filePriority(4), int(FilePriority::Skip));
    QCOMPARE(model.filePriority(99), -1);
}

// docs/27 D4: with a filter typed, the head covers what it matches, skipped files hidden by
// hideSkipped included.
void TestTorrentFilesModel::the_head_tick_covers_what_the_filter_shows()
{
    TorrentFilesModel model;
    model.applyFiles(fixture(QStringLiteral("files-fig2.json")));
    QSignalSpy head(&model, &TorrentFilesModel::headWantedChanged);

    model.setFilterText(QStringLiteral("*.srt"));
    QCOMPARE(model.headFileIndexes(), (QVariantList{2, 3}));
    QCOMPARE(model.headFileCount(), 2);
    QCOMPARE(model.headWanted(), 0);
    QCOMPARE(head.count(), 1);

    model.setHideSkipped(true);
    QCOMPARE(model.headFileIndexes(), (QVariantList{2, 3}));

    model.setFilterText(QString());
    QCOMPARE(model.headFileCount(), 5);
    QCOMPARE(model.headWanted(), 1);
}

void TestTorrentFilesModel::on_disk_shows_what_skipped_files_hold()
{
    // DECISIONS C17 D1: a skipped file's allocated bytes, "—" for a wanted one, a folder the
    // sum of its skipped files'; the column shows only while some skipped file holds any.
    const auto file = [](int index, const QString &name, int priority) {
        return QJsonObject{{QStringLiteral("index"), index},
                           {QStringLiteral("name"), name},
                           {QStringLiteral("size"), 1000.0},
                           {QStringLiteral("priority"), priority},
                           {QStringLiteral("progress"), 0.0}};
    };
    const QJsonArray files{file(0, QStringLiteral("A/wanted.bin"), 1),
                           file(1, QStringLiteral("A/b.bin"), 0),
                           file(2, QStringLiteral("A/c.bin"), 0)};
    TorrentFilesModel model;
    QAbstractItemModelTester tester(&model);
    model.applyFiles(files);
    QSignalSpy changed(&model, &TorrentFilesModel::onDiskChanged);
    QVERIFY(!model.showOnDisk());
    QCOMPARE(at(model, 2, TorrentFilesModel::OnDiskTextRole).toString(), QString());

    model.setOnDisk({{1, 4096 * 1024}, {2, 0}});
    QCOMPARE(changed.count(), 1);
    QVERIFY(model.showOnDisk());
    QCOMPARE(model.heldOnDisk(), qint64(4096 * 1024));
    QCOMPARE(model.holdingOnDisk(), 1);
    QCOMPARE(at(model, 0, TorrentFilesModel::OnDiskRole).toLongLong(), qint64(4096 * 1024));
    QCOMPARE(at(model, 1, TorrentFilesModel::OnDiskTextRole).toString(), QStringLiteral("—"));
    QVERIFY(
        at(model, 2, TorrentFilesModel::OnDiskTextRole).toString().contains(QStringLiteral("MB")));
    // Read, and holding nothing (sparse): a size, not the wanted file's "—".
    const QString none = at(model, 3, TorrentFilesModel::OnDiskTextRole).toString();
    QVERIFY(!none.isEmpty() && none != QStringLiteral("—"));

    // A poll keeps the reading; a tick makes the file's space expected, so it leaves the sum.
    model.applyFiles(files);
    QCOMPARE(model.heldOnDisk(), qint64(4096 * 1024));
    model.setLocalPriority({1}, int(FilePriority::Normal));
    QVERIFY(!model.showOnDisk());
    QCOMPARE(at(model, 2, TorrentFilesModel::OnDiskTextRole).toString(), QStringLiteral("—"));

    model.clear();
    model.applyFiles(files);
    QVERIFY(!model.showOnDisk());
}

QTEST_MAIN(TestTorrentFilesModel)
#include "tst_torrentfilesmodel.moc"
