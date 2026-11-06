#include "core/TorrentFile.hpp"

#include "core/Bencode.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using omaseed::core::TorrentFile;
namespace bencode = omaseed::core::bencode;

namespace {

/// A v2 .torrent whose two 48 KiB files have the same content, so one "pieces root" and one
/// layer of three hashes (piece length 16 KiB), and a 100-byte file too small for a layer.
/// `layer` is what its "piece layers" holds under that root: qBittorrent's torrents/export
/// writes the layer once per file that shares it (#71).
QByteArray duplicateFilesTorrent(const QByteArray &layer)
{
    const QByteArray root(32, 'R');
    const auto leaf = [](qint64 length, const QByteArray &pieces) {
        return bencode::Value(bencode::Dict{
            {QByteArray(),
             bencode::Value(bencode::Dict{{"length", bencode::Value(length)},
                                          {"pieces root", bencode::Value(pieces)}})}});
    };
    const bencode::Value info(
        bencode::Dict{{"file tree",
                       bencode::Value(bencode::Dict{{"a.bin", leaf(3 * 16384, root)},
                                                    {"b.bin", leaf(3 * 16384, root)},
                                                    {"c.txt", leaf(100, QByteArray(32, 'c'))}})},
                      {"meta version", bencode::Value(qint64(2))},
                      {"name", bencode::Value(QByteArray("dups"))},
                      {"piece length", bencode::Value(qint64(16384))}});
    return bencode::encode(bencode::Value(bencode::Dict{
        {"announce", bencode::Value(QByteArray("http://127.0.0.1:26996/announce"))},
        {"info", info},
        {"piece layers", bencode::Value(bencode::Dict{{root, bencode::Value(layer)}})}}));
}

/// The layer of three piece hashes the two files share.
QByteArray threeHashes()
{
    return QByteArray(32, '1') + QByteArray(32, '2') + QByteArray(32, '3');
}

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
    void torrentIdOfAHybridIsItsTruncatedV2Hash();
    void repairsALayerRepeatedPerDuplicateFile();
    void leavesSoundLayersAndOtherTorrentsAlone();
    void rejectsGarbage();
    void readsThePrivateSourceTag(); // #146
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
    QVERIFY(!torrent.multiFile);
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
    QVERIFY(torrent.multiFile);
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

void TestTorrentFile::torrentIdOfAHybridIsItsTruncatedV2Hash()
{
    // A hybrid (v1 "pieces" and v2 "file tree" in one info dict). qBittorrent keys it by
    // libtorrent's info_hash_t::get_best(): the v2 hash cut to 40 hex digits, not the v1 hash.
    // The parser does not check that the two piece layouts agree, so a stub is enough.
    const QByteArray info = QByteArrayLiteral("d"
                                              "9:file treed6:Hy.bind0:d6:lengthi16384e"
                                              "11:pieces root32:") +
                            QByteArray(32, 'r') +
                            QByteArrayLiteral("eee"
                                              "6:lengthi16384e"
                                              "12:meta versioni2e"
                                              "4:name6:Hy.bin"
                                              "12:piece lengthi16384e"
                                              "6:pieces20:") +
                            QByteArray(20, 'p') + QByteArrayLiteral("e");
    const TorrentFile hybrid =
        TorrentFile::parse(QByteArrayLiteral("d4:info") + info + QByteArrayLiteral("e"));
    QVERIFY(hybrid.isValid());
    QCOMPARE(hybrid.infoHashV1.size(), 40);
    QCOMPARE(hybrid.infoHashV2.size(), 64);
    QVERIFY(hybrid.infoHashV1 != hybrid.infoHashV2.left(40));
    QCOMPARE(hybrid.torrentId(), hybrid.infoHashV2.left(40));
    QVERIFY(!hybrid.multiFile);
    QVERIFY(hybrid.magnetLink().contains(QStringLiteral("urn:btih:") + hybrid.infoHashV1));

    // The rule itself, as App uses it for magnets: trimmed, lower-cased, v2 wins.
    const QString v1 = QStringLiteral("fdc2dabac7590000000000000000000000000001");
    const QString v2 =
        QStringLiteral("81f2da56846c00000000000000000000000000000000000000000000000000ff");
    QCOMPARE(TorrentFile::torrentIdFor(v1, v2.toUpper()), v2.left(40));
    QCOMPARE(TorrentFile::torrentIdFor(QStringLiteral(" ") + v1.toUpper(), QString()), v1);
    QCOMPARE(TorrentFile::torrentIdFor(QString(), QString()), QString());
}

// #71: the export's layer, repeated once per file sharing its root, cut back to one; the info
// bytes, so the hashes, as they were.
void TestTorrentFile::repairsALayerRepeatedPerDuplicateFile()
{
    const QByteArray sound = duplicateFilesTorrent(threeHashes());
    const QByteArray exported = duplicateFilesTorrent(threeHashes().repeated(2));
    int repaired = -1;
    const QByteArray fixed = TorrentFile::repairPieceLayers(exported, &repaired);
    QCOMPARE(repaired, 1);
    QCOMPARE(fixed, sound);
    const TorrentFile before = TorrentFile::parse(exported);
    const TorrentFile after = TorrentFile::parse(fixed);
    QVERIFY(after.isValid());
    QCOMPARE(after.infoHashV2, before.infoHashV2);
    QCOMPARE(after.infoHashV1, before.infoHashV1);
    QCOMPARE(bencode::decode(fixed).value("piece layers").value(QByteArray(32, 'R')).toString(),
             threeHashes());

    // Three files sharing it: three copies, the same cut.
    QCOMPARE(TorrentFile::repairPieceLayers(duplicateFilesTorrent(threeHashes().repeated(3))),
             sound);
}

void TestTorrentFile::leavesSoundLayersAndOtherTorrentsAlone()
{
    int repaired = -1;
    const QByteArray sound = duplicateFilesTorrent(threeHashes());
    QCOMPARE(TorrentFile::repairPieceLayers(sound, &repaired), sound);
    QCOMPARE(repaired, 0);
    // Too long, but not copies of itself: not ours to guess, left for qBittorrent to refuse.
    const QByteArray odd = duplicateFilesTorrent(threeHashes() + QByteArray(96, 'x'));
    QCOMPARE(TorrentFile::repairPieceLayers(odd, &repaired), odd);
    QCOMPARE(repaired, 0);
    // Too short, too.
    const QByteArray cut = duplicateFilesTorrent(threeHashes().left(64));
    QCOMPARE(TorrentFile::repairPieceLayers(cut), cut);
    // v1, and not a torrent at all.
    const QByteArray v1 = readFile(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    QVERIFY(!v1.isEmpty());
    QCOMPARE(TorrentFile::repairPieceLayers(v1), v1);
    const QByteArray garbage("12:piece layers, and nothing a torrent has");
    QCOMPARE(TorrentFile::repairPieceLayers(garbage), garbage);
}

void TestTorrentFile::rejectsGarbage()
{
    QString error;
    const TorrentFile torrent = TorrentFile::parse(QByteArrayLiteral("not a torrent"), &error);
    QVERIFY(!torrent.isValid());
    QVERIFY(!error.isEmpty());
}

// #146 (C27 CR6): the info's `source`, as qBittorrent's creator writes it for a private one.
void TestTorrentFile::readsThePrivateSourceTag()
{
    const QByteArray bytes =
        "d8:announce38:https://tracker-a.example/announce/k3y4:infod6:lengthi3e"
        "4:name5:a.txt12:piece lengthi16384e6:pieces20:aaaaaaaaaaaaaaaaaaaa"
        "7:privatei1e6:source2:TAee";
    const TorrentFile file = TorrentFile::parse(bytes);
    QVERIFY(file.isValid());
    QVERIFY(file.isPrivate);
    QCOMPARE(file.source, QStringLiteral("TA"));
    const TorrentFile none = TorrentFile::parse(
        "d4:infod6:lengthi3e4:name5:a.txt12:piece lengthi16384e6:pieces20:aaaaaaaaaaaaaaaaaaaaee");
    QVERIFY(none.isValid());
    QVERIFY(none.source.isEmpty());
}

QTEST_APPLESS_MAIN(TestTorrentFile)

#include "tst_torrentfile.moc"
