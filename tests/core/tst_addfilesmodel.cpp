// AddFilesModel — the add sheet's file picker (docs/11 D1). Built from a real .torrent
// fixture: the tree, the check marks over a subtree, the priority ladder, the summary line and
// the groups App sends to `torrents/filePrio` after the add.

#include "core/AddFilesModel.hpp"
#include "core/TorrentFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
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

/// Writes a small file at `relative` under `dir`, making its folders.
bool writeFile(const QTemporaryDir &dir, const QString &relative)
{
    const QString path = dir.filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).path()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("stray") == 5;
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
    void toggleAllTicksAllThenNone();
    void tickingBackRestoresThePriority();
    void renamesAFile();
    void renamesAFolderAndEverythingUnderIt();
    void refusesNamesThatCannotBe();
    void refusesANameAStrayFileHoldsOnDisk();
    void renamesKeepTheChoicesAndStartOverOnLoad();
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

    // docs/27: space on a half-checked folder ticks all of it (a mixed tick reads "–", and
    // a click on it fills it); once full, it skips all of it.
    model.toggleWanted(bunny);
    QCOMPARE(roleAt(model, bunny, "wanted").toInt(), 2);
    QCOMPARE(model.wantedCount(), 8);
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

    // docs/27 D3: "none" remembered the high, "all" brings it back.
    model.setAllWanted(true);
    QCOMPARE(model.wantedCount(), 8);
    QCOMPARE(model.priority(tears), 6);
    QCOMPARE(model.priority(sintel), 1);
}

// docs/27 D1/D2: the head tick and ctrl+a: all, then none, then all again.
void TestAddFilesModel::toggleAllTicksAllThenNone()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    model.toggleWanted(sintel); // mixed overall

    model.toggleAllWanted(); // some skipped -> all
    QCOMPARE(model.wantedCount(), 8);
    model.toggleAllWanted(); // all -> none
    QCOMPARE(model.wantedCount(), 0);
    model.toggleAllWanted(); // none -> all
    QCOMPARE(model.wantedCount(), 8);
}

// docs/27 D3: a file skipped and ticked again comes back at the priority it had.
void TestAddFilesModel::tickingBackRestoresThePriority()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int tears = rowNamed(model, QStringLiteral("Tears of Steel/"));
    model.raise(tears);
    model.raise(tears); // maximal
    QCOMPARE(model.priority(tears), 7);

    model.toggleWanted(tears);
    QCOMPARE(model.priority(tears), 0);
    model.toggleWanted(tears);
    QCOMPARE(model.priority(tears), 7);

    // `x` skips too, and remembers as well.
    model.setPriority(tears, 0);
    model.toggleWanted(tears);
    QCOMPARE(model.priority(tears), 7);

    // A file that was never anything but normal comes back at normal; a new torrent forgets.
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    model.toggleWanted(sintel);
    model.toggleWanted(sintel);
    QCOMPARE(model.priority(sintel), 1);
    model.toggleWanted(tears);
    model.load(fixture("open_movies_v1.torrent").files);
    QCOMPARE(model.priority(rowNamed(model, QStringLiteral("Tears of Steel/"))), 1);
}

// M18: renaming before the add.
void TestAddFilesModel::renamesAFile()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int readme = rowNamed(model, QStringLiteral("README.txt"));
    QCOMPARE(model.plainName(readme), QStringLiteral("README.txt"));
    QCOMPARE(roleAt(model, readme, "renamed").toBool(), false);

    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    QVERIFY(model.rename(readme, QStringLiteral("read me first.txt")));
    QCOMPARE(changed.count(), 1);
    const int renamed = rowNamed(model, QStringLiteral("read me first.txt"));
    QVERIFY(renamed >= 0);
    QCOMPARE(rowNamed(model, QStringLiteral("README.txt")), -1);
    QCOMPARE(roleAt(model, renamed, "renamed").toBool(), true);
    QCOMPARE(roleAt(model, renamed, "originalName").toString(), QStringLiteral("README.txt"));

    const QVariantList renames = model.renameList();
    QCOMPARE(renames.size(), 1);
    const QVariantMap op = renames.first().toMap();
    QCOMPARE(op.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
    QCOMPARE(op.value(QStringLiteral("from")).toString(), QStringLiteral("README.txt"));
    QCOMPARE(op.value(QStringLiteral("to")).toString(), QStringLiteral("read me first.txt"));

    QCOMPARE(model.pathOf(renamed), QStringLiteral("read me first.txt"));
    QCOMPARE(model.rowOfPath(QStringLiteral("read me first.txt")), renamed);
    QCOMPARE(model.rowOfPath(QStringLiteral("README.txt")), -1);

    // The same name again changes nothing and logs nothing.
    QVERIFY(model.rename(renamed, QStringLiteral("read me first.txt")));
    QCOMPARE(model.renameList().size(), 1);
}

