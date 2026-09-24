#include "core/TorrentFile.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using omaseed::core::TorrentFile;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TestTorrentFile : public QObject
{
    Q_OBJECT

private slots:
    void matchesFixtures_data();
    void matchesFixtures();
    void singleFileNameIsUsedAsItsPath();
    void multiFileUnicodePathsJoinWithSlash();
    void magnetLinkIncludesHashNameAndTrackers();
    void padFilesAreLeftOut();
    void filesKeepTheirMetainfoOrder();
    void torrentIdIsV1WhenPresentAndTruncatedV2Otherwise();
    void rejectsGarbage();
};

void TestTorrentFile::matchesFixtures_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("name");
    QTest::addColumn<qint64>("totalSize");
    QTest::addColumn<QString>("infoHashV1");
    QTest::addColumn<QString>("infoHashV2");
    QTest::addColumn<bool>("isPrivate");
    QTest::addColumn<qint64>("pieceLength");

    const QString manifestPath = QStringLiteral(OMASEED_FIXTURES_DIR "/fixtures.json");
    const QByteArray manifestBytes = readFile(manifestPath);
    QVERIFY2(!manifestBytes.isEmpty(), qPrintable(manifestPath));

    const QJsonArray manifest = QJsonDocument::fromJson(manifestBytes).array();
    QVERIFY(!manifest.isEmpty());

    for (const QJsonValue &v : manifest) {
        const QJsonObject obj = v.toObject();
        const QString file = obj.value(QStringLiteral("file")).toString();
        QTest::newRow(qPrintable(file))
            << file << obj.value(QStringLiteral("name")).toString()
            << qint64(obj.value(QStringLiteral("totalSize")).toDouble())
            << obj.value(QStringLiteral("infoHashV1")).toString()
            << obj.value(QStringLiteral("infoHashV2")).toString()
            << obj.value(QStringLiteral("isPrivate")).toBool()
            << qint64(obj.value(QStringLiteral("pieceLength")).toDouble());
    }
}

void TestTorrentFile::matchesFixtures()
{
    QFETCH(QString, file);
    QFETCH(QString, name);
    QFETCH(qint64, totalSize);
    QFETCH(QString, infoHashV1);
    QFETCH(QString, infoHashV2);
    QFETCH(bool, isPrivate);
    QFETCH(qint64, pieceLength);

    const QByteArray data = readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/") + file);
    QVERIFY(!data.isEmpty());

    QString error;
    const TorrentFile torrent = TorrentFile::parse(data, &error);
    QVERIFY2(torrent.isValid(), qPrintable(error));

    QCOMPARE(torrent.name, name);
    QCOMPARE(torrent.totalSize, totalSize);
    QCOMPARE(torrent.infoHashV1, infoHashV1);
    QCOMPARE(torrent.infoHashV2, infoHashV2);
    QCOMPARE(torrent.isPrivate, isPrivate);
    QCOMPARE(torrent.pieceLength, pieceLength);
    QCOMPARE(torrent.infoHashV1.size(), 40);
}

void TestTorrentFile::singleFileNameIsUsedAsItsPath()
{
    const QByteArray data =
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    const TorrentFile torrent = TorrentFile::parse(data);
    QVERIFY(torrent.isValid());
    QCOMPARE(torrent.files.size(), 1);
    QCOMPARE(torrent.files.first().path, QStringLiteral("Single.bin"));
    QCOMPARE(torrent.files.first().size, torrent.totalSize);
}

void TestTorrentFile::multiFileUnicodePathsJoinWithSlash()
{
    const QByteArray data =
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/multi_file_v1_unicode.torrent"));
    const TorrentFile torrent = TorrentFile::parse(data);
    QVERIFY(torrent.isValid());
    QCOMPARE(torrent.files.size(), 3);

    QStringList paths;
    for (const auto &f : torrent.files)
        paths << f.path;
    QVERIFY(paths.contains(QStringLiteral("café/notes.txt")));
    QVERIFY(paths.contains(QStringLiteral("日本語/sub フォルダ/ファイル.bin")));
    QVERIFY(paths.contains(QStringLiteral("root.txt")));
}

