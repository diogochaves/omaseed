// Format against the strings the spec draws (docs/05-ui-spec-v2.html §1, §2): every case
// here is copied from a figure, so a change in the rules fails the test instead of the eye.

#include "core/Format.hpp"

#include <QTest>

using namespace omaseed::core;

class TestFormat : public QObject
{
    Q_OBJECT

private slots:
    void speed_data();
    void speed();
    void size_data();
    void size();
    void eta_data();
    void eta();
    void etaShort_data();
    void etaShort();
    void ratio_data();
    void ratio();
    void percent_data();
    void percent();
    void duration_data();
    void duration();
    void relativeTimeToday();
    void relativeTimeYesterday();
    void relativeTimeOlder();
    void addedPrefixesRelativeTime();
    void seedsPeers();
    void speedShort_data();
    void speedShort();
    void percentRoundingTo100();
    void countGroupsWithSpaces();
    void piecesRow();
    void connectedTotalRow();
    void shareLimitRow();
};

void TestFormat::speed_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<QString>("expected");

    QTest::newRow("idle") << qint64(0) << QStringLiteral("0");
    QTest::newRow("negative") << qint64(-1) << QStringLiteral("0");
    QTest::newRow("bytes") << qint64(512) << QStringLiteral("512 B/s");
    QTest::newRow("spec hero up") << qint64(831488) << QStringLiteral("812 KB/s");
    QTest::newRow("spec hero down") << qint64(4414997) << QStringLiteral("4.21 MB/s");
    QTest::newRow("spec table row 1") << qint64(1929379) << QStringLiteral("1.84 MB/s");
    QTest::newRow("spec table row 4") << qint64(419840) << QStringLiteral("410 KB/s");
}

void TestFormat::speed()
{
    QFETCH(qint64, bytes);
    QFETCH(QString, expected);
    QCOMPARE(Format::speed(bytes), expected);
}

void TestFormat::size_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<QString>("expected");

    QTest::newRow("zero") << qint64(0) << QStringLiteral("0");
    QTest::newRow("unknown") << qint64(-1) << QStringLiteral("—");
    QTest::newRow("bytes") << qint64(900) << QStringLiteral("900 B");
    QTest::newRow("kilobytes") << qint64(9216) << QStringLiteral("9 KB");
    QTest::newRow("spec 412 MB") << qint64(432013312) << QStringLiteral("412 MB");
    QTest::newRow("spec 1.31 GB") << qint64(1404304589) << QStringLiteral("1.31 GB");
    QTest::newRow("spec 24.7 GB") << qint64(26522494894) << QStringLiteral("24.7 GB");
    QTest::newRow("spec 6.20 GB") << qint64(6657199308) << QStringLiteral("6.20 GB");
    QTest::newRow("carry") << qint64(1024 * 1024 - 1) << QStringLiteral("1.00 MB");
}

void TestFormat::size()
{
    QFETCH(qint64, bytes);
    QFETCH(QString, expected);
    QCOMPARE(Format::size(bytes), expected);
}

void TestFormat::eta_data()
{
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<QString>("expected");

    QTest::newRow("spec strip") << qint64(160) << QStringLiteral("2 min 40 s");
    QTest::newRow("spec table hours") << qint64(9660) << QStringLiteral("2h 41m");
    QTest::newRow("infinite") << qint64(8640000) << QStringLiteral("∞");
    QTest::newRow("unknown") << qint64(-1) << QStringLiteral("—");
    QTest::newRow("seconds") << qint64(40) << QStringLiteral("40 s");
    QTest::newRow("whole minutes") << qint64(1140) << QStringLiteral("19 min");
    QTest::newRow("days") << qint64(273600) << QStringLiteral("3d 4h");
}

void TestFormat::eta()
{
    QFETCH(qint64, seconds);
    QFETCH(QString, expected);
    QCOMPARE(Format::eta(seconds), expected);
}

void TestFormat::etaShort_data()
{
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<QString>("expected");

    QTest::newRow("spec row 1") << qint64(160) << QStringLiteral("2m 40s");
    QTest::newRow("spec row 2") << qint64(1140) << QStringLiteral("19m");
    QTest::newRow("spec row 3 pads") << qint64(425) << QStringLiteral("7m 05s");
    QTest::newRow("spec row 4") << qint64(9660) << QStringLiteral("2h 41m");
    QTest::newRow("spec row 5") << qint64(8640000) << QStringLiteral("∞");
    QTest::newRow("unknown") << qint64(-1) << QStringLiteral("—");
}

