// #14 — the bar widget's heartbeat (docs/20 Q7): the rule that decides the tray icon, and the
// watcher that notices a touch, a removal and a file going stale. Everything happens in a
// temporary directory, never in the session's $XDG_RUNTIME_DIR.

#include "integration/WidgetHeartbeat.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::integration::WidgetHeartbeat;

namespace {

/// Writes the heartbeat file and stamps it `ageSeconds` in the past.
bool touch(const QString &path, double ageSeconds)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    const QDateTime when = QDateTime::currentDateTime().addMSecs(-qint64(ageSeconds * 1000.0));
    return file.setFileTime(when, QFileDevice::FileModificationTime);
}

} // namespace

class TestWidgetHeartbeat : public QObject
{
    Q_OBJECT

private slots:
    void theRule_data();
    void theRule();
    void noRuntimeDirectoryMeansNoWidget();
    void aTouchMakesTheWidgetAliveAndARemovalEndsIt();
    void anOldFileIsNotAlive();
    void aFileGoingStaleIsNoticedWithoutATouch();
    void theDirectoryIsMadePrivate();
};

void TestWidgetHeartbeat::theRule_data()
{
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("ageSeconds"); // -1: no file
    QTest::addColumn<bool>("visible");

    QTest::newRow("auto, no widget") << "auto" << -1 << true;
    QTest::newRow("auto, fresh") << "auto" << 10 << false;
    QTest::newRow("auto, 89 s") << "auto" << 89 << false;
    QTest::newRow("auto, 90 s is stale") << "auto" << 90 << true;
    QTest::newRow("auto, an hour old") << "auto" << 3600 << true;
    QTest::newRow("auto, stamped in the future") << "auto" << -30 << false;
    QTest::newRow("always, fresh") << "always" << 10 << true;
    QTest::newRow("always, no widget") << "always" << -1 << true;
    QTest::newRow("never, no widget") << "never" << -1 << false;
    QTest::newRow("never, fresh") << "never" << 10 << false;
    // An unknown mode is auto (App normalises it before it gets here anyway).
    QTest::newRow("unknown, fresh") << "bogus" << 10 << false;
}

void TestWidgetHeartbeat::theRule()
{
    QFETCH(QString, mode);
    QFETCH(int, ageSeconds);
    QFETCH(bool, visible);

    const QDateTime now = QDateTime::fromSecsSinceEpoch(1789346626);
    const QDateTime mtime = ageSeconds == -1 ? QDateTime() : now.addSecs(-ageSeconds);
    QCOMPARE(WidgetHeartbeat::trayIconVisible(mode, mtime, now), visible);
}

void TestWidgetHeartbeat::noRuntimeDirectoryMeansNoWidget()
{
    const QByteArray saved = qgetenv("XDG_RUNTIME_DIR");
    qunsetenv("XDG_RUNTIME_DIR");
    QVERIFY(WidgetHeartbeat::defaultDirectory().isEmpty());
    WidgetHeartbeat heartbeat;
    QVERIFY(!heartbeat.widgetAlive());
    QVERIFY(heartbeat.filePath().isEmpty());
    qputenv("XDG_RUNTIME_DIR", saved);
}

void TestWidgetHeartbeat::aTouchMakesTheWidgetAliveAndARemovalEndsIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WidgetHeartbeat heartbeat(nullptr, dir.path());
    QVERIFY(!heartbeat.widgetAlive());
    QSignalSpy changed(&heartbeat, &WidgetHeartbeat::widgetAliveChanged);

    // The widget's first touch: the directory watch sees the file appear.
    QVERIFY(touch(heartbeat.filePath(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat.widgetAlive(), 3000);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toBool(), true);

    // Another touch while alive changes nothing.
    QVERIFY(touch(heartbeat.filePath(), 0));
    QTest::qWait(200);
    QCOMPARE(changed.count(), 1);

    // The widget removed (a clean shutdown deletes the file): back at once.
    QVERIFY(QFile::remove(heartbeat.filePath()));
    QTRY_VERIFY_WITH_TIMEOUT(!heartbeat.widgetAlive(), 3000);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.at(1).at(0).toBool(), false);
}

void TestWidgetHeartbeat::anOldFileIsNotAlive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(touch(QDir(dir.path()).filePath(QStringLiteral("widget.alive")), 600));
    WidgetHeartbeat heartbeat(nullptr, dir.path());
    QVERIFY(!heartbeat.widgetAlive());

    // A touch of the existing file (its mtime only) is seen through the file's own watch.
    QSignalSpy changed(&heartbeat, &WidgetHeartbeat::widgetAliveChanged);
    QVERIFY(touch(heartbeat.filePath(), 0));
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat.widgetAlive(), 3000);
    QCOMPARE(changed.count(), 1);
}

void TestWidgetHeartbeat::aFileGoingStaleIsNoticedWithoutATouch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Half a second short of stale when the watcher starts: its timer must wake it then.
    QVERIFY(touch(QDir(dir.path()).filePath(QStringLiteral("widget.alive")),
                  WidgetHeartbeat::kFreshSeconds - 0.5));
    WidgetHeartbeat heartbeat(nullptr, dir.path());
    QVERIFY(heartbeat.widgetAlive());
    QSignalSpy changed(&heartbeat, &WidgetHeartbeat::widgetAliveChanged);
    QTRY_VERIFY_WITH_TIMEOUT(!heartbeat.widgetAlive(), 3000);
    QCOMPARE(changed.count(), 1);
}

void TestWidgetHeartbeat::theDirectoryIsMadePrivate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString runtime = QDir(dir.path()).filePath(QStringLiteral("omaseed"));
    WidgetHeartbeat heartbeat(nullptr, runtime);
    QVERIFY(QFileInfo(runtime).isDir());
    QCOMPARE(QFile::permissions(runtime) &
                 (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup |
                  QFileDevice::WriteOther | QFileDevice::ExeGroup | QFileDevice::ExeOther),
             QFileDevice::Permissions());
}

QTEST_GUILESS_MAIN(TestWidgetHeartbeat)
#include "tst_widgetheartbeat.moc"
