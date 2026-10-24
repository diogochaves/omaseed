// core::AnnounceMask (#146, DECISIONS C27 CR5, board 49 §6): every shape a tracker's announce
// URL takes, with the key in the path, before or after "announce", in the query, percent-escaped,
// on udp with a port; the key never comes out. Every key and host here is invented.

#include "core/AnnounceMask.hpp"

#include <QTest>

namespace mask = omaseed::core::AnnounceMask;

class TestAnnounceMask : public QObject
{
    Q_OBJECT

private slots:
    void urls_data();
    void urls();
    void notAUrlIsAllDots();
    void everyUrlInATextIsMasked();
};

void TestAnnounceMask::urls_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("masked");
    QTest::addColumn<QString>("key");
    const QString d = mask::dots();
    QTest::newRow("after announce")
        << QStringLiteral("https://tracker-a.example/announce/a1b2c3d4e5")
        << QStringLiteral("https://tracker-a.example/announce/") + d
        << QStringLiteral("a1b2c3d4e5");
    QTest::newRow("before announce")
        << QStringLiteral("https://tracker-b.example/a1b2c3d4e5/announce")
        << QStringLiteral("https://tracker-b.example/") + d + QStringLiteral("/announce")
        << QStringLiteral("a1b2c3d4e5");
    QTest::newRow("two parts") << QStringLiteral("https://tracker-b.example/a/f00d/k3y5/announce")
                               << QStringLiteral("https://tracker-b.example/") + d +
                                      QStringLiteral("/announce")
                               << QStringLiteral("f00d");
    QTest::newRow("query") << QStringLiteral(
                                  "http://tracker-c.example:2710/announce.php?passkey=k3y5")
                           << QStringLiteral("http://tracker-c.example:2710/announce.php?") + d
                           << QStringLiteral("k3y5");
    QTest::newRow("path and query")
        << QStringLiteral("https://tracker-c.example/u/1234/announce?auth=k3y5&x=1")
        << QStringLiteral("https://tracker-c.example/") + d + QStringLiteral("/announce?") + d
        << QStringLiteral("k3y5");
    QTest::newRow("escaped") << QStringLiteral("https://tracker-e.example/announce/k%2B3y")
                             << QStringLiteral("https://tracker-e.example/announce/") + d
                             << QStringLiteral("3y");
    QTest::newRow("udp public") << QStringLiteral("udp://open.tracker-d.example:6969/announce")
                                << QStringLiteral("udp://open.tracker-d.example:6969/announce")
                                << QString();
    QTest::newRow("bare host") << QStringLiteral("https://tracker-f.example")
                               << QStringLiteral("https://tracker-f.example") << QString();
    QTest::newRow("userinfo") << QStringLiteral("https://user:k3y5@tracker-g.example/announce")
                              << QStringLiteral("https://tracker-g.example/announce")
                              << QStringLiteral("k3y5");
}

void TestAnnounceMask::urls()
{
    QFETCH(QString, url);
    QFETCH(QString, masked);
    QFETCH(QString, key);
    QCOMPARE(mask::maskUrl(url), masked);
    if (!key.isEmpty())
        QVERIFY(!mask::maskUrl(url).contains(key));
}

void TestAnnounceMask::notAUrlIsAllDots()
{
    QCOMPARE(mask::maskUrl(QStringLiteral("k3y5")), mask::dots());
    QCOMPARE(mask::maskUrl(QString()), mask::dots());
    QCOMPARE(mask::dots(), QString(8, QChar(0x2022)));
}

void TestAnnounceMask::everyUrlInATextIsMasked()
{
    const QString text = QStringLiteral(
        "Create new torrent file failed. Trackers: "
        "https://tracker-a.example/announce/a1b2c3d4e5|"
        "http://tracker-c.example/x?passkey=f00d (tier 2) and \"udp://u.example:1/k3y5\".");
    const QString masked = mask::maskText(text);
    QVERIFY(!masked.contains(QStringLiteral("a1b2c3d4e5")));
    QVERIFY(!masked.contains(QStringLiteral("f00d")));
    QVERIFY(!masked.contains(QStringLiteral("k3y5")));
    QVERIFY(masked.startsWith(QStringLiteral("Create new torrent file failed. Trackers: ")));
    QVERIFY(masked.contains(QStringLiteral("https://tracker-a.example/announce/") + mask::dots() +
                            QStringLiteral("|http://tracker-c.example/")));
    QVERIFY(masked.endsWith(QStringLiteral(" (tier 2) and \"udp://u.example:1/") + mask::dots() +
                            QStringLiteral("\".")));
    QCOMPARE(mask::maskText(QStringLiteral("no url here")), QStringLiteral("no url here"));
}

QTEST_APPLESS_MAIN(TestAnnounceMask)
#include "tst_announcemask.moc"
