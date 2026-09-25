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
// here can touch an entry the application itself wrote. Every server these tests add lands
// in that service (the strict key among them), and a failed QVERIFY skips the test's own
// removeServer, so initTestCase and cleanupTestCase empty the whole test service.

#include "core/ApiError.hpp"
#include "core/QbtClient.hpp"
#include "core/SecretStore.hpp"
#include "core/ServerListModel.hpp"
#include "core/SyncEngine.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"
#include "ui/app/App.hpp"

#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
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

/// Deletes every keyring entry in the test service, whatever its key. QtKeychain cannot list
/// a service, so this goes through libsecret's own CLI; QtKeychain's libsecret schema stores
/// the service as the `server` attribute. Only ever `kSecretService`, never the app's own.
void clearTestSecretService()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (tool.isEmpty()) {
        qWarning("secret-tool not found: entries in %s are left behind",
                 qPrintable(kSecretService));
        return;
    }
    QProcess clear;
    clear.start(tool, {QStringLiteral("clear"), QStringLiteral("server"), kSecretService});
    if (!clear.waitForFinished(10000)) {
        clear.kill();
        clear.waitForFinished(1000);
        qWarning("secret-tool clear timed out (a locked keyring?)");
    }
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
    void cleanupTestCase();
    void init();

    void testServerReportsVersionAndCount();
    void testServerRejectsAWrongKey();
    void setDefaultServerMovesTheEngine();
    void removeServerDeletesTheSecret();
    void appearanceOverridesReachTheTheme();
    void aLateKeychainAnswerStaysWithItsServer();
    void aGoodSyncAfterAuthRequiredClearsServerLost();
    void webUiChangesMoveTheSavedUrl();

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
    clearTestSecretService(); // whatever a crashed earlier run left
}

void TestAppServers::cleanupTestCase()
{
    clearTestSecretService();
}

void TestAppServers::init()
{
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/servers.json"));
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/state.json"));
}

void TestAppServers::testServerReportsVersionAndCount()
{
    if (strictKey().isEmpty())
        QSKIP("QBT_STRICT_API_KEY is not set: scripts/dev/test-server.sh start --strict "
              "writes it to ~/.config/omaseed/dev.env; load that to run this");

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
        QSKIP("QBT_STRICT_API_KEY is not set: scripts/dev/test-server.sh start --strict "
              "writes it to ~/.config/omaseed/dev.env; load that to run this");

    App app;
    QSignalSpy tested(&app, &App::serverTested);

    app.testServer(strictFields(), QStringLiteral("not-the-key"));
    QVERIFY(tested.wait(15000));

    const QList<QVariant> answer = tested.takeFirst();
    QVERIFY(!answer.at(1).toBool());
    // The words first, the raw error kept for the dim line under them.
    QCOMPARE(answer.at(6).toString(), QStringLiteral("the server refused this API key"));
    const QString detail = answer.at(7).toString();
    QVERIFY2(detail.contains(QStringLiteral("401")) || detail.contains(QStringLiteral("403")),
             qPrintable(detail));
}

void TestAppServers::setDefaultServerMovesTheEngine()
{
    if (strictKey().isEmpty())
        QSKIP("QBT_STRICT_API_KEY is not set: scripts/dev/test-server.sh start --strict "
              "writes it to ~/.config/omaseed/dev.env; load that to run this");

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

// APP-2 / INT-6: switch to an API-key server and away again before its keychain read
// returns. The late answer must not put that key on the client, which by then points at the
// next server. Neither URL is ever contacted successfully: both are closed local ports.
void TestAppServers::aLateKeychainAnswerStaysWithItsServer()
{
    App app;
    app.start();

    const QVariantMap keyed{
        {QStringLiteral("name"), QStringLiteral("keyed")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
        {QStringLiteral("authMode"), QStringLiteral("apiKey")},
    };
    const QVariantMap open{
        {QStringLiteral("name"), QStringLiteral("open")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:7")},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    };
    // The first server added becomes the live one, and its key read is queued behind the
    // write; the switch below happens before either has run.
    const QString keyedId = app.addServer(keyed, QStringLiteral("key-of-the-first-server"));
    QVERIFY(!keyedId.isEmpty());
    const QString openId = app.addServer(open, QString());
    QVERIFY(!openId.isEmpty());
    QVERIFY(app.setDefaultServer(openId));
    QCOMPARE(app.serverName(), QStringLiteral("open"));

    // Wait until the first server's secret is readable (App's write has landed), then give
    // App's own queued read time to answer too.
    SecretStore secrets(kSecretService);
    bool stored = false;
    for (int attempt = 0; attempt < 50 && !stored; ++attempt) {
        auto *job = secrets.secret(keyedId, SecretStore::Kind::ApiKey);
        QSignalSpy finished(job, &omaseed::core::SecretStoreJob::finished);
        QVERIFY(finished.wait(5000));
        stored = finished.at(0).at(0).toBool();
        if (!stored)
            QTest::qWait(100);
    }
    QVERIFY(stored);
    QTest::qWait(1000);

    QVERIFY2(!app.client()->hasApiKey(), "the first server's key reached the second server");
    QCOMPARE(app.client()->baseUrl(), QUrl(QStringLiteral("http://127.0.0.1:7")));

    QVERIFY(app.removeServer(keyedId));
    QVERIFY(app.removeServer(openId));
}

// APP-3: authRequired marks the server lost; serverBack never follows it, so the first
// successful sync afterwards has to clear the mark (and with it the tray's "disconnected").
void TestAppServers::aGoodSyncAfterAuthRequiredClearsServerLost()
{
    App app;
    QVERIFY(!app.isServerLost());
    Q_EMIT app.engine()->authRequired(
        omaseed::core::ApiError(omaseed::core::ApiError::Kind::Forbidden, QStringLiteral("403")));
    QVERIFY(app.isServerLost());
    Q_EMIT app.engine()->synced(false);
    QVERIFY(!app.isServerLost());
}

void TestAppServers::webUiChangesMoveTheSavedUrl()
{
    // docs/16 §3: a Web UI port or HTTPS change moves the saved server with it, unless the
    // saved URL goes through something else (a reverse proxy on its own port).
    using omaseed::app::App;
    const QUrl direct(QStringLiteral("http://127.0.0.1:8081"));
    const QVariantMap before{{QStringLiteral("web_ui_port"), 8081},
                             {QStringLiteral("use_https"), false}};
    QCOMPARE(App::urlAfterPreferences(direct, {{QStringLiteral("web_ui_port"), 9091}}, before),
             QUrl(QStringLiteral("http://127.0.0.1:9091")));
    QCOMPARE(App::urlAfterPreferences(direct, {{QStringLiteral("use_https"), true}}, before),
             QUrl(QStringLiteral("https://127.0.0.1:8081")));
    const QUrl proxied(QStringLiteral("https://torrents.example.org/qbt/"));
    QCOMPARE(App::urlAfterPreferences(proxied, {{QStringLiteral("web_ui_port"), 9091}}, before),
             proxied);
    QCOMPARE(App::urlAfterPreferences(proxied, {{QStringLiteral("use_https"), false}}, before),
             proxied);
    QCOMPARE(App::urlAfterPreferences(direct, {{QStringLiteral("dht"), true}}, before), direct);
}

QTEST_MAIN(TestAppServers)

#include "tst_app_servers.moc"
