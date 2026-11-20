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

#include <algorithm>

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
    void transmissionsSetCyclesThroughLow();
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
    void filesTooBigForFat32AreNamedAndSkipped();
    void extrasAreSaidAndMarkedNeverUnticked();

    // DECISIONS C47 I3–I6: the tree's view state, reached through the proxy.
    void treeIsExposed();
    void sortThroughTheProxyKeepsTicksAndRenames();
    void flatThroughTheProxyTicksTheRightFile();
    void toggleKindSkipsThenRestoresPriorities();
    void aFilteredFolderTicksOnlyWhatItShows();
    void loadKeepsSortFlatAndHideSkipped();
    void loadDropsKindFilterAndFolds();
};

void TestAddFilesModel::filesTooBigForFat32AreNamedAndSkipped()
{
    // DECISIONS C17 D9: the wanted files of the limit or more, named, and "Skip these".
    const TorrentFile torrent = fixture("open_movies_v1.torrent");
    QVERIFY(torrent.isValid());
    AddFilesModel model;
    model.load(torrent.files);
    qint64 biggest = 0;
    for (const auto &file : torrent.files)
        biggest = std::max(biggest, file.size);
    const QVariantList big = model.wantedFilesOfAtLeast(biggest);
    QCOMPARE(big.size(), 1);
    QCOMPARE(big.first().toMap().value(QStringLiteral("size")).toLongLong(), biggest);
    QVERIFY(model.wantedFilesOfAtLeast(biggest + 1).isEmpty());

    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    model.skipFilesOfAtLeast(biggest);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.wantedCount(), 7);
    QVERIFY(model.wantedFilesOfAtLeast(biggest).isEmpty());
    model.skipFilesOfAtLeast(biggest); // nothing left to skip: no change
    QCOMPARE(changed.count(), 1);
}

void TestAddFilesModel::extrasAreSaidAndMarkedNeverUnticked()
{
    // #63 (DECISIONS C28): the line under the tree and each extra's meta; nothing unticked.
    constexpr qint64 kMiB = 1024 * 1024;
    const QList<TorrentFileEntry> files{
        {QStringLiteral("Field Recordings/01 Rain.flac"), 400 * kMiB},
        {QStringLiteral("Field Recordings/02 Wind.flac"), 380 * kMiB},
        {QStringLiteral("Field Recordings/release.nfo"), 4096},
        {QStringLiteral("Field Recordings/Sample/rain-sample.flac"), 12 * kMiB - 4096},
    };
    AddFilesModel model;
    model.load(files);
    QCOMPARE(model.extraIds(), QList<int>({2, 3}));
    QCOMPARE(model.extrasText(),
             QStringLiteral("2 extras: an NFO and a sample (12.0 MB). Ticked, so the torrent "
                            "seeds whole."));
    QCOMPARE(model.wantedCount(), 4);
    QVERIFY(!model.isCustomised());
    const int nfo = rowNamed(model, QStringLiteral("release.nfo"));
    const int rain = rowNamed(model, QStringLiteral("01 Rain.flac"));
    const int sampleFolder = rowNamed(model, QStringLiteral("Sample/"));
    QVERIFY(nfo >= 0 && rain >= 0 && sampleFolder >= 0);
    QCOMPARE(roleAt(model, nfo, "extra").toBool(), true);
    QCOMPARE(roleAt(model, rain, "extra").toBool(), false);
    QCOMPARE(roleAt(model, sampleFolder, "extra").toBool(), false); // files only
    QCOMPARE(roleAt(model, rowNamed(model, QStringLiteral("rain-sample.flac")), "extra").toBool(),
             true);

    // Skipped by hand: still named, no longer "ticked".
    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    model.toggleWanted(nfo);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.extrasText(), QStringLiteral("2 extras: an NFO and a sample (12.0 MB)."));
    model.toggleWanted(nfo);
    QVERIFY(model.extrasText().endsWith(QStringLiteral("Ticked, so the torrent seeds whole.")));

    // One, and three kinds in the order the line names them.
    model.load({files.at(0), files.at(1), files.at(2)});
    QCOMPARE(model.extrasText(),
             QStringLiteral("1 extra: an NFO (4 KB). Ticked, so the torrent seeds whole."));
    model.load({{QStringLiteral("disc.iso"), 900 * kMiB},
                {QStringLiteral("a.nfo"), 1024},
                {QStringLiteral("b.nfo"), 1024},
                {QStringLiteral("checksums.sfv"), 1024},
                {QStringLiteral("Visit our site.url"), 1024}});
    QCOMPARE(model.extrasText(),
             QStringLiteral("4 extras: 2 NFOs, a checksum file and an advert (4 KB). Ticked, "
                            "so the torrent seeds whole."));

    // None: no line; a cleared sheet has none either.
    model.load({files.at(0), files.at(1)});
    QVERIFY(model.extrasText().isEmpty());
    model.load(files);
    model.clear();
    QVERIFY(model.extrasText().isEmpty());
    QVERIFY(model.extraIds().isEmpty());
}

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