void TestFormat::etaShort()
{
    QFETCH(qint64, seconds);
    QFETCH(QString, expected);
    QCOMPARE(Format::etaShort(seconds), expected);
}

void TestFormat::ratio_data()
{
    QTest::addColumn<double>("value");
    QTest::addColumn<QString>("expected");

    QTest::newRow("spec strip") << 0.15 << QStringLiteral("0.15");
    QTest::newRow("spec palette") << 4.1 << QStringLiteral("4.10");
    QTest::newRow("none yet") << -1.0 << QStringLiteral("0.00");
    QTest::newRow("absurd") << 12000.0 << QStringLiteral("∞");
}

void TestFormat::ratio()
{
    QFETCH(double, value);
    QFETCH(QString, expected);
    QCOMPARE(Format::ratio(value), expected);
}

void TestFormat::percent_data()
{
    QTest::addColumn<double>("fraction");
    QTest::addColumn<QString>("spaced");
    QTest::addColumn<QString>("compact");

    QTest::newRow("spec strip") << 0.489 << QStringLiteral("48.9 %") << QStringLiteral("48.9%");
    QTest::newRow("done") << 1.0 << QStringLiteral("100 %") << QStringLiteral("100%");
    QTest::newRow("nothing") << 0.0 << QStringLiteral("0 %") << QStringLiteral("0%");
    QTest::newRow("spec row 4") << 0.031 << QStringLiteral("3.1 %") << QStringLiteral("3.1%");
}

void TestFormat::percent()
{
    QFETCH(double, fraction);
    QFETCH(QString, spaced);
    QFETCH(QString, compact);
    QCOMPARE(Format::percent(fraction), spaced);
    QCOMPARE(Format::percentCompact(fraction), compact);
}

void TestFormat::duration_data()
{
    QTest::addColumn<qint64>("seconds");
    QTest::addColumn<QString>("expected");

    QTest::newRow("spec time active") << qint64(862) << QStringLiteral("14 min 22 s");
    QTest::newRow("zero") << qint64(0) << QStringLiteral("0 s");
    QTest::newRow("hours") << qint64(9660) << QStringLiteral("2h 41m");
}

void TestFormat::duration()
{
    QFETCH(qint64, seconds);
    QFETCH(QString, expected);
    QCOMPARE(Format::duration(seconds), expected);
}

void TestFormat::relativeTimeToday()
{
    const QDateTime when(QDate::currentDate(), QTime(10, 41));
    QCOMPARE(Format::relativeTime(when), QStringLiteral("today 10:41"));
}

void TestFormat::relativeTimeYesterday()
{
    const QDateTime when(QDate::currentDate().addDays(-1), QTime(22, 5));
    QCOMPARE(Format::relativeTime(when), QStringLiteral("yesterday 22:05"));
}

void TestFormat::relativeTimeOlder()
{
    const QDateTime when(QDate(QDate::currentDate().year(), 9, 12), QTime(9, 41));
    if (when.date() == QDate::currentDate() || when.date() == QDate::currentDate().addDays(-1))
        QSKIP("12 September is today or yesterday: the relative form wins, as it should");
    QCOMPARE(Format::relativeTime(when), QStringLiteral("12 Sep 09:41"));
}

void TestFormat::addedPrefixesRelativeTime()
{
    const QDateTime when(QDate::currentDate(), QTime(10, 41));
    QCOMPARE(Format::added(when), QStringLiteral("added today 10:41"));
    QCOMPARE(Format::added(QDateTime()), QString());
    QCOMPARE(Format::relativeTime(QDateTime()), QString());
}

void TestFormat::seedsPeers()
{
    QCOMPARE(Format::seedsPeers(21, 42), QStringLiteral("21 / 42"));
    QCOMPARE(Format::seedsPeers(0, 0), QStringLiteral("0 / 0"));
}

