// T4.5 — App's settings half, live. The server list, the connection test, switching the live
// engine to another server, and the Appearance overrides reaching `Theme`.
//
// The network half runs against the **strict** test server on :8082 (real Bearer auth), never
// against Diogo's library on :8080. Without `QBT_STRICT_API_KEY` in the environment those
// tests skip rather than fail, so `ctest` stays green on a machine that has no dev.env.
//
// Two environment variables keep this test out of the real profile: `XDG_CONFIG_HOME` points
// `servers.json`, `state.json` and the file-backed secrets at a temporary directory, and
// `OMASEED_SECRET_SERVICE` gives the keychain backend a service name of its own, so nothing
// here can touch an entry the application itself wrote.

#include "core/SecretStore.hpp"
#include "core/ServerListModel.hpp"
#include "core/SyncEngine.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"
#include "ui/app/App.hpp"

#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using omaseed::app::App;
using omaseed::core::SecretStore;
using omaseed::core::SyncEngine;
using omaseed::integration::Theme;
using omaseed::integration::ThemeReader;

namespace {

const auto kStrictUrl = QStringLiteral("http://127.0.0.1:8082");
const auto kSecretService = QStringLiteral("io.github.diogochaves.omaseed-t45-servers-test");

QString strictKey()
{
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("QBT_STRICT_API_KEY"));
}

QVariantMap strictFields()
{
    return QVariantMap{
        {QStringLiteral("name"), QStringLiteral("strict")},
        {QStringLiteral("url"), kStrictUrl},
        {QStringLiteral("authMode"), QStringLiteral("apiKey")},
    };
}

} // namespace

class TestAppServers : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void testServerReportsVersionAndCount();
    void testServerRejectsAWrongKey();
    void setDefaultServerMovesTheEngine();
    void removeServerDeletesTheSecret();
    void appearanceOverridesReachTheTheme();

private:
    QTemporaryDir m_config;
};

void TestAppServers::initTestCase()
{
    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    qputenv("OMASEED_SECRET_SERVICE", kSecretService.toUtf8());
    // App falls back to the environment when no server is stored; these tests want the store.
    qunsetenv("OMASEED_SERVER_URL");
}

void TestAppServers::init()
{
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/servers.json"));
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/state.json"));
}

void TestAppServers::testServerReportsVersionAndCount()
{
    if (strictKey().isEmpty())
        QSKIP("QBT_STRICT_API_KEY is not set; load ~/.config/omaseed/dev.env to run this");

    App app;
    QSignalSpy tested(&app, &App::serverTested);

    const QString id = app.addServer(strictFields(), strictKey());
    QVERIFY(!id.isEmpty());
    QCOMPARE(app.serverList()->count(), 1);

    // No secret argument: the stored one has to be read back out of SecretStore.
    app.testServer(app.serverList()->serverMap(id), QString());
    QVERIFY(tested.wait(15000));

    const QList<QVariant> answer = tested.takeFirst();
    QCOMPARE(answer.at(0).toString(), id);
    QVERIFY2(answer.at(1).toBool(), qPrintable(answer.at(6).toString()));
    QVERIFY(!answer.at(2).toString().isEmpty()); // app/version
    QVERIFY(!answer.at(3).toString().isEmpty()); // app/webapiVersion
    QVERIFY(answer.at(4).toInt() >= 0);          // torrents/info length
    QVERIFY(answer.at(5).toInt() >= 0);          // latency

    // The row the Servers pane draws now carries the same numbers, and (Q23) the versions:
    // "v5.2.3" from the server is "5.2.3" in `version` and "v5.2.3" again in `versionText`.
    const QVariantMap row = app.serverList()->serverMap(id);
    QVERIFY(row.value(QStringLiteral("reachable")).toBool());
    QVERIFY(
        row.value(QStringLiteral("reachableText")).toString().startsWith(QStringLiteral("yes")));
    QCOMPARE(row.value(QStringLiteral("torrentCount")).toInt(), answer.at(4).toInt());
    const QString version = answer.at(2).toString();
    QCOMPARE(row.value(QStringLiteral("version")).toString(),
             version.startsWith(QLatin1Char('v')) ? version.mid(1) : version);
    QCOMPARE(row.value(QStringLiteral("versionText")).toString(),
             QStringLiteral("v%1").arg(row.value(QStringLiteral("version")).toString()));
    QCOMPARE(row.value(QStringLiteral("apiVersion")).toString(), answer.at(3).toString());
}

