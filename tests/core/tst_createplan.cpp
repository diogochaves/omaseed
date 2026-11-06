// core::CreatePlan (#146, DECISIONS C27, board 49): the automatic piece size against what
// qBittorrent 5.3.0 chose, the override's rows, the folder's check (CR17), the saved name
// (CR15) and the band's time left (CR13).

#include "core/CreatePlan.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace plan = omaseed::core::CreatePlan;
using omaseed::core::CreatePlan::FolderCheck;

namespace {

void write(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(size));
}

} // namespace

class TestCreatePlan : public QObject
{
    Q_OBJECT

private slots:
    void theAutomaticPieceSizeIsQBittorrents_data();
    void theAutomaticPieceSizeIsQBittorrents();
    void thePieceRows();
    void aFolderOnThisMachine();
    void aSingleFile();
    void missingEmptyOnlyHidden();
    void anUnreadableFolder();
    void theServersList();
    void names();
    void timeLeft();
};

// Board 49 §1 and the build plan: every size qBittorrent 5.3.0 was asked about on :8081.
void TestCreatePlan::theAutomaticPieceSizeIsQBittorrents_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<qint64>("piece");
    const qint64 K = 1024, M = 1024 * K, G = 1024 * M;
    QTest::newRow("100 KB") << qint64(100000) << 16 * K;
    QTest::newRow("1 MB") << qint64(1000000) << 16 * K;
    QTest::newRow("3 MB") << qint64(3000000) << 32 * K;
    QTest::newRow("5.5 MB") << qint64(5500000) << 32 * K;
    QTest::newRow("10 MB") << qint64(10000000) << 32 * K;
    QTest::newRow("50 MB") << qint64(50000000) << 128 * K;
    QTest::newRow("100 MB") << qint64(100000000) << 128 * K;
    QTest::newRow("300 MB") << qint64(300000000) << 256 * K;
    QTest::newRow("600 MB") << qint64(600000000) << 256 * K;
    QTest::newRow("1 GiB") << G << 512 * K;
    QTest::newRow("2 GiB") << 2 * G << 512 * K;
    QTest::newRow("3 GB") << qint64(3000000000) << M;
    QTest::newRow("4 GiB") << 4 * G << M;
    QTest::newRow("8 GiB") << 8 * G << M;
    QTest::newRow("12 GB") << qint64(12000000000) << 2 * M;
    QTest::newRow("16 GiB") << 16 * G << 2 * M;
    QTest::newRow("32 GiB") << 32 * G << 2 * M;
    QTest::newRow("huge") << 100000 * G << 16 * M;
}

void TestCreatePlan::theAutomaticPieceSizeIsQBittorrents()
{
    QFETCH(qint64, bytes);
    QFETCH(qint64, piece);
    QCOMPARE(plan::autoPieceSize(bytes), piece);
}

void TestCreatePlan::thePieceRows()
{
    const qint64 bytes = qint64(6400) * 1000 * 1000; // 6.4 GB
    const QList<plan::PieceRow> rows = plan::pieceRows(bytes, 14);
    QCOMPARE(rows.size(), 13); // 16 KiB … 64 MiB
    QCOMPARE(rows.first().size, plan::kMinPiece);
    QCOMPARE(rows.last().size, plan::kMaxPiece);
    int automatic = 0;
    for (const plan::PieceRow &row : rows) {
        automatic += row.automatic ? 1 : 0;
        QCOMPARE(row.count, plan::pieceCount(bytes, row.size));
        QVERIFY(row.torrentBytes >= 20 * row.count);
    }
    QCOMPARE(automatic, 1);
    const auto chosen = std::find_if(
        rows.cbegin(), rows.cend(), [](const plan::PieceRow &row) { return row.automatic; });
    QCOMPARE(chosen->size, qint64(1024 * 1024));
    QCOMPARE(chosen->count, qint64(6104)); // board 49's "about 6 100 pieces"
    QCOMPARE(plan::pieceCount(0, 16384), qint64(0));
    QCOMPARE(plan::pieceCount(16385, 16384), qint64(2));
}

void TestCreatePlan::aFolderOnThisMachine()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("photo-walks"));
    write(root + QStringLiteral("/harbour/01.jpg"), 3000);
    write(root + QStringLiteral("/harbour/02.jpg"), 2000);
    write(root + QStringLiteral("/notes.txt"), 100);
    write(root + QStringLiteral("/.DS_Store"), 50);
    write(root + QStringLiteral("/.thumbnails/01.png"), 70);
    FolderCheck check = plan::checkLocal(root);
    QCOMPARE(check.state, FolderCheck::State::Ok);
    QVERIFY(check.sized);
    QVERIFY(!check.isFile);
    QCOMPARE(check.files, 3);
    QCOMPARE(check.bytes, qint64(5100));
    QCOMPARE(check.hidden, 2);
    QCOMPARE(check.hiddenBytes, qint64(120));
    QVERIFY(check.hiddenNames.contains(QStringLiteral(".DS_Store")));

    // Hidden files in: everything counts.
    check = plan::checkLocal(root, false);
    QCOMPARE(check.files, 5);
    QCOMPARE(check.hidden, 0);
}

