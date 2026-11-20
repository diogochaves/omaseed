// DECISIONS C47 I5: each file's kind, by board 59 A1's table (59 itself unruled), extras first
// as TorrentExtras counts them, and a split archive set counted as one.

#include "core/FileKinds.hpp"

#include <QTest>

using omaseed::core::TorrentFileEntry;
namespace FileKinds = omaseed::core::FileKinds;
using Kind = FileKinds::Kind;

namespace {

constexpr qint64 kGiB = 1024LL * 1024 * 1024;

QList<TorrentFileEntry> filesOf(const QStringList &paths, qint64 size = kGiB)
{
    QList<TorrentFileEntry> files;
    for (const QString &path : paths)
        files.append({path, size});
    return files;
}

QStringList namesOf(const QList<Kind> &kinds)
{
    QStringList names;
    for (Kind kind : kinds)
        names.append(FileKinds::name(kind));
    return names;
}

} // namespace

class TestFileKinds : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyExtensionOfTheTable_data();
    void everyExtensionOfTheTable();
    void caseIgnored();
    void whatTorrentExtrasCountsIsAnExtra();
    void aProgramDeepInsideIsOther();
    void aTopLevelExeIsAnExtra();
    void noExtensionIsOther();
    void aSplitRarSetIsOneArchive();
    void aNumberedSetIsOneArchive();
    void aOneFileTorrentHasItsKind();
    void namesAndWords();
};

void TestFileKinds::everyExtensionOfTheTable_data()
{
    QTest::addColumn<QString>("extensions");
    QTest::addColumn<QString>("kind");
    QTest::newRow("video") << "mkv mp4 m4v avi mov webm wmv ts m2ts mpg mpeg" << "video";
    QTest::newRow("audio") << "flac mp3 m4a aac ogg opus wav ape wv dsf" << "audio";
    QTest::newRow("subtitles") << "srt ass ssa sub idx vtt sup" << "subtitles";
    // Many images, and big: an image set, not TorrentExtras' stray images.
    QTest::newRow("images") << "jpg jpeg png webp gif tif tiff bmp heic" << "images";
    QTest::newRow("books") << "epub pdf mobi azw3 cbz cbr djvu" << "books";
    QTest::newRow("archives") << "zip rar 7z tar gz xz zst" << "archives";
    QTest::newRow("disc images") << "iso img bin cue nrg dmg" << "discImages";
}

void TestFileKinds::everyExtensionOfTheTable()
{
    QFETCH(QString, extensions);
    QFETCH(QString, kind);
    QStringList paths;
    for (const QString &extension : extensions.split(QLatin1Char(' ')))
        paths.append(QStringLiteral("a%1.%2").arg(paths.size()).arg(extension));
    const QStringList names = namesOf(FileKinds::classify(filesOf(paths)));
    QCOMPARE(names, QStringList(paths.size(), kind));
}

void TestFileKinds::caseIgnored()
{
    const QList<Kind> kinds =
        FileKinds::classify(filesOf({QStringLiteral("Movie.MKV"), QStringLiteral("Movie.En.SRT")}));
    QCOMPARE(kinds, (QList<Kind>{Kind::Video, Kind::Subtitles}));
}

void TestFileKinds::whatTorrentExtrasCountsIsAnExtra()
{
    const QList<Kind> kinds = FileKinds::classify(filesOf({
        QStringLiteral("Movie.mkv"),
        QStringLiteral("Movie.nfo"),
        QStringLiteral("Sample/movie-sample.mkv"), // a video by extension, a sample first
        QStringLiteral("poster.jpg"),              // a quarter of the torrent: content
    }));
    QCOMPARE(kinds.at(0), Kind::Video);
    QCOMPARE(kinds.at(1), Kind::Extras);
    QCOMPARE(kinds.at(2), Kind::Extras);
    QCOMPARE(kinds.at(3), Kind::Images);
    // A small poster is a stray image, so an extra.
    QList<TorrentFileEntry> withPoster = filesOf({QStringLiteral("Movie.mkv")});
    withPoster.append({QStringLiteral("poster.jpg"), 1024});
    QCOMPARE(FileKinds::classify(withPoster).at(1), Kind::Extras);
}

