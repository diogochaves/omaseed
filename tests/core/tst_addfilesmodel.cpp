// AddFilesModel — the add sheet's file picker (docs/11 D1). Built from a real .torrent
// fixture: the tree, the check marks over a subtree, the priority ladder, the summary line and
// the groups App sends to `torrents/filePrio` after the add.

#include "core/AddFilesModel.hpp"
#include "core/TorrentFile.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;

namespace {

TorrentFile fixture(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return TorrentFile::parse(file.readAll());
}

QVariant roleAt(const AddFilesModel &model, int row, const char *role)
{
    const QByteArray wanted(role);
    const auto names = model.roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == wanted)
            return model.data(model.index(row, 0), it.key());
    }
    return {};
}

/// The row whose `name` role is `name` ("Sintel/" for a folder), or -1.
int rowNamed(const AddFilesModel &model, const QString &name)
{
    for (int row = 0; row < model.count(); ++row) {
        if (roleAt(model, row, "name").toString() == name)
            return row;
    }
    return -1;
}

} // namespace

class TestAddFilesModel : public QObject
{
    Q_OBJECT

private slots:
    void loadsTheTreeAllNormal();
    void spaceSkipsAFolderAndBringsItBack();
    void partlySkippedFolderIsHalfChecked();
    void raiseAndLowerWalkTheLadder();
    void clickCyclesPriorityAndKeepsSkipsSkipped();
    void clickOnASkippedRowBringsItBackAtNormal();
    void allAndNone();
    void groupsLeaveNormalOut();
    void foldingKeepsTheChoices();
    void loadingAnotherTorrentStartsOver();
    void padFilesNeverShow();
};

void TestAddFilesModel::loadsTheTreeAllNormal()
{
    const TorrentFile torrent = fixture("open_movies_v1.torrent");
    QVERIFY(torrent.isValid());

    AddFilesModel model;
    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    model.load(torrent.files);

    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.fileCount(), 8);
    QCOMPARE(model.wantedCount(), 8);
    QCOMPARE(model.wantedSize(), torrent.totalSize);
    QVERIFY(!model.isCustomised());
    // Four folders, their subs folders, and README.txt at the top level, every folder open.
    QCOMPARE(roleAt(model, 0, "name").toString(), QStringLiteral("Big Buck Bunny/"));
    QVERIFY(model.isFolder(0));
    QVERIFY(model.isExpanded(0));
    QCOMPARE(roleAt(model, 0, "wanted").toInt(), 2);
    QCOMPARE(roleAt(model, 0, "priority").toInt(), 1);
    QVERIFY(rowNamed(model, QStringLiteral("README.txt")) >= 0);
    QCOMPARE(model.depthAt(0), 0);
    QCOMPARE(model.depthAt(rowNamed(model, QStringLiteral("bbb.en.srt"))), 2);
    QVERIFY(model.summaryText().startsWith(QStringLiteral("8 files · ")));
    QCOMPARE(model.priorityList().size(), 8);
}

void TestAddFilesModel::spaceSkipsAFolderAndBringsItBack()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);

    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    QVERIFY(sintel >= 0);
    model.toggleWanted(sintel);

    QCOMPARE(roleAt(model, sintel, "wanted").toInt(), 0);
    QCOMPARE(roleAt(model, sintel, "priority").toInt(), 0);
    QCOMPARE(model.wantedCount(), 6); // sintel_4k.mkv and sintel.en.srt
    QVERIFY(model.isCustomised());
    QVERIFY(model.summaryText().startsWith(QStringLiteral("6 of 8 files · ")));
    // Ids 3 and 4 in the metainfo's order.
    QCOMPARE(model.priorities().at(3), 0);
    QCOMPARE(model.priorities().at(4), 0);
    QCOMPARE(model.priorities().at(5), 1);

    model.toggleWanted(sintel);
    QCOMPARE(roleAt(model, sintel, "wanted").toInt(), 2);
    QCOMPARE(model.wantedCount(), 8);
    QVERIFY(!model.isCustomised());
}

void TestAddFilesModel::partlySkippedFolderIsHalfChecked()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);

    const int srt = rowNamed(model, QStringLiteral("bbb.de.srt"));
    QVERIFY(srt >= 0);
    QSignalSpy data(&model, &QAbstractItemModel::dataChanged);
    model.toggleWanted(srt);
    QVERIFY(data.count() > 0);

    const int bunny = rowNamed(model, QStringLiteral("Big Buck Bunny/"));
    QCOMPARE(roleAt(model, bunny, "wanted").toInt(), 1);
    QCOMPARE(roleAt(model, bunny, "priority").toInt(), -1); // mixed

    // Space on a half-checked folder skips all of it (anything wanted -> skip).
    model.toggleWanted(bunny);
    QCOMPARE(roleAt(model, bunny, "wanted").toInt(), 0);
    QCOMPARE(model.wantedCount(), 5);
}

void TestAddFilesModel::raiseAndLowerWalkTheLadder()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int tears = rowNamed(model, QStringLiteral("Tears of Steel/"));

    model.raise(tears);
    QCOMPARE(model.priority(tears), 6);
    model.raise(tears);
    QCOMPARE(model.priority(tears), 7);
    model.raise(tears);
    QCOMPARE(model.priority(tears), 7);
    model.lower(tears);
    model.lower(tears);
    QCOMPARE(model.priority(tears), 1);
    model.lower(tears);
    QCOMPARE(model.priority(tears), 0);
    QCOMPARE(roleAt(model, tears, "wanted").toInt(), 0);
    model.setPriority(tears, 1);
    QVERIFY(!model.isCustomised());
}

