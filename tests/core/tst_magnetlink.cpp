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
    void numberedTopicsAreRead();
    void nonAsciiHashAndNegativeLengthAreRejected();
    void bareInfoHashIsAMagnet_data();
    void bareInfoHashIsAMagnet(); // #147
    void notABareInfoHash_data();
    void notABareInfoHash(); // #147
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

void TestMagnetLink::numberedTopicsAreRead()
{
    const QString v1 = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
    const QString v2 =
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    const MagnetLink link = MagnetLink::parse(
        QStringLiteral("magnet:?xt.1=urn:btih:%1&xt.2=urn:btmh:1220%2").arg(v1, v2));
    QVERIFY(link.isValid());
    QCOMPARE(link.infoHashV1, v1);
    QCOMPARE(link.infoHashV2, v2);
    // Not a number after the dot: not a topic.
    QVERIFY(!MagnetLink::parse(QStringLiteral("magnet:?xt.a=urn:btih:%1").arg(v1)).isValid());
}

void TestMagnetLink::nonAsciiHashAndNegativeLengthAreRejected()
{
    // 40 characters, one of them U+00E9: must not reach std::isxdigit as a negative char.
    const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef0123456\u00e9");
    QVERIFY(!MagnetLink::parse(QStringLiteral("magnet:?xt=urn:btih:") + hash).isValid());

    const MagnetLink link = MagnetLink::parse(
        QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&xl=-5"));
    QVERIFY(link.isValid());
    QCOMPARE(link.exactLength, qint64(-1));
}

void TestMagnetLink::bareInfoHashIsAMagnet_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("v1");
    QTest::addColumn<QString>("v2");
    QTest::addColumn<QString>("magnet");

    const QString v1 = QStringLiteral("aabbccddeeff00112233445566778899aabbccdd");
    const QString v2 =
        QStringLiteral("0703e91a4e92560200a851c3f8e8283077f2333510bd3723bbaa367577fff0af");
    QTest::newRow("40 hex") << v1 << v1 << QString() << QStringLiteral("magnet:?xt=urn:btih:") + v1;
    QTest::newRow("40 hex, upper case")
        << v1.toUpper() << v1 << QString() << QStringLiteral("magnet:?xt=urn:btih:") + v1;
    // The same hash in base32 (RFC 4648), as parsesBase32InfoHash reads it inside a magnet.
    QTest::newRow("32 base32") << QStringLiteral("VK54ZXPO74ABCIRTIRKWM54ITGVLXTG5") << v1
                               << QString() << QStringLiteral("magnet:?xt=urn:btih:") + v1;
    QTest::newRow("64 hex, v2") << v2 << QString() << v2
                                << QStringLiteral("magnet:?xt=urn:btmh:1220") + v2;
    QTest::newRow("64 hex, upper case")
        << v2.toUpper() << QString() << v2 << QStringLiteral("magnet:?xt=urn:btmh:1220") + v2;
    QTest::newRow("a whole line, padded") << QStringLiteral("  %1\n").arg(v1) << v1 << QString()
                                          << QStringLiteral("magnet:?xt=urn:btih:") + v1;
}

void TestMagnetLink::bareInfoHashIsAMagnet()
{
    QFETCH(QString, text);
    QFETCH(QString, v1);
    QFETCH(QString, v2);
    QFETCH(QString, magnet);

    const MagnetLink link = MagnetLink::fromInfoHash(text);
    QVERIFY(link.isValid());
    QCOMPARE(link.infoHashV1, v1);
    QCOMPARE(link.infoHashV2, v2);
    QVERIFY(link.trackers.isEmpty());
    QCOMPARE(link.toString(), magnet);
    // What it renders is a magnet like any other, with the same hash.
    const MagnetLink reparsed = MagnetLink::parse(link.toString());
    QCOMPARE(reparsed.infoHashV1, v1);
    QCOMPARE(reparsed.infoHashV2, v2);
}

void TestMagnetLink::notABareInfoHash_data()
{
    QTest::addColumn<QString>("text");

    const QString v1 = QStringLiteral("aabbccddeeff00112233445566778899aabbccdd");
    QTest::newRow("empty") << QString();
    QTest::newRow("39 hex") << v1.left(39);
    QTest::newRow("41 hex") << v1 + QLatin1Char('a');
    QTest::newRow("63 hex") << v1 + v1.left(23);
    QTest::newRow("65 hex") << v1 + v1.left(25);
    QTest::newRow("40, not hex") << v1.left(39) + QLatin1Char('g');
    QTest::newRow("hex inside a word") << QStringLiteral("hash") + v1;
    QTest::newRow("hex in a sentence") << QStringLiteral("the hash is %1").arg(v1);
    QTest::newRow("two hashes") << v1 + QLatin1Char(' ') + v1;
    QTest::newRow("a URL") << QStringLiteral("https://example.org/t/%1").arg(v1);
    QTest::newRow("a magnet") << QStringLiteral("magnet:?xt=urn:btih:") + v1;
    QTest::newRow("multihash prefix, 68 hex") << QStringLiteral("1220") + v1 + v1.left(24);
    // Base32 is read in upper case only: these would decode, but so would many a word.
    QTest::newRow("base32, lower case") << QStringLiteral("vk54zxpo74abcirtirkwm54itgvlxtg5");
    QTest::newRow("base32, mixed case") << QStringLiteral("Vk54ZXPO74ABCIRTIRKWM54ITGVLXTG5");
    QTest::newRow("31 base32") << QStringLiteral("VK54ZXPO74ABCIRTIRKWM54ITGVLXTG");
    QTest::newRow("33 base32") << QStringLiteral("VK54ZXPO74ABCIRTIRKWM54ITGVLXTG5A");
    QTest::newRow("32, not base32") << QStringLiteral("VK54ZXPO74ABCIRTIRKWM54ITGVLXTG1");
    QTest::newRow("non-ASCII") << QStringLiteral("0123456789abcdef0123456789abcdef0123456\u00e9");
}

void TestMagnetLink::notABareInfoHash()
{
    QFETCH(QString, text);
    const MagnetLink link = MagnetLink::fromInfoHash(text);
    QVERIFY(!link.isValid());
    QVERIFY(link.toString().isEmpty());
}

QTEST_APPLESS_MAIN(TestMagnetLink)

#include "tst_magnetlink.moc"
