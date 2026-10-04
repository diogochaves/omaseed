#include "core/TrackerMerge.hpp"

#include <QTest>

namespace TrackerMerge = omaseed::core::TrackerMerge;

class TestTrackerMerge : public QObject
{
    Q_OBJECT

private slots:
    void keyIgnoresCaseOfSchemeAndHost();
    void keySpellsOutTheDefaultPort();
    void keyKeepsThePathAndQuery();
    void keyRefusesWhatIsNoTracker();
    void missingKeepsOrderAndSpelling();
    void missingDropsDuplicatesWithinTheSource();
    void nothingMissingWhenTheSameTrackers();
};

void TestTrackerMerge::keyIgnoresCaseOfSchemeAndHost()
{
    QCOMPARE(TrackerMerge::key(QStringLiteral("UDP://Tracker.Example:1337/announce")),
             TrackerMerge::key(QStringLiteral("udp://tracker.example:1337/announce")));
}

void TestTrackerMerge::keySpellsOutTheDefaultPort()
{
    QCOMPARE(TrackerMerge::key(QStringLiteral("http://t.example/announce")),
             TrackerMerge::key(QStringLiteral("http://t.example:80/announce")));
    QCOMPARE(TrackerMerge::key(QStringLiteral("https://t.example/announce")),
             TrackerMerge::key(QStringLiteral("https://t.example:443/announce/")));
    QVERIFY(TrackerMerge::key(QStringLiteral("http://t.example:8080/announce")) !=
            TrackerMerge::key(QStringLiteral("http://t.example/announce")));
}

void TestTrackerMerge::keyKeepsThePathAndQuery()
{
    // A private tracker's passkey lives in the path or the query: two users, two trackers.
    QVERIFY(TrackerMerge::key(QStringLiteral("https://t.example/abc/announce")) !=
            TrackerMerge::key(QStringLiteral("https://t.example/ABC/announce")));
    QVERIFY(TrackerMerge::key(QStringLiteral("https://t.example/announce?passkey=1")) !=
            TrackerMerge::key(QStringLiteral("https://t.example/announce?passkey=2")));
}

void TestTrackerMerge::keyRefusesWhatIsNoTracker()
{
    QVERIFY(TrackerMerge::key(QStringLiteral("** [DHT] **")).isEmpty());
    QVERIFY(TrackerMerge::key(QString()).isEmpty());
    QVERIFY(TrackerMerge::key(QStringLiteral("not a url")).isEmpty());
}

void TestTrackerMerge::missingKeepsOrderAndSpelling()
{
    const QStringList incoming{QStringLiteral("udp://open.demonii.example:1337/announce"),
                               QStringLiteral("https://Tracker.Publicsite.example/announce"),
                               QStringLiteral("udp://tracker.opentrackr.example:1337/announce")};
    const QStringList existing{QStringLiteral("** [DHT] **"),
                               QStringLiteral("https://tracker.publicsite.example:443/announce")};
    QCOMPARE(TrackerMerge::missing(incoming, existing),
             (QStringList{QStringLiteral("udp://open.demonii.example:1337/announce"),
                          QStringLiteral("udp://tracker.opentrackr.example:1337/announce")}));
}

void TestTrackerMerge::missingDropsDuplicatesWithinTheSource()
{
    const QStringList incoming{QStringLiteral("udp://a.example:1/announce"),
                               QStringLiteral("UDP://A.example:1/announce"),
                               QStringLiteral(" udp://b.example:2/announce ")};
    QCOMPARE(TrackerMerge::missing(incoming, {}),
             (QStringList{QStringLiteral("udp://a.example:1/announce"),
                          QStringLiteral("udp://b.example:2/announce")}));
}

void TestTrackerMerge::nothingMissingWhenTheSameTrackers()
{
    const QStringList same{QStringLiteral("udp://a.example:1/announce")};
    QVERIFY(TrackerMerge::missing(same, same).isEmpty());
}

QTEST_APPLESS_MAIN(TestTrackerMerge)

#include "tst_trackermerge.moc"
