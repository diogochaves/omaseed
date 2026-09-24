// Regression tests for App glue that needs no server: the install runner's failure paths,
// and the other small App behaviours the 2026-09 review found broken.

#include "ui/app/App.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::app::App;

class TestAppGlue : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // APP-4
    void installThatCrashesAnswersOnce();
    void installThatCannotStartAnswersOnce();
    void installThatFailsCarriesItsStderr();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

void TestAppGlue::initTestCase()
{
    // Nothing here may reach the desktop's session bus (keychain, tray, notices).
    QVERIFY(m_dir.isValid());
    qputenv("DBUS_SESSION_BUS_ADDRESS",
            QStringLiteral("unix:path=%1/no-bus").arg(m_dir.path()).toUtf8());
}

void TestAppGlue::init()
{
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestAppGlue::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestAppGlue::installThatCrashesAnswersOnce()
{
    App app;
    QSignalSpy finished(&app, &App::installFinished);
    // A process killed by a signal: errorOccurred(Crashed), then finished(CrashExit).
    app.runInstall(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("kill -9 $$")});
    QVERIFY(finished.wait(5000));
    QTest::qWait(200); // a second answer would arrive right behind the first
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QVERIFY(!finished.constFirst().at(1).toString().isEmpty());

    // And the runner is free again.
    app.runInstall(QStringLiteral("true"), {});
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.at(1).at(0).toBool(), true);
}

void TestAppGlue::installThatCannotStartAnswersOnce()
{
    App app;
    QSignalSpy finished(&app, &App::installFinished);
    app.runInstall(QStringLiteral("/nonexistent/omaseed-no-such-program"), {});
    QVERIFY(finished.count() > 0 || finished.wait(5000));
    QTest::qWait(200);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
}

void TestAppGlue::installThatFailsCarriesItsStderr()
{
    App app;
    QSignalSpy finished(&app, &App::installFinished);
    app.runInstall(QStringLiteral("sh"),
                   {QStringLiteral("-c"), QStringLiteral("echo 'no polkit agent' >&2; exit 127")});
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.constFirst().at(0).toBool(), false);
    QCOMPARE(finished.constFirst().at(1).toString(), QStringLiteral("no polkit agent"));
}

QTEST_MAIN(TestAppGlue)

#include "tst_app_glue.moc"
