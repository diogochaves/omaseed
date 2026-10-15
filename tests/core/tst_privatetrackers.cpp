// #44 (docs/29 D7, F5): which magnets get no lookup because their tracker is one of the
// library's private ones. A table: the library's torrents in, a magnet's `tr=` in, the
// matching host out.

#include "core/PrivateTrackers.hpp"

#include <QTest>

using omaseed::core::PrivateTrackers;

namespace {

/// The library every row is checked against.
PrivateTrackers library()
{
    PrivateTrackers trackers;
    trackers.addTorrent(true,
                        {QStringLiteral("https://tracker.privatesite.example/a1b2c3/announce"),
                         QStringLiteral("udp://backup.privatesite.example:6969/announce"),
                         QStringLiteral("** [DHT] **")});
    trackers.addTorrent(true, {QStringLiteral("http://Flacs.Example:2710/xyz/announce")});
    // A public torrent: its trackers are not private, even when they are many.
    trackers.addTorrent(false,
                        {QStringLiteral("udp://tracker.opentrackr.example:1337/announce"),
                         QStringLiteral("http://open.demonii.example/announce")});
    // A private torrent with no tracker at all (DHT is off for it anyway).
    trackers.addTorrent(true, {});
    return trackers;
}

} // namespace

class TestPrivateTrackers : public QObject
{
    Q_OBJECT

private slots:
    void hostsAreThePrivateTorrentsOnly();
    void match_data();
    void match();
};

void TestPrivateTrackers::hostsAreThePrivateTorrentsOnly()
{
    QCOMPARE(library().hosts(),
             (QStringList{QStringLiteral("backup.privatesite.example"),
                          QStringLiteral("flacs.example"),
                          QStringLiteral("tracker.privatesite.example")}));
    QVERIFY(PrivateTrackers().hosts().isEmpty());
}

void TestPrivateTrackers::match_data()
{
    QTest::addColumn<QStringList>("magnetTrackers");
    QTest::addColumn<QString>("host");

    QTest::newRow("no trackers") << QStringList() << QString();
    QTest::newRow("the same announce URL")
        << QStringList{QStringLiteral("https://tracker.privatesite.example/a1b2c3/announce")}
        << QStringLiteral("tracker.privatesite.example");
    QTest::newRow("another passkey, another scheme")
        << QStringList{QStringLiteral("udp://tracker.privatesite.example:80/zzz/announce")}
        << QStringLiteral("tracker.privatesite.example");
    QTest::newRow("the host in another case")
        << QStringList{QStringLiteral("http://FLACS.example:2710/other/announce")}
        << QStringLiteral("flacs.example");
    QTest::newRow("the second tracker matches")
        << QStringList{QStringLiteral("udp://tracker.opentrackr.example:1337/announce"),
                       QStringLiteral("udp://backup.privatesite.example:6969/announce")}
        << QStringLiteral("backup.privatesite.example");
    QTest::newRow("a public tracker")
        << QStringList{QStringLiteral("udp://tracker.opentrackr.example:1337/announce")}
        << QString();
    QTest::newRow("another site's subdomain")
        << QStringList{QStringLiteral("https://privatesite.example/announce")} << QString();
    QTest::newRow("a pseudo-tracker") << QStringList{QStringLiteral("** [DHT] **")} << QString();
    QTest::newRow("nonsense") << QStringList{QStringLiteral("not a url"), QString()} << QString();
}

void TestPrivateTrackers::match()
{
    QFETCH(QStringList, magnetTrackers);
    QFETCH(QString, host);
    QCOMPARE(library().match(magnetTrackers), host);
}

QTEST_GUILESS_MAIN(TestPrivateTrackers)
#include "tst_privatetrackers.moc"