// C25 K17: on Transmission the keys and the click walk skip · low · normal · high.
void TestAddFilesModel::transmissionsSetCyclesThroughLow()
{
    AddFilesModel model;
    model.setPrioritySet({0, 2, 1, 6});
    model.load(fixture("open_movies_v1.torrent").files);
    const int mkv = rowNamed(model, QStringLiteral("bbb_sunflower_2160p_60fps.mp4"));
    model.cyclePriority(mkv);
    QCOMPARE(model.priority(mkv), 6);
    model.cyclePriority(mkv);
    QCOMPARE(model.priority(mkv), 2); // high is the top: round to low
    model.cyclePriority(mkv);
    QCOMPARE(model.priority(mkv), 1);
    model.raise(mkv);
    model.raise(mkv);
    QCOMPARE(model.priority(mkv), 6); // never maximal
    model.lower(mkv);
    model.lower(mkv);
    QCOMPARE(model.priority(mkv), 2);
    model.lower(mkv);
    QCOMPARE(model.priority(mkv), 0);
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

#include "tst_addfilesmodel.moc" // --- C47 ---------------------------------------------------------------------------------

namespace {

/// A show: two episodes, their subtitles, an NFO and a bonus video. Ids in this order.
QList<TorrentFileEntry> showFiles()
{
    return {
        {QStringLiteral("Show/ep1.mkv"), 900},
        {QStringLiteral("Show/ep2.mkv"), 1000},
        {QStringLiteral("Show/Subs/ep1.en.srt"), 10},
        {QStringLiteral("Show/Subs/ep2.en.srt"), 10},
        {QStringLiteral("Show/release.nfo"), 5},
        {QStringLiteral("Bonus/making.mkv"), 500},
    };
}

constexpr int kSkipped = int(FilePriority::Skip);
constexpr int kNormalP = int(FilePriority::Normal);
constexpr int kHighP = int(FilePriority::High);

} // namespace

void TestAddFilesModel::treeIsExposed()
{
    AddFilesModel model;
    model.load(showFiles());
    TorrentFilesModel *tree = model.tree();
    QVERIFY(tree);
    QCOMPARE(model.property("tree").value<TorrentFilesModel *>(), tree);
    QCOMPARE(tree->rowCount(), model.rowCount());
    // Its view state drives the proxy's rows.
    tree->setKindFilter(QStringLiteral("subtitles"));
    QCOMPARE(model.rowCount(), tree->rowCount());
    QCOMPARE(model.rowCount(), 4); // Show/, Subs/ and the two subtitles
}

void TestAddFilesModel::sortThroughTheProxyKeepsTicksAndRenames()
{
    AddFilesModel model;
    model.load(showFiles());
    model.toggleWanted(model.rowOfPath(QStringLiteral("Show/ep1.mkv")));
    QVERIFY(
        model.rename(model.rowOfPath(QStringLiteral("Show/ep2.mkv")), QStringLiteral("Two.mkv")));

    model.tree()->sortBy(QStringLiteral("size"));
    const int one = model.rowOfPath(QStringLiteral("Show/ep1.mkv"));
    const int two = model.rowOfPath(QStringLiteral("Show/Two.mkv"));
    QVERIFY(one >= 0 && two >= 0);
    QVERIFY(two < one); // largest first
    QCOMPARE(roleAt(model, one, "wanted").toInt(), 0);
    QCOMPARE(roleAt(model, two, "wanted").toInt(), 2);
    QVERIFY(roleAt(model, two, "renamed").toBool());
    QCOMPARE(roleAt(model, two, "originalName").toString(), QStringLiteral("ep2.mkv"));
    QCOMPARE(model.priorities(),
             (QList<int>{kSkipped, kNormalP, kNormalP, kNormalP, kNormalP, kNormalP}));
}

void TestAddFilesModel::flatThroughTheProxyTicksTheRightFile()
{
    AddFilesModel model;
    model.load(showFiles());
    model.tree()->setFlat(true);
    model.tree()->sortBy(QStringLiteral("size")); // ep2 (1000), ep1, making, the rest
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.pathOf(0), QStringLiteral("Show/ep2.mkv"));
    QCOMPARE(roleAt(model, 0, "folderPath").toString(), QStringLiteral("Show"));
    model.toggleWanted(0);
    QCOMPARE(model.priorities().at(1), kSkipped);
    QCOMPARE(model.wantedCount(), 5);
    QCOMPARE(roleAt(model, 0, "wanted").toInt(), 0);
}