void TestFileKinds::aProgramDeepInsideIsOther()
{
    // No program kind (59 A1's program row waits for board 59): below the top level an .exe is
    // not an advert, so it is other.
    const QList<Kind> kinds = FileKinds::classify(
        filesOf({QStringLiteral("Game/setup/install.exe"), QStringLiteral("Game/data.bin")}));
    QCOMPARE(kinds.at(0), Kind::Other);
}

void TestFileKinds::aTopLevelExeIsAnExtra()
{
    const QList<Kind> kinds = FileKinds::classify(
        filesOf({QStringLiteral("codec_pack.exe"), QStringLiteral("Night Train.mkv")}));
    QCOMPARE(kinds, (QList<Kind>{Kind::Extras, Kind::Video}));
}

void TestFileKinds::noExtensionIsOther()
{
    const QList<Kind> kinds = FileKinds::classify(
        filesOf({QStringLiteral("LICENSE"), QStringLiteral(".hidden"), QStringLiteral("a.weird")}));
    QCOMPARE(kinds, (QList<Kind>{Kind::Other, Kind::Other, Kind::Other}));
}

void TestFileKinds::aSplitRarSetIsOneArchive()
{
    const QList<TorrentFileEntry> files = filesOf({
        QStringLiteral("Pack/pack.rar"),
        QStringLiteral("Pack/pack.r00"),
        QStringLiteral("Pack/pack.r01"),
        QStringLiteral("Other/other.part1.rar"),
        QStringLiteral("Other/other.part2.rar"),
        QStringLiteral("Other/other.part3.rar"),
        QStringLiteral("loose.zip"),
    });
    const QList<Kind> kinds = FileKinds::classify(files);
    QCOMPARE(kinds, QList<Kind>(files.size(), Kind::Archives));
    QCOMPARE(FileKinds::units(Kind::Archives, files, kinds), 3);
}

void TestFileKinds::aNumberedSetIsOneArchive()
{
    const QList<TorrentFileEntry> files = filesOf({
        QStringLiteral("backup.7z.001"),
        QStringLiteral("backup.7z.002"),
        QStringLiteral("backup.7z.003"),
        QStringLiteral("readme.md"),
    });
    const QList<Kind> kinds = FileKinds::classify(files);
    QCOMPARE(kinds.mid(0, 3), QList<Kind>(3, Kind::Archives));
    QCOMPARE(FileKinds::units(Kind::Archives, files, kinds), 1);
    // Other kinds count their files.
    QCOMPARE(FileKinds::units(Kind::Other, files, kinds), 1);
}

void TestFileKinds::aOneFileTorrentHasItsKind()
{
    // TorrentExtras counts nothing in a one-file torrent: even an .nfo is its own content.
    QCOMPARE(FileKinds::classify(filesOf({QStringLiteral("Film.mkv")})), QList<Kind>{Kind::Video});
    QCOMPARE(FileKinds::classify(filesOf({QStringLiteral("notes.nfo")})), QList<Kind>{Kind::Other});
}

void TestFileKinds::namesAndWords()
{
    for (Kind kind : FileKinds::all())
        QCOMPARE(FileKinds::fromName(FileKinds::name(kind)), std::optional<Kind>(kind));
    QVERIFY(!FileKinds::fromName(QStringLiteral("mixed")));
    QCOMPARE(FileKinds::word(Kind::Audio, false), QStringLiteral("audio file"));
    QCOMPARE(FileKinds::word(Kind::DiscImages, true), QStringLiteral("disc images"));
    QCOMPARE(FileKinds::word(Kind::Other, true), QStringLiteral("other files"));
}

QTEST_MAIN(TestFileKinds)
#include "tst_filekinds.moc"