void TestAppServers::testServerRejectsAWrongKey()
{
    if (strictKey().isEmpty())
        QSKIP("QBT_STRICT_API_KEY is not set; load ~/.config/omaseed/dev.env to run this");

    App app;
    QSignalSpy tested(&app, &App::serverTested);

    app.testServer(strictFields(), QStringLiteral("not-the-key"));
    QVERIFY(tested.wait(15000));

    const QList<QVariant> answer = tested.takeFirst();
    QVERIFY(!answer.at(1).toBool());
    const QString error = answer.at(6).toString();
    QVERIFY2(error.contains(QStringLiteral("401")) || error.contains(QStringLiteral("403")) ||
                 error.contains(QStringLiteral("orbidden")) ||
                 error.contains(QStringLiteral("nauthorized")),
             qPrintable(error));
}

void TestAppServers::setDefaultServerMovesTheEngine()
{
    if (strictKey().isEmpty())
        QSKIP("QBT_STRICT_API_KEY is not set; load ~/.config/omaseed/dev.env to run this");

    App app;
    app.start(); // nothing stored yet: no engine, no server
    QVERIFY(!app.hasServer());

    QSignalSpy synced(app.engine(), &SyncEngine::firstSyncFinished);
    const QString id = app.addServer(strictFields(), strictKey());
    QVERIFY(!id.isEmpty());
    // The first server added is made default, and App moves the live engine onto it.
    QVERIFY(app.hasServer());
    QCOMPARE(app.serverName(), QStringLiteral("strict"));
    QVERIFY2(synced.wait(20000), "the engine never finished a first sync against :8082");
    QVERIFY(app.globalState()->connected());

    // A second server, then switch: the engine has to end up on the new one.
    QVariantMap second = strictFields();
    second[QStringLiteral("name")] = QStringLiteral("strict-twin");
    const QString other = app.addServer(second, strictKey());
    QVERIFY(!other.isEmpty());
    QCOMPARE(app.serverCount(), 2);

    QSignalSpy serverChanged(&app, &App::serverChanged);
    QSignalSpy syncedAgain(app.engine(), &SyncEngine::firstSyncFinished);
    QVERIFY(app.setDefaultServer(other));
    QVERIFY(serverChanged.count() > 0);
    QCOMPARE(app.serverName(), QStringLiteral("strict-twin"));
    QCOMPARE(app.serverList()->defaultId(), other);
    QVERIFY2(syncedAgain.wait(20000), "the engine did not sync after the default moved");
}

void TestAppServers::removeServerDeletesTheSecret()
{
    App app;
    const QString id =
        app.addServer(strictFields(), QStringLiteral("a-secret-that-is-not-the-real-one"));
    QVERIFY(!id.isEmpty());
    // App's own SecretStore probes its backend and writes the secret from queued jobs; a
    // second store (its own queue, its own probe) polls until that write has landed.
    SecretStore secrets(kSecretService);
    bool stored = false;
    for (int attempt = 0; attempt < 50 && !stored; ++attempt) {
        auto *job = secrets.secret(id, SecretStore::Kind::ApiKey);
        QSignalSpy finished(job, &omaseed::core::SecretStoreJob::finished);
        QVERIFY(finished.wait(5000));
        stored = finished.at(0).at(0).toBool();
        if (!stored)
            QTest::qWait(100);
    }
    QVERIFY2(stored, "the secret was not stored with the server");

    QVERIFY(app.removeServer(id));
    QCOMPARE(app.serverList()->count(), 0);

    // The removal is queued the same way, so poll until the entry is gone.
    bool gone = false;
    for (int attempt = 0; attempt < 50 && !gone; ++attempt) {
        auto *job = secrets.secret(id, SecretStore::Kind::ApiKey);
        QSignalSpy finished(job, &omaseed::core::SecretStoreJob::finished);
        QVERIFY(finished.wait(5000));
        const bool ok = finished.at(0).at(0).toBool();
        const QString value = finished.at(0).at(1).toString();
        gone = !ok || value.isEmpty();
        if (!gone)
            QTest::qWait(100);
    }
    QVERIFY2(gone, "the secret outlived the server it belonged to");
}

