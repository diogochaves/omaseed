// AltPickWords: the notice of a speed pick made outside the window (DECISIONS C36 M9).

#include "core/AltPickWords.hpp"

#include <QTest>

using namespace omaseed::core;

class TestAltPickWords : public QObject
{
    Q_OBJECT

private slots:
    void words_data();
    void words();
};

void TestAltPickWords::words_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<qint64>("dl");
    QTest::addColumn<qint64>("up");
    QTest::addColumn<int>("minutes");
    QTest::addColumn<QString>("summary");
    QTest::addColumn<QString>("body");

    const qint64 kib = 1024;
    const QString defaults = QStringLiteral("These are qBittorrent's defaults, never changed. Set "
                                            "your own in Settings › qBittorrent › Speed.");

    QTest::newRow("off") << QStringLiteral("off") << qint64(0) << qint64(0) << 0
                         << QStringLiteral("Normal speeds") << QString();
    QTest::newRow("turtle") << QStringLiteral("turtle") << 1024 * kib << 200 * kib << 0
                            << QStringLiteral("Turtle") << QStringLiteral("↓ 1.00 MB/s ↑ 200 KB/s");
    QTest::newRow("turtle, upload only")
        << QStringLiteral("turtle") << qint64(0) << 200 * kib << 0 << QStringLiteral("Turtle")
        << QStringLiteral("↑ 200 KB/s");
    QTest::newRow("turtle, no limits") << QStringLiteral("turtle") << qint64(0) << qint64(0) << 0
                                       << QStringLiteral("Turtle") << QStringLiteral("No limits");
    QTest::newRow("turtle at qBittorrent's defaults")
        << QStringLiteral("turtle") << 10 * kib << 10 * kib << 0 << QStringLiteral("Turtle")
        << QStringLiteral("↓ 10 KB/s ↑ 10 KB/s\n") + defaults;
    QTest::newRow("turtle, one default only")
        << QStringLiteral("turtle") << 10 * kib << 20 * kib << 0 << QStringLiteral("Turtle")
        << QStringLiteral("↓ 10 KB/s ↑ 20 KB/s");
    QTest::newRow("turtle, not read yet") << QStringLiteral("turtle") << qint64(-1) << qint64(-1)
                                          << 0 << QStringLiteral("Turtle") << QString();
    QTest::newRow("burst for 60 min")
        << QStringLiteral("burst") << qint64(0) << 2048 * kib << 60
        << QStringLiteral("Burst for 60 min") << QStringLiteral("↑ 2.00 MB/s");
    QTest::newRow("burst for 30 min, both ways")
        << QStringLiteral("burst") << 5120 * kib << 2048 * kib << 30
        << QStringLiteral("Burst for 30 min") << QStringLiteral("↓ 5.00 MB/s ↑ 2.00 MB/s");
    QTest::newRow("burst until stopped")
        << QStringLiteral("burst") << qint64(0) << 2048 * kib << 0
        << QStringLiteral("Burst until stopped") << QStringLiteral("↑ 2.00 MB/s");
    QTest::newRow("burst with no limits")
        << QStringLiteral("burst") << qint64(0) << qint64(0) << 0
        << QStringLiteral("Burst until stopped") << QStringLiteral("No limits");
}

void TestAltPickWords::words()
{
    QFETCH(QString, kind);
    QFETCH(qint64, dl);
    QFETCH(qint64, up);
    QFETCH(int, minutes);
    QFETCH(QString, summary);
    QFETCH(QString, body);

    const AltPickWords::Notice notice = AltPickWords::notice(kind, dl, up, minutes);
    QCOMPARE(notice.summary, summary);
    QCOMPARE(notice.body, body);
}

QTEST_GUILESS_MAIN(TestAltPickWords)
#include "tst_altpickwords.moc"
