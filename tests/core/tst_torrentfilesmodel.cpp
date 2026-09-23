// TorrentFilesModel against recorded `torrents/files` answers: the tree it builds out of
// slash-separated names, the aggregates a folder shows, the fold state, and the in-place
// update that keeps the rows (and the cursor) while the numbers move.

#include "core/TorrentFilesModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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
    void sort_by_flips_the_current_key_and_picks_a_direction_for_a_new_one();
    void the_filter_keeps_the_folders_of_a_match();
    void a_folder_acts_on_the_files_the_filter_shows();
    void remaining_availability_and_pieces_add_up();
    void a_local_priority_reaggregates_the_folders();
    void selection_targets_its_files_and_survives_a_refresh();
    void shown_order_splits_the_files_in_thirds();
    void a_local_rename_moves_the_subtree();
    void the_server_path_follows_the_content_path();
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

    // 120 B done + 4096 B half done + 10 B done, out of 4226.
    QCOMPARE(at(model, 0, TorrentFilesModel::PriorityTextRole).toString(), QStringLiteral("mixed"));
    QVERIFY(qAbs(at(model, 0, TorrentFilesModel::ProgressRole).toDouble() - 0.5156) < 0.001);
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

QTEST_MAIN(TestTorrentFilesModel)
#include "tst_torrentfilesmodel.moc"