void TestFormat::speedShort_data()
{
    QTest::addColumn<qint64>("bytesPerSecond");
    QTest::addColumn<QString>("text");

    // Spec §2 Fig. 4's ↓ and ↑ columns, value for value.
    QTest::newRow("fig4 1.02 M") << qint64(1069547) << QStringLiteral("1.02 M");
    QTest::newRow("fig4 744 K") << qint64(761856) << QStringLiteral("744 K");
    QTest::newRow("fig4 402 K") << qint64(411648) << QStringLiteral("402 K");
    QTest::newRow("fig4 118 K") << qint64(120832) << QStringLiteral("118 K");
    QTest::newRow("fig4 98 K") << qint64(100352) << QStringLiteral("98 K");
    QTest::newRow("fig4 12 K") << qint64(12288) << QStringLiteral("12 K");
    QTest::newRow("fig4 zero") << qint64(0) << QStringLiteral("0");
    QTest::newRow("negative is zero") << qint64(-1) << QStringLiteral("0");
    QTest::newRow("bare bytes") << qint64(512) << QStringLiteral("512");
    QTest::newRow("tens of megabytes") << qint64(48LL * 1024 * 1024) << QStringLiteral("48.0 M");
    QTest::newRow("gigabytes") << qint64(3LL * 1024 * 1024 * 1024) << QStringLiteral("3.00 G");
}

void TestFormat::speedShort()
{
    QFETCH(qint64, bytesPerSecond);
    QFETCH(QString, text);
    QCOMPARE(Format::speedShort(bytesPerSecond), text);
}

void TestFormat::percentRoundingTo100()
{
    // A folder whose only missing bytes are two 9 KB subtitles out of 3.9 GB is 99.9995 %
    // done; the spec prints that as "100 %" (spec §2 Fig. 2's root row), not "100.0 %".
    QCOMPARE(Format::percentCompact(0.9999956), QStringLiteral("100%"));
    QCOMPARE(Format::percent(0.9999956), QStringLiteral("100 %"));
    QCOMPARE(Format::percentCompact(0.9994), QStringLiteral("99.9%"));
}

void TestFormat::countGroupsWithSpaces()
{
    // Spec §2 Fig. 1: "1 264 × 1 MiB", "1 204 total".
    QCOMPARE(Format::count(1264), QStringLiteral("1 264"));
    QCOMPARE(Format::count(1204), QStringLiteral("1 204"));
    QCOMPARE(Format::count(617), QStringLiteral("617"));
    QCOMPARE(Format::count(0), QStringLiteral("0"));
    QCOMPARE(Format::count(1000000), QStringLiteral("1 000 000"));
    QCOMPARE(Format::count(-4200), QStringLiteral("-4 200"));
}

void TestFormat::piecesRow()
{
    QCOMPARE(Format::pieces(1264, 1024 * 1024, 617), QStringLiteral("1 264 × 1 MiB · 617 have"));
    QCOMPARE(Format::pieces(1, 16 * 1024, 0), QStringLiteral("1 × 16 KiB · 0 have"));
    QCOMPARE(Format::pieces(4096, 512 * 1024, 4096),
             QStringLiteral("4 096 × 512 KiB · 4 096 have"));
    // A magnet with no metadata yet has no pieces to print.
    QCOMPARE(Format::pieces(-1, -1, 0), QString());
    QCOMPARE(Format::pieces(0, 1024, 0), QString());
}

void TestFormat::connectedTotalRow()
{
    QCOMPARE(Format::connectedTotal(21, 1204), QStringLiteral("21 connected · 1 204 total"));
    QCOMPARE(Format::connectedTotal(42, 88), QStringLiteral("42 connected · 88 total"));
    QCOMPARE(Format::connectedTotal(-1, -1), QStringLiteral("0 connected · 0 total"));
}

void TestFormat::shareLimitRow()
{
    // qBittorrent's sentinels: -2 global, -1 unlimited, anything else a real limit.
    QCOMPARE(Format::shareLimit(-2.0, -2), QStringLiteral("global"));
    QCOMPARE(Format::shareLimit(2.0, -2), QStringLiteral("global · ratio 2.0"));
    QCOMPARE(Format::shareLimit(2.0, -1), QStringLiteral("ratio 2.0"));
    QCOMPARE(Format::shareLimit(-1.0, -1), QStringLiteral("unlimited"));
    QCOMPARE(Format::shareLimit(1.5, 10080), QStringLiteral("ratio 1.5 · seeding 7d 0h"));
}

QTEST_MAIN(TestFormat)
#include "tst_format.moc"
