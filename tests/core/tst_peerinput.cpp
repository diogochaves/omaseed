// PeerInput: what the details pane's add-tracker and add-peer sheets accept (review VIEW-6).

#include "core/PeerInput.hpp"

#include <QTest>

using omaseed::core::PeerInputResult;
namespace PeerInput = omaseed::core::PeerInput;

class TestPeerInput : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void peerAddresses_data();
    void peerAddresses();
    void peersSplitOnLinesSpacesAndCommas();
    void peersStopAtTheFirstBadEntry();
    void emptyInputIsNotValid();
    void trackerUrlsSplitOnWhiteSpaceOnly();
    void trackerUrlsNeedATrackerSchemeAndAHost_data();
    void trackerUrlsNeedATrackerSchemeAndAHost();
    void webSeedUrlsNeedHttpAndAHost_data();
    void webSeedUrlsNeedHttpAndAHost();
    void webSeedUrlsSplitOnWhiteSpace();
};

void TestPeerInput::peerAddresses_data()
{
    QTest::addColumn<QString>("entry");
    QTest::addColumn<bool>("valid");

    QTest::newRow("ipv4") << QStringLiteral("10.0.0.2:6881") << true;
    QTest::newRow("ipv6 in brackets") << QStringLiteral("[2001:db8::1]:51413") << true;
    QTest::newRow("ipv6 loopback") << QStringLiteral("[::1]:6881") << true;
    QTest::newRow("host name") << QStringLiteral("seed.example.org:6881") << true;
    QTest::newRow("single label") << QStringLiteral("localhost:6881") << true;
    QTest::newRow("highest port") << QStringLiteral("10.0.0.2:65535") << true;

    QTest::newRow("no port") << QStringLiteral("10.0.0.2") << false;
    QTest::newRow("empty port") << QStringLiteral("10.0.0.2:") << false;
    QTest::newRow("port 0") << QStringLiteral("10.0.0.2:0") << false;
    QTest::newRow("port too big") << QStringLiteral("10.0.0.2:65536") << false;
    QTest::newRow("port not a number") << QStringLiteral("10.0.0.2:http") << false;
    QTest::newRow("no host") << QStringLiteral(":6881") << false;
    QTest::newRow("bare ipv6") << QStringLiteral("2001:db8::1:6881") << false;
    QTest::newRow("bracket without port") << QStringLiteral("[2001:db8::1]") << false;
    QTest::newRow("ipv4 in brackets") << QStringLiteral("[10.0.0.2]:6881") << false;
    QTest::newRow("short ipv4") << QStringLiteral("1.2.3:6881") << false;
    QTest::newRow("octet too big") << QStringLiteral("10.0.0.300:6881") << false;
    QTest::newRow("underscore host") << QStringLiteral("bad_host:6881") << false;
    QTest::newRow("url") << QStringLiteral("http://10.0.0.2:6881") << false;
}

void TestPeerInput::peerAddresses()
{
    QFETCH(QString, entry);
    QFETCH(bool, valid);
    QCOMPARE(PeerInput::isPeerAddress(entry), valid);
}

void TestPeerInput::peersSplitOnLinesSpacesAndCommas()
{
    const PeerInputResult result = PeerInput::peers(
        QStringLiteral("  10.0.0.2:6881\n[::1]:6881, host.example:1  \n\n10.0.0.2:6881 "));
    QVERIFY(result.isValid());
    QCOMPARE(result.entries,
             (QStringList{QStringLiteral("10.0.0.2:6881"),
                          QStringLiteral("[::1]:6881"),
                          QStringLiteral("host.example:1")}));
}

void TestPeerInput::peersStopAtTheFirstBadEntry()
{
    const PeerInputResult result =
        PeerInput::peers(QStringLiteral("10.0.0.2:6881\n10.0.0.3\n10.0.0.4"));
    QVERIFY(!result.isValid());
    QCOMPARE(result.invalid, QStringLiteral("10.0.0.3"));
}