void TestTorrentFile::magnetLinkIncludesHashNameAndTrackers()
{
    const QByteArray data =
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/private_announce_list.torrent"));
    const TorrentFile torrent = TorrentFile::parse(data);
    QVERIFY(torrent.isValid());

    const QString magnet = torrent.magnetLink();
    QVERIFY(magnet.startsWith(QStringLiteral("magnet:?xt=urn:btih:") + torrent.infoHashV1));
    QVERIFY(magnet.contains(QStringLiteral("dn=Private.iso")));
    QVERIFY(magnet.contains(QStringLiteral("tr=http%3A%2F%2Ftracker1.example.com%2Fannounce")));
    QVERIFY(magnet.contains(QStringLiteral("tr=http%3A%2F%2Ftracker2.example.com%2Fannounce")));
    QVERIFY(magnet.contains(QStringLiteral("tr=http%3A%2F%2Ftracker3.example.com%2Fannounce")));
    QVERIFY(!magnet.contains(QStringLiteral("btmh")));

    // A v2-only torrent: its SHA-1 identifies nothing, so only the v2 multihash topic.
    const TorrentFile v2 =
        TorrentFile::parse(readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/v2_stub.torrent")));
    QVERIFY(v2.isValid());
    const QString v2Magnet = v2.magnetLink();
    QVERIFY2(v2Magnet.startsWith(QStringLiteral("magnet:?xt=urn:btmh:1220") + v2.infoHashV2),
             qPrintable(v2Magnet));
    QVERIFY(!v2Magnet.contains(QStringLiteral("btih")));
}

void TestTorrentFile::padFilesAreLeftOut()
{
    // qBittorrent numbers files without BEP-47 pad files, so `torrents/filePrio` id 1 is
    // notes.txt here, not the pad entry between the two.
    const QByteArray data =
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/multi_file_padded_v1.torrent"));
    const TorrentFile torrent = TorrentFile::parse(data);
    QVERIFY(torrent.isValid());
    QCOMPARE(torrent.files.size(), 2);
    QCOMPARE(torrent.files.at(0).path, QStringLiteral("disc/one.bin"));
    QCOMPARE(torrent.files.at(1).path, QStringLiteral("notes.txt"));
    QCOMPARE(torrent.totalSize, qint64(25000));
}

void TestTorrentFile::filesKeepTheirMetainfoOrder()
{
    // The add sheet's file ids are positions in this list, so it must be the metainfo's own
    // order, not a sorted one.
    const QByteArray data =
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/multi_file_v1_unicode.torrent"));
    const TorrentFile torrent = TorrentFile::parse(data);
    QVERIFY(torrent.isValid());
    QCOMPARE(torrent.files.size(), 3);
    QCOMPARE(torrent.files.at(0).path, QStringLiteral("café/notes.txt"));
    QCOMPARE(torrent.files.at(0).size, qint64(120));
    QCOMPARE(torrent.files.at(1).path, QStringLiteral("日本語/sub フォルダ/ファイル.bin"));
    QCOMPARE(torrent.files.at(2).path, QStringLiteral("root.txt"));
}

void TestTorrentFile::torrentIdIsV1WhenPresentAndTruncatedV2Otherwise()
{
    const TorrentFile v1 = TorrentFile::parse(
        readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent")));
    QCOMPARE(v1.torrentId(), v1.infoHashV1);

    // v2-only: no "pieces", so there is no v1 infohash for qBittorrent to key it by.
    const TorrentFile v2 =
        TorrentFile::parse(readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/v2_stub.torrent")));
    QVERIFY(v2.isValid());
    QCOMPARE(v2.infoHashV2.size(), 64);
    QCOMPARE(v2.torrentId(), v2.infoHashV2.left(40));
    QCOMPARE(v2.files.size(), 1);
    QCOMPARE(v2.files.first().path, QStringLiteral("V2Stub.bin"));
}

void TestTorrentFile::rejectsGarbage()
{
    QString error;
    const TorrentFile torrent = TorrentFile::parse(QByteArrayLiteral("not a torrent"), &error);
    QVERIFY(!torrent.isValid());
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TestTorrentFile)

#include "tst_torrentfile.moc"