void TestCreatePlan::aSingleFile()
{
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("talk.mkv")), 4096);
    const FolderCheck check = plan::checkLocal(dir.filePath(QStringLiteral("talk.mkv")));
    QCOMPARE(check.state, FolderCheck::State::Ok);
    QVERIFY(check.isFile);
    QCOMPARE(check.files, 1);
    QCOMPARE(check.bytes, qint64(4096));
}

void TestCreatePlan::missingEmptyOnlyHidden()
{
    QTemporaryDir dir;
    QCOMPARE(plan::checkLocal(dir.filePath(QStringLiteral("nothing"))).state,
             FolderCheck::State::Missing);
    QDir().mkpath(dir.filePath(QStringLiteral("empty/sub")));
    QCOMPARE(plan::checkLocal(dir.filePath(QStringLiteral("empty"))).state,
             FolderCheck::State::Empty);
    write(dir.filePath(QStringLiteral("hidden/.keep")), 1);
    const FolderCheck hidden = plan::checkLocal(dir.filePath(QStringLiteral("hidden")));
    QCOMPARE(hidden.state, FolderCheck::State::OnlyHidden);
    QCOMPARE(hidden.hidden, 1);
}

void TestCreatePlan::anUnreadableFolder()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("locked"));
    write(root + QStringLiteral("/a.txt"), 10);
    QFile::setPermissions(root, QFileDevice::WriteOwner);
    const FolderCheck locked = plan::checkLocal(root);
    QFile::setPermissions(root,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QCOMPARE(locked.state, FolderCheck::State::Unreadable);

    // A file inside that can't be read: named.
    const QString inside = dir.filePath(QStringLiteral("mixed"));
    write(inside + QStringLiteral("/ok.txt"), 10);
    write(inside + QStringLiteral("/07 storm.flac"), 10);
    QFile::setPermissions(inside + QStringLiteral("/07 storm.flac"), QFileDevice::WriteOwner);
    const FolderCheck mixed = plan::checkLocal(inside);
    QFile::setPermissions(inside + QStringLiteral("/07 storm.flac"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QCOMPARE(mixed.state, FolderCheck::State::Unreadable);
    QCOMPARE(mixed.unreadableFile, QStringLiteral("07 storm.flac"));
}

void TestCreatePlan::theServersList()
{
    const FolderCheck missing = plan::fromListing(false, {});
    QCOMPARE(missing.state, FolderCheck::State::Missing);
    const FolderCheck names = plan::fromListing(true,
                                                {QStringLiteral("/srv/share/a.flac"),
                                                 QStringLiteral("/srv/share/b"),
                                                 QStringLiteral("/srv/share/.DS_Store")});
    QCOMPARE(names.state, FolderCheck::State::Ok);
    QVERIFY(!names.sized);
    QCOMPARE(names.files, 2);
    QCOMPARE(names.hidden, 1);
    QCOMPARE(plan::fromListing(true, {}).state, FolderCheck::State::Empty);
    QCOMPARE(plan::fromListing(true, {QStringLiteral("/srv/x/.keep")}).state,
             FolderCheck::State::OnlyHidden);
}

void TestCreatePlan::names()
{
    QCOMPARE(plan::savedName(QStringLiteral("field-recordings-2026"),
                             QStringLiteral("tracker-a.example")),
             QStringLiteral("field-recordings-2026 [tracker-a.example].torrent"));
    QCOMPARE(plan::savedName(QStringLiteral("a/b"), QString()), QStringLiteral("a_b.torrent"));
    QTemporaryDir dir;
    QCOMPARE(plan::freeName(dir.path(), QStringLiteral("x.torrent")), QStringLiteral("x.torrent"));
    write(dir.filePath(QStringLiteral("x.torrent")), 1);
    write(dir.filePath(QStringLiteral("x (2).torrent")), 1);
    QCOMPARE(plan::freeName(dir.path(), QStringLiteral("x.torrent")),
             QStringLiteral("x (3).torrent"));
    QCOMPARE(plan::nameOf(QStringLiteral("/mnt/media/share/photo-walks/")),
             QStringLiteral("photo-walks"));
    QCOMPARE(plan::parentOf(QStringLiteral("/mnt/media/share/photo-walks/")),
             QStringLiteral("/mnt/media/share"));
    QCOMPARE(plan::parentOf(QStringLiteral("/share")), QStringLiteral("/"));
}

void TestCreatePlan::timeLeft()
{
    QCOMPARE(plan::secondsLeft(2, 10000), qint64(-1)); // too little to tell
    QCOMPARE(plan::secondsLeft(50, 1000), qint64(-1));
    QCOMPARE(plan::secondsLeft(42, 14000), qint64(19)); // board 49: "about 20 s left"
    QCOMPARE(plan::secondsLeft(100, 30000), qint64(-1));
}

QTEST_APPLESS_MAIN(TestCreatePlan)
#include "tst_createplan.moc"