void TestPeerInput::emptyInputIsNotValid()
{
    QVERIFY(!PeerInput::peers(QString()).isValid());
    QVERIFY(!PeerInput::peers(QStringLiteral(" \n , ")).isValid());
    QVERIFY(PeerInput::peers(QStringLiteral(" \n , ")).invalid.isEmpty());
    QVERIFY(!PeerInput::trackerUrls(QStringLiteral("\n\n")).isValid());
}

void TestPeerInput::trackerUrlsSplitOnWhiteSpaceOnly()
{
    const PeerInputResult result = PeerInput::trackerUrls(
        QStringLiteral("udp://a.example:1337/announce\n  https://b.example/a?k=1,2 "
                       "udp://a.example:1337/announce"));
    QVERIFY(result.isValid());
    QCOMPARE(result.entries,
             (QStringList{QStringLiteral("udp://a.example:1337/announce"),
                          QStringLiteral("https://b.example/a?k=1,2")}));
}

void TestPeerInput::trackerUrlsNeedATrackerSchemeAndAHost_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("valid");

    QTest::newRow("udp") << QStringLiteral("udp://tracker.example:6969/announce") << true;
    QTest::newRow("http") << QStringLiteral("http://tracker.example/announce") << true;
    QTest::newRow("https") << QStringLiteral("https://tracker.example/announce") << true;
    QTest::newRow("wss") << QStringLiteral("wss://tracker.example") << true;
    QTest::newRow("upper-case scheme") << QStringLiteral("UDP://tracker.example:6969") << true;
    QTest::newRow("no scheme") << QStringLiteral("tracker.example/announce") << false;
    QTest::newRow("magnet") << QStringLiteral("magnet:?xt=urn:btih:abc") << false;
    QTest::newRow("ftp") << QStringLiteral("ftp://tracker.example") << false;
    QTest::newRow("no host") << QStringLiteral("udp:///announce") << false;
}

void TestPeerInput::trackerUrlsNeedATrackerSchemeAndAHost()
{
    QFETCH(QString, url);
    QFETCH(bool, valid);
    const PeerInputResult result = PeerInput::trackerUrls(url);
    QCOMPARE(result.isValid(), valid);
    if (!valid)
        QCOMPARE(result.invalid, url);
}

void TestPeerInput::webSeedUrlsNeedHttpAndAHost_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("valid");

    QTest::newRow("http") << QStringLiteral("http://mirror.example/pub/") << true;
    QTest::newRow("https") << QStringLiteral("https://archive.example/download/item/") << true;
    QTest::newRow("a file") << QStringLiteral("https://mirror.example/a.iso") << true;
    QTest::newRow("upper-case scheme") << QStringLiteral("HTTPS://mirror.example/") << true;
    QTest::newRow("udp") << QStringLiteral("udp://tracker.example:6969/announce") << false;
    QTest::newRow("ftp") << QStringLiteral("ftp://mirror.example/pub/") << false;
    QTest::newRow("no scheme") << QStringLiteral("mirror.example/pub/") << false;
    QTest::newRow("no host") << QStringLiteral("https:///pub/") << false;
}

void TestPeerInput::webSeedUrlsNeedHttpAndAHost()
{
    QFETCH(QString, url);
    QFETCH(bool, valid);
    const PeerInputResult result = PeerInput::webSeedUrls(url);
    QCOMPARE(result.isValid(), valid);
    if (!valid)
        QCOMPARE(result.invalid, url);
}

void TestPeerInput::webSeedUrlsSplitOnWhiteSpace()
{
    const PeerInputResult result = PeerInput::webSeedUrls(
        QStringLiteral("https://a.example/x/\n https://b.example/y?k=1,2  https://a.example/x/"));
    QVERIFY(result.isValid());
    QCOMPARE(result.entries,
             (QStringList{QStringLiteral("https://a.example/x/"),
                          QStringLiteral("https://b.example/y?k=1,2")}));
    QVERIFY(!PeerInput::webSeedUrls(QStringLiteral("  ")).isValid());
}

QTEST_GUILESS_MAIN(TestPeerInput)

#include "tst_peerinput.moc"