// M17: a click on the priority text cycles normal -> high -> maximal -> normal, on the files
// that are wanted; the ones the folder's check mark skipped stay skipped.
void TestAddFilesModel::clickCyclesPriorityAndKeepsSkipsSkipped()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int srt = rowNamed(model, QStringLiteral("bbb.de.srt"));
    model.toggleWanted(srt); // skipped
    const int bunny = rowNamed(model, QStringLiteral("Big Buck Bunny/"));
    QCOMPARE(roleAt(model, bunny, "wanted").toInt(), 1);

    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    model.cyclePriority(bunny);
    QCOMPARE(changed.count(), 1);
    const int mkv = rowNamed(model, QStringLiteral("bbb_sunflower_2160p_60fps.mp4"));
    QVERIFY(mkv >= 0);
    QCOMPARE(model.priority(mkv), 6);
    QCOMPARE(model.priority(srt), 0); // still skipped
    QCOMPARE(roleAt(model, bunny, "wanted").toInt(), 1);

    model.cyclePriority(bunny);
    QCOMPARE(model.priority(mkv), 7);
    model.cyclePriority(bunny);
    QCOMPARE(model.priority(mkv), 1);
    QCOMPARE(model.priority(srt), 0);

    // A row whose wanted files disagree starts over at normal.
    model.cyclePriority(mkv);
    QCOMPARE(model.priority(mkv), 6);
    model.cyclePriority(bunny);
    QCOMPARE(model.priority(mkv), 1);
    QCOMPARE(model.priority(srt), 0);
}

void TestAddFilesModel::clickOnASkippedRowBringsItBackAtNormal()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    model.toggleWanted(sintel);
    QCOMPARE(roleAt(model, sintel, "wanted").toInt(), 0);

    model.cyclePriority(sintel);
    QCOMPARE(roleAt(model, sintel, "wanted").toInt(), 2);
    QCOMPARE(model.priority(sintel), 1);
    QCOMPARE(model.wantedCount(), 8);
}

// The header's "all" and "none".
void TestAddFilesModel::allAndNone()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int tears = rowNamed(model, QStringLiteral("Tears of Steel/"));
    model.raise(tears); // high
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    model.toggleWanted(sintel); // skipped

    model.setAllWanted(true);
    QCOMPARE(model.wantedCount(), 8);
    QCOMPARE(model.priority(tears), 6); // kept
    QCOMPARE(model.priority(sintel), 1);

    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    model.setAllWanted(false);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.wantedCount(), 0);
    for (int row = 0; row < model.count(); ++row)
        QCOMPARE(roleAt(model, row, "wanted").toInt(), 0);
    model.setAllWanted(false); // nothing to change: no signal
    QCOMPARE(changed.count(), 1);

    model.setAllWanted(true);
    QCOMPARE(model.wantedCount(), 8);
    QVERIFY(!model.isCustomised()); // "none" dropped the high, "all" brings back normal
}

void TestAddFilesModel::groupsLeaveNormalOut()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    QVERIFY(AddFilesModel::priorityGroups(model.priorityList()).isEmpty());

    model.toggleWanted(rowNamed(model, QStringLiteral("Sintel/")));
    model.raise(rowNamed(model, QStringLiteral("README.txt")));
    model.raise(rowNamed(model, QStringLiteral("README.txt")));

    const QMap<int, QList<int>> groups = AddFilesModel::priorityGroups(model.priorityList());
    QCOMPARE(groups.size(), 2);
    QCOMPARE(groups.value(0), (QList<int>{3, 4}));
    QCOMPARE(groups.value(7), (QList<int>{7}));
    QVERIFY(!groups.contains(1));
}

void TestAddFilesModel::foldingKeepsTheChoices()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int before = model.count();

    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    model.toggleWanted(sintel);
    model.setExpanded(sintel, false);
    QVERIFY(!model.isExpanded(sintel));
    QVERIFY(model.count() < before);
    QCOMPARE(roleAt(model, sintel, "wanted").toInt(), 0);

    model.setExpanded(sintel, true);
    QCOMPARE(model.count(), before);
    QCOMPARE(model.wantedCount(), 6);
}

void TestAddFilesModel::loadingAnotherTorrentStartsOver()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    model.toggleWanted(0);
    model.setExpanded(0, false);

    model.load(fixture("multi_file_v1_unicode.torrent").files);
    QCOMPARE(model.fileCount(), 3);
    QCOMPARE(model.wantedCount(), 3);
    QVERIFY(!model.isCustomised());
    QVERIFY(model.isExpanded(0));

    QSignalSpy count(&model, &AddFilesModel::countChanged);
    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.fileCount(), 0);
    QVERIFY(model.summaryText().isEmpty());
    QCOMPARE(count.count(), 1);
}

void TestAddFilesModel::padFilesNeverShow()
{
    AddFilesModel model;
    model.load(fixture("multi_file_padded_v1.torrent").files);
    QCOMPARE(model.fileCount(), 2);
    QCOMPARE(rowNamed(model, QStringLiteral(".pad/")), -1);
    // notes.txt is file id 1 on the server, right after disc/one.bin.
    const int notes = rowNamed(model, QStringLiteral("notes.txt"));
    model.toggleWanted(notes);
    QCOMPARE(AddFilesModel::priorityGroups(model.priorityList()).value(0), (QList<int>{1}));
}

QTEST_GUILESS_MAIN(TestAddFilesModel)

#include "tst_addfilesmodel.moc"
