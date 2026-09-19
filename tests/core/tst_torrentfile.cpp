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