void TestAppServers::appearanceOverridesReachTheTheme()
{
    QTemporaryDir themeHome;
    QVERIFY(themeHome.isValid());
    ThemeReader::Paths paths;
    paths.stateCurrentDir = themeHome.path() + QStringLiteral("/state");
    paths.configOmarchyDir = themeHome.path() + QStringLiteral("/config");
    ThemeReader reader(paths);
    reader.reload();
    Theme theme(&reader);

    App app;
    app.attachTheme(&reader);
    // Nothing on disk to read: the bundled Tokyo Night at the default base size.
    const int defaultBody = theme.font()->body();
    QCOMPARE(app.baseSizeOverride(), 0);

    app.setBaseSizeOverride(20);
    QCOMPARE(app.baseSizeOverride(), 20);
    QCOMPARE(theme.font()->baseSize(), 20);
    QCOMPARE(theme.font()->body(), 20);
    QVERIFY(theme.font()->body() != defaultBody);

    // Out of range means "follow the shell".
    app.setBaseSizeOverride(3);
    QCOMPARE(app.baseSizeOverride(), 0);
    QCOMPARE(theme.font()->body(), defaultBody);

    // The palette override: off pins the bundled theme.
    QVERIFY(app.followOmarchyTheme());
    app.setFollowOmarchyTheme(false);
    QCOMPARE(theme.themeName(), QStringLiteral("tokyo-night"));

    // The transparency override is a plain percentage App keeps for QML; 0 means "default".
    app.setWindowAlphaOverride(70);
    QCOMPARE(app.windowAlphaOverride(), 70);
    app.setWindowAlphaOverride(5);
    QCOMPARE(app.windowAlphaOverride(), 0);

    // Which side the hero's speed pair sits on; anything but "right" reads back as "left".
    QCOMPARE(app.speedsAlignment(), QStringLiteral("left"));
    app.setSpeedsAlignment(QStringLiteral("right"));
    QCOMPARE(app.speedsAlignment(), QStringLiteral("right"));
    app.setSpeedsAlignment(QStringLiteral("sideways"));
    QCOMPARE(app.speedsAlignment(), QStringLiteral("left"));
    app.setSpeedsAlignment(QStringLiteral("right"));

    // docs/08 §5: the rail's three looks and its hidden flag; unknown values read as the
    // defaults.
    QCOMPARE(app.railScrollHint(), QStringLiteral("chevrons"));
    app.setRailScrollHint(QStringLiteral("sideways"));
    QCOMPARE(app.railScrollHint(), QStringLiteral("chevrons"));
    app.setRailScrollHint(QStringLiteral("fade"));
    QCOMPARE(app.surfaceTint(), 0);
    app.setSurfaceTint(5);
    QCOMPARE(app.surfaceTint(), 0);
    app.setSurfaceTint(8);
    QCOMPARE(app.trackerHostStyle(), QStringLiteral("prefix"));
    app.setTrackerHostStyle(QStringLiteral("left"));
    QCOMPARE(app.railHidden(), false);
    app.setRailHidden(true);
    // docs/10 §5: where Details sits.
    QCOMPARE(app.detailsPlacement(), QStringLiteral("auto"));
    app.setDetailsPlacement(QStringLiteral("left"));
    QCOMPARE(app.detailsPlacement(), QStringLiteral("auto"));
    app.setDetailsPlacement(QStringLiteral("bottom"));

    // The General pane's two flags, the tray mode and the alignment round-trip through
    // state.json.
    app.setShowServersGroup(false);
    app.setPathMapEnabled(false);
    app.setTrayIconMode(QStringLiteral("never"));
    App reopened;
    QCOMPARE(reopened.showServersGroup(), false);
    QCOMPARE(reopened.pathMapEnabled(), false);
    QCOMPARE(reopened.trayIconMode(), QStringLiteral("never"));
    QCOMPARE(reopened.speedsAlignment(), QStringLiteral("right"));
    QCOMPARE(reopened.railScrollHint(), QStringLiteral("fade"));
    QCOMPARE(reopened.surfaceTint(), 8);
    QCOMPARE(reopened.trackerHostStyle(), QStringLiteral("left"));
    QCOMPARE(reopened.railHidden(), true);
    QCOMPARE(reopened.detailsPlacement(), QStringLiteral("bottom"));
    QCOMPARE(reopened.baseSizeOverride(), 0);
    QCOMPARE(reopened.followOmarchyTheme(), false);
}

QTEST_MAIN(TestAppServers)

#include "tst_app_servers.moc"
