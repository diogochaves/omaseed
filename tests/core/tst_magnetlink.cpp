#include "core/MagnetLink.hpp"

#include <QTest>

using omaseed::core::MagnetLink;

class TestMagnetLink : public QObject
{
    Q_OBJECT

private slots:
    void parsesHexInfoHash();
    void parsesBase32InfoHash();
    void parsesBtmhV2Hash();
    void decodesDisplayNameWithPlusAsSpace();
    void collectsMultipleTrackersInOrder();
    void collectsWebSeedsPeersAndExactLength();
    void nonMagnetInputIsInvalid();
    void garbageHashIsInvalid();
    void toStringIsCanonicalAndRoundTrips();
};

void TestMagnetLink::parsesHexInfoHash()
{
    const MagnetLink link = MagnetLink::parse(
        QStringLiteral("magnet:?xt=urn:btih:AABBCCDDEEFF00112233445566778899AABBCCDD"));
    QVERIFY(link.isValid());
    QCOMPARE(link.infoHashV1, QStringLiteral("aabbccddeeff00112233445566778899aabbccdd"));
}

void TestMagnetLink::parsesBase32InfoHash()
{
    // Base32 (RFC 4648) encoding of hex aabbccddeeff00112233445566778899aabbccdd.
    const MagnetLink link =
        MagnetLink::parse(QStringLiteral("magnet:?xt=urn:btih:VK54ZXPO74ABCIRTIRKWM54ITGVLXTG5"));
    QVERIFY(link.isValid());
    QCOMPARE(link.infoHashV1, QStringLiteral("aabbccddeeff00112233445566778899aabbccdd"));
}

void TestMagnetLink::parsesBtmhV2Hash()
{
    const QString sha256hex =
        QStringLiteral("0703e91a4e92560200a851c3f8e8283077f2333510bd3723bbaa367577fff0af");
    const MagnetLink link =
        MagnetLink::parse(QStringLiteral("magnet:?xt=urn:btmh:1220") + sha256hex);
    QVERIFY(link.isValid());
    QCOMPARE(link.infoHashV2, sha256hex);
    QVERIFY(link.infoHashV1.isEmpty());
}

void TestMagnetLink::decodesDisplayNameWithPlusAsSpace()
{
    const MagnetLink link = MagnetLink::parse(
        QStringLiteral("magnet:?xt=urn:btih:aabbccddeeff00112233445566778899aabbccdd"
                       "&dn=My+Great+File%20v2"));
    QVERIFY(link.isValid());
    QCOMPARE(link.displayName, QStringLiteral("My Great File v2"));
}

void TestMagnetLink::collectsMultipleTrackersInOrder()
{
    const MagnetLink link = MagnetLink::parse(
        QStringLiteral("magnet:?xt=urn:btih:aabbccddeeff00112233445566778899aabbccdd"
                       "&tr=http%3A%2F%2Fa.example%2Fannounce"
                       "&tr=http%3A%2F%2Fb.example%2Fannounce"));
    QVERIFY(link.isValid());
    QCOMPARE(link.trackers.size(), 2);
    QCOMPARE(link.trackers.at(0), QStringLiteral("http://a.example/announce"));
    QCOMPARE(link.trackers.at(1), QStringLiteral("http://b.example/announce"));
}

void TestMagnetLink::collectsWebSeedsPeersAndExactLength()
{
    const MagnetLink link = MagnetLink::parse(
        QStringLiteral("magnet:?xt=urn:btih:aabbccddeeff00112233445566778899aabbccdd"
                       "&xl=123456"
                       "&ws=http%3A%2F%2Fseed.example%2Ffile"
                       "&x.pe=1.2.3.4%3A6881"));
    QVERIFY(link.isValid());
    QCOMPARE(link.exactLength, qint64(123456));
    QCOMPARE(link.webSeeds, QStringList{QStringLiteral("http://seed.example/file")});
    QCOMPARE(link.peers, QStringList{QStringLiteral("1.2.3.4:6881")});
}

void TestMagnetLink::nonMagnetInputIsInvalid()
{
    QVERIFY(!MagnetLink::parse(QStringLiteral("http://example.com/file.torrent")).isValid());
    QVERIFY(!MagnetLink::parse(QStringLiteral("")).isValid());
    QVERIFY(!MagnetLink::parse(QStringLiteral("not a magnet at all")).isValid());
}

void TestMagnetLink::garbageHashIsInvalid()
{
    QVERIFY(!MagnetLink::parse(QStringLiteral("magnet:?xt=urn:btih:nothex")).isValid());
    QVERIFY(!MagnetLink::parse(QStringLiteral("magnet:?dn=NoHashHere")).isValid());
}

void TestMagnetLink::toStringIsCanonicalAndRoundTrips()
{
    const MagnetLink link =
        MagnetLink::parse(QStringLiteral("magnet:?dn=My+File"
                                         "&tr=http%3A%2F%2Fa.example%2Fannounce"
                                         "&xt=urn:btih:aabbccddeeff00112233445566778899aabbccdd"));
    QVERIFY(link.isValid());

    const QString canonical = link.toString();
    QVERIFY(canonical.startsWith(
        QStringLiteral("magnet:?xt=urn:btih:aabbccddeeff00112233445566778899aabbccdd")));

    const MagnetLink reparsed = MagnetLink::parse(canonical);
    QVERIFY(reparsed.isValid());
    QCOMPARE(reparsed.infoHashV1, link.infoHashV1);
    QCOMPARE(reparsed.displayName, link.displayName);
    QCOMPARE(reparsed.trackers, link.trackers);
    QCOMPARE(reparsed.toString(), canonical);
}

QTEST_APPLESS_MAIN(TestMagnetLink)

#include "tst_magnetlink.moc"
