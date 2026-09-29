#include "core/TorrentFile.hpp"
#include "core/TorrentFolderScan.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using omaseed::core::FolderTorrent;
using omaseed::core::TorrentFile;
using omaseed::core::TorrentFolderScan;

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/// Writes `bytes` to `dir/name` and dates it `minutesAgo` minutes back.
bool put(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes, int minutesAgo)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return false;
    // Flushed first: a write still buffered at close() would move the time back to now.
    if (!file.flush())
        return false;
    return file.setFileTime(QDateTime::currentDateTime().addSecs(-60LL * minutesAgo),
                            QFileDevice::FileModificationTime);
}

QStringList fileNames(const QList<FolderTorrent> &torrents)
{
    QStringList names;
    for (const FolderTorrent &torrent : torrents)
        names.append(QFileInfo(torrent.path).fileName());
    return names;
}

} // namespace

class TestTorrentFolderScan : public QObject
{
    Q_OBJECT

private slots:
    void readsTheMetainfo();
    void newestFirst();
    void capsAtTheLimit();
    void skipsWhatDoesNotParse();
    void ignoresOtherFilesAndSubfolders();
    void matchesTheSuffixInAnyCase();
    void aMissingFolderIsEmpty();
};

void TestTorrentFolderScan::readsTheMetainfo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray bytes = fixture(QStringLiteral("single_file_v1.torrent"));
    QVERIFY(!bytes.isEmpty());
    QVERIFY(put(dir, QStringLiteral("saved-by-the-browser.torrent"), bytes, 5));

    const QList<FolderTorrent> found = TorrentFolderScan::scan(dir.path());
    QCOMPARE(found.size(), 1);
    const TorrentFile parsed = TorrentFile::parse(bytes);
    QCOMPARE(found[0].path, dir.filePath(QStringLiteral("saved-by-the-browser.torrent")));
    QCOMPARE(found[0].name, parsed.name);
    QCOMPARE(found[0].size, parsed.totalSize);
    QCOMPARE(found[0].infoHashV1, parsed.infoHashV1);
    QCOMPARE(found[0].infoHashV2, parsed.infoHashV2);
    QVERIFY(found[0].modified.isValid());
    QVERIFY(qAbs(found[0].modified.secsTo(QDateTime::currentDateTime()) - 300) < 5);
}

void TestTorrentFolderScan::newestFirst()
{
    QTemporaryDir dir;
    const QByteArray bytes = fixture(QStringLiteral("single_file_v1.torrent"));
    QVERIFY(put(dir, QStringLiteral("three-hours.torrent"), bytes, 180));
    QVERIFY(put(dir, QStringLiteral("one-hour.torrent"), bytes, 60));
    QVERIFY(put(dir, QStringLiteral("two-hours.torrent"), bytes, 120));

    QCOMPARE(fileNames(TorrentFolderScan::scan(dir.path())),
             QStringList({QStringLiteral("one-hour.torrent"),
                          QStringLiteral("two-hours.torrent"),
                          QStringLiteral("three-hours.torrent")}));
}

void TestTorrentFolderScan::capsAtTheLimit()
{
    QTemporaryDir dir;
    const QByteArray bytes = fixture(QStringLiteral("multi_file_v1_unicode.torrent"));
    for (int i = 1; i <= 7; ++i)
        QVERIFY(put(dir, QStringLiteral("t%1.torrent").arg(i), bytes, i * 10));

    QCOMPARE(fileNames(TorrentFolderScan::scan(dir.path())),
             QStringList({QStringLiteral("t1.torrent"),
                          QStringLiteral("t2.torrent"),
                          QStringLiteral("t3.torrent"),
                          QStringLiteral("t4.torrent"),
                          QStringLiteral("t5.torrent")}));
    QCOMPARE(TorrentFolderScan::scan(dir.path(), 2).size(), 2);
    QCOMPARE(TorrentFolderScan::scan(dir.path(), 0).size(), 0);
}

void TestTorrentFolderScan::skipsWhatDoesNotParse()
{
    QTemporaryDir dir;
    const QByteArray bytes = fixture(QStringLiteral("single_file_v1.torrent"));
    for (int i = 1; i <= 5; ++i)
        QVERIFY(put(dir, QStringLiteral("good%1.torrent").arg(i), bytes, 10 + i));
    // The newest ones: an HTML error page saved as .torrent, a truncated download, an empty
    // file. None of them counts towards the five.
    QVERIFY(put(dir, QStringLiteral("error-page.torrent"), "<html>403</html>", 1));
    QVERIFY(put(dir, QStringLiteral("truncated.torrent"), bytes.left(bytes.size() / 2), 2));
    QVERIFY(put(dir, QStringLiteral("empty.torrent"), QByteArray(), 3));

    const QStringList names = fileNames(TorrentFolderScan::scan(dir.path()));
    QCOMPARE(names.size(), 5);
    for (const QString &name : names)
        QVERIFY2(name.startsWith(QStringLiteral("good")), qPrintable(name));
}

void TestTorrentFolderScan::ignoresOtherFilesAndSubfolders()
{
    QTemporaryDir dir;
    const QByteArray bytes = fixture(QStringLiteral("single_file_v1.torrent"));
    QVERIFY(put(dir, QStringLiteral("the-one.torrent"), bytes, 30));
    QVERIFY(put(dir, QStringLiteral("notes.txt"), bytes, 1));
    QVERIFY(put(dir, QStringLiteral("film.torrent.part"), bytes, 1));
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("nested.torrent")));
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("old")));
    QFile nested(dir.filePath(QStringLiteral("old/deep.torrent")));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write(bytes);
    nested.close();

    QCOMPARE(fileNames(TorrentFolderScan::scan(dir.path())),
             QStringList({QStringLiteral("the-one.torrent")}));
}

void TestTorrentFolderScan::matchesTheSuffixInAnyCase()
{
    QTemporaryDir dir;
    QVERIFY(put(dir,
                QStringLiteral("SHOUTING.TORRENT"),
                fixture(QStringLiteral("single_file_v1.torrent")),
                1));
    QCOMPARE(TorrentFolderScan::scan(dir.path()).size(), 1);
}

void TestTorrentFolderScan::aMissingFolderIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(TorrentFolderScan::scan(dir.filePath(QStringLiteral("no-such-folder"))).isEmpty());
    QVERIFY(TorrentFolderScan::scan(QString()).isEmpty());
}

QTEST_GUILESS_MAIN(TestTorrentFolderScan)

#include "tst_torrentfolderscan.moc"
