// core::CreateTrackers (#146, DECISIONS C27 CR4, CR5): the public announce URLs the library
// uses most, and the private hosts with their newest torrent's announce URL; two passkeys for
// one host. Every host, key and name is invented.

#include "core/CreateTrackers.hpp"

#include <QTest>

using omaseed::core::CreateTrackers;

namespace {

CreateTrackers::Torrent torrent(const QString &hash, bool isPrivate, int day)
{
    return {hash,
            QStringLiteral("t-") + hash,
            isPrivate,
            QDateTime(QDate(2026, 10, day), QTime(12, 0), QTimeZone::UTC)};
}

} // namespace

class TestCreateTrackers : public QObject
{
    Q_OBJECT

private slots:
    void publicTrackersByUse();
    void privateHostsAndTheirNewestKey();
    void onlyAnnounceUrls();
};

void TestCreateTrackers::publicTrackersByUse()
{
    const QList<CreateTrackers::Torrent> torrents = {torrent(QStringLiteral("a"), false, 1),
                                                     torrent(QStringLiteral("b"), false, 2),
                                                     torrent(QStringLiteral("c"), false, 3),
                                                     torrent(QStringLiteral("p"), true, 4)};
    QHash<QString, QStringList> byUrl;
    byUrl.insert(QStringLiteral("udp://open.tracker-c.example:6969/announce"),
                 {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    byUrl.insert(QStringLiteral("https://tracker-d.example/announce"),
                 {QStringLiteral("a"), QStringLiteral("p")});
    byUrl.insert(QStringLiteral("udp://tracker-e.example:1337/announce"), {QStringLiteral("b")});
    byUrl.insert(QStringLiteral("udp://tracker-f.example:1337/announce"), {QStringLiteral("c")});
    byUrl.insert(QStringLiteral("https://tracker-a.example/announce/k3y"), {QStringLiteral("p")});
    const CreateTrackers trackers(torrents, byUrl);
    const auto top = trackers.publicTop();
    QCOMPARE(top.size(), 3);
    QCOMPARE(top.at(0).url, QStringLiteral("udp://open.tracker-c.example:6969/announce"));
    QCOMPARE(top.at(0).count, 3);
    // d counts only its public torrent; e and f tie at 1, by URL.
    QCOMPARE(top.at(1).url, QStringLiteral("https://tracker-d.example/announce"));
    QCOMPARE(top.at(1).count, 1);
    QCOMPARE(top.at(2).url, QStringLiteral("udp://tracker-e.example:1337/announce"));
    QCOMPARE(trackers.publicTop(10).size(), 4); // a private-only URL is never public
}

void TestCreateTrackers::privateHostsAndTheirNewestKey()
{
    const QList<CreateTrackers::Torrent> torrents = {torrent(QStringLiteral("p1"), true, 2),
                                                     torrent(QStringLiteral("p2"), true, 20),
                                                     torrent(QStringLiteral("p3"), true, 5),
                                                     torrent(QStringLiteral("q1"), true, 2)};
    QHash<QString, QStringList> byUrl;
    // An old passkey on p1 and p3, a new one on p2 (the newest); the same site over udp too.
    byUrl.insert(QStringLiteral("https://tracker-a.example/announce/old0"),
                 {QStringLiteral("p1"), QStringLiteral("p3")});
    byUrl.insert(QStringLiteral("https://tracker-a.example/announce/new1"), {QStringLiteral("p2")});
    byUrl.insert(QStringLiteral("https://tracker-b.example/k3y/announce"), {QStringLiteral("q1")});
    const CreateTrackers trackers(torrents, byUrl);
    const auto hosts = trackers.privateHosts();
    QCOMPARE(hosts.size(), 2);
    QCOMPARE(hosts.at(0).host, QStringLiteral("tracker-a.example"));
    QCOMPARE(hosts.at(0).count, 3);
    QCOMPARE(hosts.at(0).newestHash, QStringLiteral("p2"));
    QCOMPARE(hosts.at(0).newestName, QStringLiteral("t-p2"));
    QCOMPARE(hosts.at(0).announce, QStringLiteral("https://tracker-a.example/announce/new1"));
    QVERIFY(hosts.at(0).otherKeys);
    QCOMPARE(hosts.at(1).host, QStringLiteral("tracker-b.example"));
    QVERIFY(!hosts.at(1).otherKeys);
    QVERIFY(trackers.privateHost(QStringLiteral("tracker-b.example")).has_value());
    QVERIFY(!trackers.privateHost(QStringLiteral("nowhere.example")).has_value());
    QVERIFY(trackers.publicTop().isEmpty());
}

void TestCreateTrackers::onlyAnnounceUrls()
{
    QVERIFY(CreateTrackers::isAnnounceUrl(QStringLiteral("udp://a.example:1/announce")));
    QVERIFY(CreateTrackers::isAnnounceUrl(QStringLiteral("wss://a.example/announce")));
    QVERIFY(!CreateTrackers::isAnnounceUrl(QStringLiteral("** [DHT] **")));
    QVERIFY(!CreateTrackers::isAnnounceUrl(QString()));
    QVERIFY(!CreateTrackers::isAnnounceUrl(QStringLiteral("ftp://a.example/x")));
}

QTEST_APPLESS_MAIN(TestCreateTrackers)
#include "tst_createtrackers.moc"
