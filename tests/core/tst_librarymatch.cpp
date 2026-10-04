#include "core/LibraryMatch.hpp"

#include <QTest>

#include <algorithm>

using omaseed::core::TorrentFileEntry;
namespace LibraryMatch = omaseed::core::LibraryMatch;

namespace {

QList<TorrentFileEntry> album(const QString &prefix = QString())
{
    return {{prefix + QStringLiteral("01 - Harbour at Dawn.flac"), 4320000},
            {prefix + QStringLiteral("02 - Rain on Tin.flac"), 8950000},
            {prefix + QStringLiteral("scans/cover.jpg"), 412000}};
}

} // namespace

class TestLibraryMatch : public QObject
{
    Q_OBJECT

private slots:
    void sameFolderSameNameSavesBesideIt();
    void sameFilesInAFolderNamedOtherwise();
    void candidateWithoutAFolderOfItsOwn();
    void singleFileSameNameAndSize();
    void singleFileRenamedIsNoMatch();
    void aSizeThatDiffersIsNoMatch();
    void aMissingOrExtraFileIsNoMatch();
    void aRenamedFileIsNoMatch();
    void orderDoesNotMatter();
    void trailingSlashesAreIgnored();
};

void TestLibraryMatch::sameFolderSameNameSavesBesideIt()
{
    const LibraryMatch::Candidate candidate{QStringLiteral("/media/music/"),
                                            QStringLiteral("/media/music/Field Recordings"),
                                            album(QStringLiteral("Field Recordings/"))};
    const auto result =
        LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate);
    QVERIFY(result.exact);
    QCOMPARE(result.savePath, QStringLiteral("/media/music"));
    QCOMPARE(result.layout, QStringLiteral("Original"));
}

void TestLibraryMatch::sameFilesInAFolderNamedOtherwise()
{
    // Tracker B named its upload differently: the files land in A's folder by saving into
    // it without a folder of the new torrent's own.
    const LibraryMatch::Candidate candidate{QStringLiteral("/media/music"),
                                            QStringLiteral("/media/music/Field Recordings"),
                                            album(QStringLiteral("Field Recordings/"))};
    const auto result =
        LibraryMatch::match(QStringLiteral("Field.Recordings.FLAC"), true, album(), candidate);
    QVERIFY(result.exact);
    QCOMPARE(result.savePath, QStringLiteral("/media/music/Field Recordings"));
    QCOMPARE(result.layout, QStringLiteral("NoSubfolder"));
}

void TestLibraryMatch::candidateWithoutAFolderOfItsOwn()
{
    const LibraryMatch::Candidate candidate{
        QStringLiteral("/media/music/fr"), QStringLiteral("/media/music/fr"), album()};
    const auto result =
        LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate);
    QVERIFY(result.exact);
    QCOMPARE(result.savePath, QStringLiteral("/media/music/fr"));
    QCOMPARE(result.layout, QStringLiteral("NoSubfolder"));
}

void TestLibraryMatch::singleFileSameNameAndSize()
{
    const QList<TorrentFileEntry> mine{{QStringLiteral("distro.iso"), 480000000}};
    const LibraryMatch::Candidate candidate{
        QStringLiteral("/srv/iso"), QStringLiteral("/srv/iso/distro.iso"), mine};
    const auto result = LibraryMatch::match(QStringLiteral("distro.iso"), false, mine, candidate);
    QVERIFY(result.exact);
    QCOMPARE(result.savePath, QStringLiteral("/srv/iso"));
    QCOMPARE(result.layout, QStringLiteral("Original"));
}

void TestLibraryMatch::singleFileRenamedIsNoMatch()
{
    const QList<TorrentFileEntry> mine{{QStringLiteral("distro.iso"), 480000000}};
    const LibraryMatch::Candidate candidate{QStringLiteral("/srv/iso"),
                                            QStringLiteral("/srv/iso/Distro 2026.iso"),
                                            {{QStringLiteral("Distro 2026.iso"), 480000000}}};
    QVERIFY(!LibraryMatch::match(QStringLiteral("distro.iso"), false, mine, candidate).exact);
}

void TestLibraryMatch::aSizeThatDiffersIsNoMatch()
{
    QList<TorrentFileEntry> theirs = album(QStringLiteral("Field Recordings/"));
    theirs[1].size += 1;
    const LibraryMatch::Candidate candidate{
        QStringLiteral("/media/music"), QStringLiteral("/media/music/Field Recordings"), theirs};
    QVERIFY(
        !LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate).exact);
}

void TestLibraryMatch::aMissingOrExtraFileIsNoMatch()
{
    QList<TorrentFileEntry> fewer = album(QStringLiteral("Field Recordings/"));
    fewer.removeLast();
    LibraryMatch::Candidate candidate{
        QStringLiteral("/media/music"), QStringLiteral("/media/music/Field Recordings"), fewer};
    QVERIFY(
        !LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate).exact);

    QList<TorrentFileEntry> more = album(QStringLiteral("Field Recordings/"));
    more.append({QStringLiteral("Field Recordings/extra.nfo"), 900});
    candidate.files = more;
    QVERIFY(
        !LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate).exact);
}

void TestLibraryMatch::aRenamedFileIsNoMatch()
{
    QList<TorrentFileEntry> theirs = album(QStringLiteral("Field Recordings/"));
    theirs[0].path = QStringLiteral("Field Recordings/01 Harbour at Dawn.flac");
    const LibraryMatch::Candidate candidate{
        QStringLiteral("/media/music"), QStringLiteral("/media/music/Field Recordings"), theirs};
    QVERIFY(
        !LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate).exact);
}

void TestLibraryMatch::orderDoesNotMatter()
{
    QList<TorrentFileEntry> theirs = album(QStringLiteral("Field Recordings/"));
    std::reverse(theirs.begin(), theirs.end());
    const LibraryMatch::Candidate candidate{
        QStringLiteral("/media/music"), QStringLiteral("/media/music/Field Recordings"), theirs};
    QVERIFY(
        LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate).exact);
}

void TestLibraryMatch::trailingSlashesAreIgnored()
{
    const LibraryMatch::Candidate candidate{QStringLiteral("/media/music/"),
                                            QStringLiteral("/media/music/Field Recordings/"),
                                            album(QStringLiteral("Field Recordings/"))};
    const auto result =
        LibraryMatch::match(QStringLiteral("Field Recordings"), true, album(), candidate);
    QVERIFY(result.exact);
    QCOMPARE(result.savePath, QStringLiteral("/media/music"));
}

QTEST_APPLESS_MAIN(TestLibraryMatch)

#include "tst_librarymatch.moc"
