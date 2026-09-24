// QbtClient against the two development qBittorrent servers:
//   QBT_TEST_URL   the playground, localhost auth bypassed, safe to write to
//   QBT_STRICT_URL the strict twin, real cookie and Bearer auth
// Both come from ~/.config/omaseed/dev.env; the test reads them from the environment and
// skips when they are missing or the server does not answer. Secrets are never printed and
// never live in this file.

#include "core/QbtClient.hpp"

#include "LiveServer.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::ServerInfo;

namespace {

QString env(const char *name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace

class TestQbtClientLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void bearerAgainstStrictServer();
    void syncMainDataFirstCallIsFull();
    void cookieLoginAgainstStrictServer();
    void wrongPasswordIsUnauthorized();
    void noCredentialIsForbiddenOnStrictServer();
    void noCredentialWorksOnPlayground();
    void torrentsInfoReturnsAnArray();
    void probeReportsServerInfo();

private:
    QUrl m_playground;
    QUrl m_strict;
    QString m_user;
    QString m_password;
    QString m_apiKey;
};

void TestQbtClientLive::initTestCase()
{
    m_playground = LiveServer::playground();
    m_strict = LiveServer::strict();
    m_user = env("QBT_STRICT_USER");
    m_password = env("QBT_STRICT_PASS");
    m_apiKey = env("QBT_STRICT_API_KEY");

    if (!answers(m_playground) || !answers(m_strict))
        QSKIP("the development servers do not answer: scripts/dev/test-server.sh start");
}

void TestQbtClientLive::bearerAgainstStrictServer()
{
    if (m_apiKey.isEmpty())
        QSKIP("QBT_STRICT_API_KEY is unset");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setApiKey(m_apiKey);

    const QbtResult<QString> version = await(this, client.appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY2(version.value().startsWith(QStringLiteral("v5")), qPrintable(version.value()));

    const QbtResult<QString> api = await(this, client.webApiVersion());
    QVERIFY2(!api.isError(), qPrintable(api.error().toString()));
    QVERIFY2(api.value().startsWith(QStringLiteral("2.15.")), qPrintable(api.value()));

    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
}

void TestQbtClientLive::syncMainDataFirstCallIsFull()
{
    if (m_apiKey.isEmpty())
        QSKIP("QBT_STRICT_API_KEY is unset");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setApiKey(m_apiKey);

    const QbtResult<QJsonObject> main = await(this, client.syncMainData(0));
    QVERIFY2(!main.isError(), qPrintable(main.error().toString()));
    QCOMPARE(main.value().value(QStringLiteral("full_update")).toBool(), true);
    QVERIFY(main.value().contains(QStringLiteral("server_state")));
    QVERIFY(main.value().value(QStringLiteral("rid")).toInt() > 0);
    QVERIFY(main.value().value(QStringLiteral("server_state")).isObject());
}

void TestQbtClientLive::cookieLoginAgainstStrictServer()
{
    if (m_user.isEmpty() || m_password.isEmpty())
        QSKIP("QBT_STRICT_USER / QBT_STRICT_PASS are unset");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setCredentials(m_user, m_password);

    const QbtResult<bool> loggedIn = await(this, client.login());
    QVERIFY2(!loggedIn.isError(), qPrintable(loggedIn.error().toString()));
    QVERIFY(loggedIn.value());

    const QbtResult<QString> version = await(this, client.appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY(version.value().startsWith(QStringLiteral("v5")));
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);

    // Without an explicit login the client logs in by itself on the first 403.
    QbtClient lazy;
    lazy.setBaseUrl(m_strict);
    lazy.setCredentials(m_user, m_password);
    const QbtResult<QJsonObject> transfer = await(this, lazy.transferInfo());
    QVERIFY2(!transfer.isError(), qPrintable(transfer.error().toString()));
    QVERIFY(transfer.value().contains(QStringLiteral("connection_status")));
}

void TestQbtClientLive::wrongPasswordIsUnauthorized()
{
    if (m_user.isEmpty())
        QSKIP("QBT_STRICT_USER is unset");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setCredentials(m_user, QStringLiteral("not-the-password"));

    const QbtResult<bool> loggedIn = await(this, client.login());
    QVERIFY(loggedIn.isError());
    QCOMPARE(loggedIn.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(client.authState(), QbtClient::AuthState::NeedsCredential);

    // A normal call with the bad password fails the same way, through the login retry.
    const QbtResult<QString> version = await(this, client.appVersion());
    QVERIFY(version.isError());
    QCOMPARE(version.error().kind(), ApiError::Kind::Unauthorized);

    // Put the right password back and the same client works again.
    if (m_password.isEmpty())
        QSKIP("QBT_STRICT_PASS is unset");
    client.setCredentials(m_user, m_password);
    const QbtResult<QString> again = await(this, client.appVersion());
    QVERIFY2(!again.isError(), qPrintable(again.error().toString()));
    QVERIFY(again.value().startsWith(QStringLiteral("v5")));
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
}

void TestQbtClientLive::noCredentialIsForbiddenOnStrictServer()
{
    QbtClient client;
    client.setBaseUrl(m_strict);

    const QbtResult<QString> version = await(this, client.appVersion());
    QVERIFY(version.isError());
    QCOMPARE(version.error().kind(), ApiError::Kind::Forbidden);
    QCOMPARE(version.error().httpStatus(), 403);
    QCOMPARE(client.authState(), QbtClient::AuthState::Forbidden);
}

void TestQbtClientLive::noCredentialWorksOnPlayground()
{
    QbtClient client;
    client.setBaseUrl(m_playground);

    const QbtResult<QString> version = await(this, client.appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY(version.value().startsWith(QStringLiteral("v5")));

    const QbtResult<QJsonObject> build = await(this, client.appBuildInfo());
    QVERIFY2(!build.isError(), qPrintable(build.error().toString()));
    QVERIFY(build.value().contains(QStringLiteral("libtorrent")));

    const QbtResult<bool> limits = await(this, client.transferSpeedLimitsMode());
    QVERIFY2(!limits.isError(), qPrintable(limits.error().toString()));
}

void TestQbtClientLive::torrentsInfoReturnsAnArray()
{
    QbtClient client;
    client.setBaseUrl(m_playground);

    const QbtResult<QJsonArray> torrents = await(this, client.torrentsInfo());
    QVERIFY2(!torrents.isError(), qPrintable(torrents.error().toString()));

    const QbtResult<QJsonObject> categories = await(this, client.torrentsCategories());
    QVERIFY2(!categories.isError(), qPrintable(categories.error().toString()));
    const QbtResult<QJsonArray> tags = await(this, client.torrentsTags());
    QVERIFY2(!tags.isError(), qPrintable(tags.error().toString()));

    if (torrents.value().isEmpty())
        return; // the details below need at least one torrent on the playground

    const QString hash = torrents.value().at(0).toObject().value(QStringLiteral("hash")).toString();
    QVERIFY(!hash.isEmpty());

    const QbtResult<QJsonObject> properties = await(this, client.torrentsProperties(hash));
    QVERIFY2(!properties.isError(), qPrintable(properties.error().toString()));
    const QbtResult<QJsonArray> files = await(this, client.torrentsFiles(hash));
    QVERIFY2(!files.isError(), qPrintable(files.error().toString()));
    const QbtResult<QJsonArray> trackers = await(this, client.torrentsTrackers(hash));
    QVERIFY2(!trackers.isError(), qPrintable(trackers.error().toString()));
    const QbtResult<QJsonObject> peers = await(this, client.torrentsPeers(hash, 0));
    QVERIFY2(!peers.isError(), qPrintable(peers.error().toString()));
    QVERIFY(peers.value().contains(QStringLiteral("peers")));

    // A hash the server does not know is a clean 404.
    const QbtResult<QJsonObject> missing = await(
        this,
        client.torrentsProperties(QStringLiteral("0000000000000000000000000000000000000000")));
    QVERIFY(missing.isError());
    QCOMPARE(missing.error().kind(), ApiError::Kind::NotFound);
}

void TestQbtClientLive::probeReportsServerInfo()
{
    QbtClient client;
    client.setBaseUrl(m_playground);
    QSignalSpy info(&client, &QbtClient::serverInfoChanged);

    const QbtResult<ServerInfo> probed = await(this, client.probe());
    QVERIFY2(!probed.isError(), qPrintable(probed.error().toString()));
    QVERIFY(probed.value().version.startsWith(QStringLiteral("v5")));
    QVERIFY(probed.value().apiVersion.startsWith(QStringLiteral("2.")));
    QCOMPARE(info.size(), 1);
    QCOMPARE(info.at(0).at(0).toString(), probed.value().version);
    QCOMPARE(info.at(0).at(1).toString(), probed.value().apiVersion);
}

QTEST_MAIN(TestQbtClientLive)

#include "tst_qbtclient_live.moc"
