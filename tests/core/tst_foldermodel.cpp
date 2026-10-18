#include "core/FolderModel.hpp"
#include "core/FolderTree.hpp"

#include <QSignalSpy>
#include <QTest>

using omaseed::core::FolderModel;
using omaseed::core::FolderNode;
using omaseed::core::FolderTree;
using omaseed::core::Torrent;

namespace {

Torrent at(const QString &savePath, qint64 completed = 1)
{
    static int serial = 0;
    Torrent torrent;
    torrent.hash = QStringLiteral("%1").arg(++serial, 40, 16, QLatin1Char('0'));
    torrent.savePath = savePath;
    torrent.completed = completed;
    torrent.size = completed;
    return torrent;
}

QList<Torrent> library()
{
    QList<Torrent> torrents;
    for (int i = 0; i < 3; ++i)
        torrents << at(QStringLiteral("/mnt/archive/games"), 100);
    for (int i = 0; i < 2; ++i)
        torrents << at(QStringLiteral("/mnt/archive/games/handheld"), 10);
    torrents << at(QStringLiteral("/mnt/fast/downloads"));
    torrents << at(QStringLiteral("/srv/media/films"));
    return torrents;
}

QStringList shown(const FolderModel &model)
{
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row)
        out << model.pathAt(row);
    return out;
}

} // namespace

// Board 42 §2 F1–F3 (DECISIONS C15 M1–M3): the rail's folders group as a model.
class TestFolderModel : public QObject
{
    Q_OBJECT

private slots:
    void rowsAndRoles()
    {
        FolderModel model;
        model.setNodes(FolderTree::build(library()));
        QCOMPARE(model.total(), 7);
        QCOMPARE(shown(model),
                 QStringList({QStringLiteral("/mnt/archive"),
                              QStringLiteral("/mnt/archive/games"),
                              QStringLiteral("/mnt/archive/games/handheld"),
                              QStringLiteral("/mnt/fast"),
                              QStringLiteral("/mnt/fast/downloads"),
                              QStringLiteral("/srv/media"),
                              QStringLiteral("/srv/media/films")}));
        const QModelIndex games = model.index(1);
        QCOMPARE(games.data(FolderModel::NameRole).toString(), QStringLiteral("games"));
        QCOMPARE(games.data(FolderModel::DepthRole).toInt(), 1);
        QCOMPARE(games.data(FolderModel::HeadingRole).toBool(), false);
        QCOMPARE(games.data(FolderModel::HasChildrenRole).toBool(), true);
        QCOMPARE(games.data(FolderModel::CountRole).toInt(), 5);
        QCOMPARE(games.data(FolderModel::CountHereRole).toInt(), 3);
        QCOMPARE(games.data(FolderModel::BytesRole).toLongLong(), 320);
        QCOMPARE(games.data(FolderModel::SavePathsRole).toInt(), 2);
        QCOMPARE(model.index(0).data(FolderModel::HeadingRole).toBool(), true);

        const QVariantMap figures = model.figures(QStringLiteral("/mnt/archive/games"));
        QCOMPARE(figures.value(QStringLiteral("name")).toString(), QStringLiteral("games"));
        QCOMPARE(figures.value(QStringLiteral("count")).toInt(), 5);
        QCOMPARE(figures.value(QStringLiteral("savePaths")).toInt(), 2);
        QVERIFY(model.figures(QStringLiteral("/nowhere")).isEmpty());
    }

    void foldHidesEverythingBelow()
    {
        // F2: a folded row keeps its own place and hides every row under it.
        FolderModel model;
        model.setNodes(FolderTree::build(library()));
        QSignalSpy folded(&model, &FolderModel::foldedChanged);
        model.toggleFold(QStringLiteral("/mnt/archive"));
        QCOMPARE(folded.count(), 1);
        QCOMPARE(model.folded(), QStringList({QStringLiteral("/mnt/archive")}));
        QCOMPARE(shown(model).first(), QStringLiteral("/mnt/archive"));
        QCOMPARE(model.count(), 5);
        QCOMPARE(model.index(0).data(FolderModel::FoldedRole).toBool(), true);

        // A fold deeper down is kept while its parent is folded, and shows again after.
        model.toggleFold(QStringLiteral("/mnt/archive/games"));
        QCOMPARE(model.count(), 5);
        model.toggleFold(QStringLiteral("/mnt/archive"));
        QCOMPARE(model.count(), 6); // handheld stays hidden under the folded games
        model.toggleFold(QStringLiteral("/mnt/archive/games"));
        QCOMPARE(model.count(), 7);
        QVERIFY(model.folded().isEmpty());
    }

    void samePollChangesNothing()
    {
        FolderModel model;
        model.setNodes(FolderTree::build(library()));
        QSignalSpy reset(&model, &FolderModel::modelReset);
        QSignalSpy changed(&model, &FolderModel::dataChanged);
        QSignalSpy revision(&model, &FolderModel::revisionChanged);
        model.setNodes(FolderTree::build(library()));
        QCOMPARE(reset.count(), 0);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(revision.count(), 0);

        // Only a figure moved: the rows stay and their data changes, no reset.
        QList<Torrent> more = library();
        more << at(QStringLiteral("/mnt/fast/downloads"));
        model.setNodes(FolderTree::build(more));
        QCOMPARE(reset.count(), 0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.figures(QStringLiteral("/mnt/fast/downloads"))
                     .value(QStringLiteral("count"))
                     .toInt(),
                 2);

        // A new save path is a new row: a reset.
        more << at(QStringLiteral("/srv/media/series"));
        model.setNodes(FolderTree::build(more));
        QCOMPARE(reset.count(), 1);
        QCOMPARE(model.total(), 8);
    }

    void marksDuringAMove()
    {
        // F3: the moved folder shows the job's percentage, the save paths below it "↻".
        FolderModel model;
        model.setNodes(FolderTree::build(library()));
        QSignalSpy changed(&model, &FolderModel::dataChanged);
        model.setMarks({{QStringLiteral("/mnt/archive/games"), QStringLiteral("52 %")},
                        {QStringLiteral("/mnt/archive/games/handheld"), QStringLiteral("↻")}});
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.index(1).data(FolderModel::MarkRole).toString(), QStringLiteral("52 %"));
        QCOMPARE(model.index(2).data(FolderModel::MarkRole).toString(), QStringLiteral("↻"));
        QCOMPARE(model.index(3).data(FolderModel::MarkRole).toString(), QString());
        model.setMarks({{QStringLiteral("/mnt/archive/games"), QStringLiteral("52 %")},
                        {QStringLiteral("/mnt/archive/games/handheld"), QStringLiteral("↻")}});
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestFolderModel)
#include "tst_foldermodel.moc"
