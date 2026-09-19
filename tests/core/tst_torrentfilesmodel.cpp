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

QTEST_MAIN(TestTorrentFilesModel)
#include "tst_torrentfilesmodel.moc"