void TestAddFilesModel::renamesAFolderAndEverythingUnderIt()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int subs = [&] {
        // Sintel's own subs folder (Big Buck Bunny has one too).
        const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
        for (int row = sintel + 1; row < model.count(); ++row) {
            if (roleAt(model, row, "name").toString() == QLatin1String("subs/"))
                return row;
        }
        return -1;
    }();
    QVERIFY(subs >= 0);
    QVERIFY(model.rename(subs, QStringLiteral("Subtitles")));

    const int folder = rowNamed(model, QStringLiteral("Subtitles/"));
    QVERIFY(folder >= 0);
    QCOMPARE(roleAt(model, folder, "renamed").toBool(), true);
    QCOMPARE(roleAt(model, folder, "originalName").toString(), QStringLiteral("subs"));
    // The file under it moved with it, and kept its own name.
    const int srt = rowNamed(model, QStringLiteral("sintel.en.srt"));
    QCOMPARE(roleAt(model, srt, "path").toString(),
             QStringLiteral("Sintel/Subtitles/sintel.en.srt"));
    QCOMPARE(roleAt(model, srt, "renamed").toBool(), false);
    // Big Buck Bunny's subs folder is another folder and stays.
    QCOMPARE(roleAt(model, rowNamed(model, QStringLiteral("bbb.en.srt")), "path").toString(),
             QStringLiteral("Big Buck Bunny/subs/bbb.en.srt"));

    // A rename inside the renamed folder is logged with the new folder's path.
    QVERIFY(model.rename(srt, QStringLiteral("english.srt")));
    const QVariantList renames = model.renameList();
    QCOMPARE(renames.size(), 2);
    QCOMPARE(renames.at(0).toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("folder"));
    QCOMPARE(renames.at(0).toMap().value(QStringLiteral("from")).toString(),
             QStringLiteral("Sintel/subs"));
    QCOMPARE(renames.at(0).toMap().value(QStringLiteral("to")).toString(),
             QStringLiteral("Sintel/Subtitles"));
    QCOMPARE(renames.at(1).toMap().value(QStringLiteral("from")).toString(),
             QStringLiteral("Sintel/Subtitles/sintel.en.srt"));
    QCOMPARE(renames.at(1).toMap().value(QStringLiteral("to")).toString(),
             QStringLiteral("Sintel/Subtitles/english.srt"));

    const QVariantMap anchor = model.renameAnchor();
    QCOMPARE(anchor.value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(anchor.value(QStringLiteral("path")).toString(),
             QStringLiteral("Big Buck Bunny/bbb_sunflower_2160p_60fps.mp4"));
}

void TestAddFilesModel::refusesNamesThatCannotBe()
{
    AddFilesModel model;
    model.load(fixture("open_movies_v1.torrent").files);
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    QVERIFY(!model.renameError(sintel, QString()).isEmpty());
    QVERIFY(!model.renameError(sintel, QStringLiteral("   ")).isEmpty());
    QVERIFY(!model.renameError(sintel, QStringLiteral("a/b")).isEmpty());
    QVERIFY(!model.renameError(sintel, QStringLiteral("a\\b")).isEmpty());
    QVERIFY(!model.renameError(sintel, QStringLiteral("..")).isEmpty());
    // A sibling of that name, folder or file.
    QVERIFY(!model.renameError(sintel, QStringLiteral("Tears of Steel")).isEmpty());
    QVERIFY(!model.renameError(sintel, QStringLiteral("README.txt")).isEmpty());
    QVERIFY(model.renameError(sintel, QStringLiteral("Sintel (2010)")).isEmpty());
    // A name that exists elsewhere, not beside it, is fine.
    QVERIFY(model.renameError(sintel, QStringLiteral("subs")).isEmpty());

    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    QVERIFY(!model.rename(sintel, QStringLiteral("a/b")));
    QCOMPARE(changed.count(), 0);
    QVERIFY(model.renameList().isEmpty());
}