void TestAddFilesModel::toggleKindSkipsThenRestoresPriorities()
{
    AddFilesModel model;
    model.load(showFiles());
    model.raise(model.rowOfPath(QStringLiteral("Show/ep1.mkv")));
    QCOMPARE(model.priorities().at(0), kHighP);
    // A filter on the subtitles does not narrow the box: the whole kind goes (C47 I5).
    model.tree()->setKindFilter(QStringLiteral("subtitles"));

    QSignalSpy changed(&model, &AddFilesModel::choicesChanged);
    model.toggleKindWanted(QStringLiteral("video"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.priorities(),
             (QList<int>{kSkipped, kSkipped, kNormalP, kNormalP, kNormalP, kSkipped}));
    QCOMPARE(model.tree()->kinds().first().toMap().value(QStringLiteral("wanted")).toInt(), 0);

    // Some skipped: every one ticked back, ep1 at high again.
    model.toggleKindWanted(QStringLiteral("video"));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(model.priorities(),
             (QList<int>{kHighP, kNormalP, kNormalP, kNormalP, kNormalP, kNormalP}));

    // One skipped by hand: the box ticks them all, it doesn't skip.
    model.tree()->setKindFilter(QString());
    model.toggleWanted(model.rowOfPath(QStringLiteral("Bonus/making.mkv")));
    model.toggleKindWanted(QStringLiteral("video"));
    QCOMPARE(model.priorities().at(5), kNormalP);

    // A kind the torrent hasn't: nothing.
    model.toggleKindWanted(QStringLiteral("books"));
    QCOMPARE(changed.count(), 4);
}

void TestAddFilesModel::aFilteredFolderTicksOnlyWhatItShows()
{
    AddFilesModel model;
    model.load(showFiles());
    model.tree()->setFilterText(QStringLiteral("ep1"));
    model.toggleWanted(model.rowOfPath(QStringLiteral("Show")));
    QCOMPARE(model.priorities(),
             (QList<int>{kSkipped, kNormalP, kSkipped, kNormalP, kNormalP, kNormalP}));
}

void TestAddFilesModel::loadKeepsSortFlatAndHideSkipped()
{
    AddFilesModel model;
    model.load(showFiles());
    TorrentFilesModel *tree = model.tree();
    tree->sortBy(QStringLiteral("size"));
    tree->setFlat(true);
    tree->setFoldersFirst(false);
    tree->setHideSkipped(true);

    model.load(showFiles());
    QCOMPARE(tree->sortKey(), QStringLiteral("size"));
    QVERIFY(tree->sortDescending());
    QVERIFY(tree->flat());
    QVERIFY(!tree->foldersFirst());
    QVERIFY(tree->hideSkipped());
    QCOMPARE(model.pathOf(0), QStringLiteral("Show/ep2.mkv"));
}

void TestAddFilesModel::loadDropsKindFilterAndFolds()
{
    AddFilesModel model;
    model.load(showFiles());
    TorrentFilesModel *tree = model.tree();
    model.setExpanded(model.rowOfPath(QStringLiteral("Bonus")), false);
    QCOMPARE(model.rowOfPath(QStringLiteral("Bonus/making.mkv")), -1);
    tree->setKindFilter(QStringLiteral("video"));
    tree->setFilterText(QStringLiteral("ep"));

    model.load(showFiles());
    QCOMPARE(tree->kindFilter(), QString());
    QCOMPARE(tree->filterText(), QString());
    QVERIFY(model.rowOfPath(QStringLiteral("Bonus/making.mkv")) >= 0);
    QCOMPARE(model.rowCount(), 9); // Show/, Subs/, Bonus/ and the six files
}