// #67: where the add saves to holds a file the torrent will not own at the new path:
// qBittorrent would replace it without a word (#64), so the name is refused as a sibling's is.
void TestAddFilesModel::refusesANameAStrayFileHoldsOnDisk()
{
    const TorrentFile torrent = fixture("open_movies_v1.torrent");
    QVERIFY(torrent.multiFile);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = torrent.name; // the torrent's own folder in the save path
    QVERIFY(writeFile(dir, root + QStringLiteral("/notes.txt")));
    QVERIFY(writeFile(dir, root + QStringLiteral("/Extras/poster.png")));
    QVERIFY(writeFile(dir, root + QStringLiteral("/README.txt"))); // its own, already there
    QStringList own;
    for (const TorrentFileEntry &file : torrent.files)
        own.append(root + QLatin1Char('/') + file.path);

    AddFilesModel model;
    model.load(torrent.files);
    const int readme = rowNamed(model, QStringLiteral("README.txt"));
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    // Without a disk to read, as before.
    QVERIFY(model.renameError(readme, QStringLiteral("notes.txt")).isEmpty());

    model.setRenameDisk(RenameDisk(dir.path(), own), root);
    QCOMPARE(model.renameError(readme, QStringLiteral("notes.txt")),
             QStringLiteral("already exists on disk"));
    QVERIFY(!model.rename(readme, QStringLiteral("notes.txt")));
    QVERIFY(model.renameList().isEmpty());
    // A folder onto one holding a stray file; an empty name is free.
    QCOMPARE(model.renameError(sintel, QStringLiteral("Extras")),
             QStringLiteral("already exists on disk"));
    QVERIFY(model.renameError(sintel, QStringLiteral("Sintel (2010)")).isEmpty());
    // The torrent's own file on disk is no stray: it may go back to its name.
    QVERIFY(model.rename(readme, QStringLiteral("read me.txt")));
    QVERIFY(model
                .renameError(rowNamed(model, QStringLiteral("read me.txt")),
                             QStringLiteral("README.txt"))
                .isEmpty());

    // "No subfolder": the paths land straight in the save path, here the root folder itself.
    QStringList ownFlat;
    for (const TorrentFileEntry &file : torrent.files)
        ownFlat.append(file.path);
    model.setRenameDisk(RenameDisk(dir.filePath(root), ownFlat), QString());
    QCOMPARE(model.renameError(rowNamed(model, QStringLiteral("read me.txt")),
                               QStringLiteral("notes.txt")),
             QStringLiteral("already exists on disk"));

    // clear() forgets the disk.
    model.clear();
    model.load(torrent.files);
    QVERIFY(
        model
            .renameError(rowNamed(model, QStringLiteral("README.txt")), QStringLiteral("notes.txt"))
            .isEmpty());
}

void TestAddFilesModel::renamesKeepTheChoicesAndStartOverOnLoad()
{
    const TorrentFile torrent = fixture("open_movies_v1.torrent");
    AddFilesModel model;
    model.load(torrent.files);
    const int sintel = rowNamed(model, QStringLiteral("Sintel/"));
    model.toggleWanted(sintel);
    QVERIFY(model.rename(sintel, QStringLiteral("Sintel (2010)")));
    // The priorities follow the file ids, not the names.
    QCOMPARE(model.priorities().at(3), 0);
    QCOMPARE(model.priorities().at(4), 0);
    QCOMPARE(roleAt(model, rowNamed(model, QStringLiteral("Sintel (2010)/")), "wanted").toInt(), 0);

    model.load(torrent.files);
    QVERIFY(model.renameList().isEmpty());
    QVERIFY(rowNamed(model, QStringLiteral("Sintel/")) >= 0);
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
