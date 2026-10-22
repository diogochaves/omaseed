// QbtClient against the two development qBittorrent servers:
//   QBT_TEST_URL   the playground, localhost auth bypassed, safe to write to
//   QBT_STRICT_URL the strict twin, real cookie and Bearer auth
// Both come from ~/.config/omaseed/dev.env; the test reads them from the environment and
// skips when they are missing or the server does not answer. Secrets are never printed and
// never live in this file.

#include "QbtWire.hpp"

#include "LiveServer.hpp"

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::Reply;
using omaseed::core::Result;
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
Result<T> await(QObject *context, Reply<T> *reply, int timeoutMs = 20000)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const Result<T> &finished) {
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
    void logMainProblemsOnBothServers();
    void fetchMetadataOnBothServers();

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
        QSKIP("QBT_STRICT_API_KEY is unset: scripts/dev/test-server.sh start --strict "
              "saves it in ~/.config/omaseed/dev.env; load that (set -a; . dev.env)");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setApiKey(m_apiKey);

    const Result<QString> version = await(this, client.appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY2(version.value().startsWith(QStringLiteral("v5")), qPrintable(version.value()));

    const Result<QString> api = await(this, client.webApiVersion());
    QVERIFY2(!api.isError(), qPrintable(api.error().toString()));
    // 2.15 is qBittorrent 5.2, 2.16 is 5.3: anything from 2.15 on is the API this targets.
    QVERIFY2(QVersionNumber::fromString(api.value()) >= QVersionNumber(2, 15),
             qPrintable(api.value()));

    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
}

void TestQbtClientLive::syncMainDataFirstCallIsFull()
{
    if (m_apiKey.isEmpty())
        QSKIP("QBT_STRICT_API_KEY is unset: scripts/dev/test-server.sh start --strict "
              "saves it in ~/.config/omaseed/dev.env; load that (set -a; . dev.env)");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setApiKey(m_apiKey);

    const Result<QJsonObject> main = await(this, client.syncMainData(0));
    QVERIFY2(!main.isError(), qPrintable(main.error().toString()));
    QCOMPARE(main.value().value(QStringLiteral("full_update")).toBool(), true);
    QVERIFY(main.value().contains(QStringLiteral("server_state")));
    QVERIFY(main.value().value(QStringLiteral("rid")).toInt() > 0);
    QVERIFY(main.value().value(QStringLiteral("server_state")).isObject());
}

void TestQbtClientLive::cookieLoginAgainstStrictServer()
{
    if (m_user.isEmpty() || m_password.isEmpty())
        QSKIP("QBT_STRICT_USER / QBT_STRICT_PASS are unset: scripts/dev/test-server.sh start "
              "--strict saves them in ~/.config/omaseed/dev.env; load that (set -a; . dev.env)");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setCredentials(m_user, m_password);

    const Result<bool> loggedIn = await(this, client.login());
    QVERIFY2(!loggedIn.isError(), qPrintable(loggedIn.error().toString()));
    QVERIFY(loggedIn.value());

    const Result<QString> version = await(this, client.appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY(version.value().startsWith(QStringLiteral("v5")));
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);

    // Without an explicit login the client logs in by itself on the first 403.
    QbtClient lazy;
    lazy.setBaseUrl(m_strict);
    lazy.setCredentials(m_user, m_password);
    const Result<QJsonObject> transfer = await(this, lazy.transferInfo());
    QVERIFY2(!transfer.isError(), qPrintable(transfer.error().toString()));
    QVERIFY(transfer.value().contains(QStringLiteral("connection_status")));
}

void TestQbtClientLive::wrongPasswordIsUnauthorized()
{
    if (m_user.isEmpty())
        QSKIP("QBT_STRICT_USER is unset: scripts/dev/test-server.sh start --strict "
              "saves it in ~/.config/omaseed/dev.env; load that (set -a; . dev.env)");

    QbtClient client;
    client.setBaseUrl(m_strict);
    client.setCredentials(m_user, QStringLiteral("not-the-password"));

    const Result<bool> loggedIn = await(this, client.login());
    QVERIFY(loggedIn.isError());
    QCOMPARE(loggedIn.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(client.authState(), QbtClient::AuthState::NeedsCredential);

    // A normal call with the bad password fails the same way, through the login retry.
    const Result<QString> version = await(this, client.appVersion());
    QVERIFY(version.isError());
    QCOMPARE(version.error().kind(), ApiError::Kind::Unauthorized);

    // Put the right password back and the same client works again.
    if (m_password.isEmpty())
        QSKIP("QBT_STRICT_PASS is unset: scripts/dev/test-server.sh start --strict "
              "saves it in ~/.config/omaseed/dev.env; load that (set -a; . dev.env)");
    client.setCredentials(m_user, m_password);
    const Result<QString> again = await(this, client.appVersion());
    QVERIFY2(!again.isError(), qPrintable(again.error().toString()));
    QVERIFY(again.value().startsWith(QStringLiteral("v5")));
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
}

void TestQbtClientLive::noCredentialIsForbiddenOnStrictServer()
{
    QbtClient client;
    client.setBaseUrl(m_strict);

    const Result<QString> version = await(this, client.appVersion());
    QVERIFY(version.isError());
    QCOMPARE(version.error().kind(), ApiError::Kind::Forbidden);
    QCOMPARE(version.error().httpStatus(), 403);
    QCOMPARE(client.authState(), QbtClient::AuthState::Forbidden);
}

void TestQbtClientLive::noCredentialWorksOnPlayground()
{
    QbtClient client;
    client.setBaseUrl(m_playground);

    const Result<QString> version = await(this, client.appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY(version.value().startsWith(QStringLiteral("v5")));

    const Result<QJsonObject> build = await(this, client.appBuildInfo());
    QVERIFY2(!build.isError(), qPrintable(build.error().toString()));
    QVERIFY(build.value().contains(QStringLiteral("libtorrent")));

    const Result<bool> limits = await(this, client.transferSpeedLimitsMode());
    QVERIFY2(!limits.isError(), qPrintable(limits.error().toString()));
}

void TestQbtClientLive::torrentsInfoReturnsAnArray()
{
    QbtClient client;
    client.setBaseUrl(m_playground);

    const Result<QJsonArray> torrents = await(this, client.torrentsInfo());
    QVERIFY2(!torrents.isError(), qPrintable(torrents.error().toString()));

    const Result<QJsonObject> categories = await(this, client.torrentsCategories());
    QVERIFY2(!categories.isError(), qPrintable(categories.error().toString()));
    const Result<QJsonArray> tags = await(this, client.torrentsTags());
    QVERIFY2(!tags.isError(), qPrintable(tags.error().toString()));

    if (torrents.value().isEmpty())
        return; // the details below need at least one torrent on the playground

    const QString hash = torrents.value().at(0).toObject().value(QStringLiteral("hash")).toString();
    QVERIFY(!hash.isEmpty());

    const Result<QJsonObject> properties = await(this, client.torrentsProperties(hash));
    QVERIFY2(!properties.isError(), qPrintable(properties.error().toString()));
    const Result<QJsonArray> files = await(this, client.torrentsFiles(hash));
    QVERIFY2(!files.isError(), qPrintable(files.error().toString()));
    const Result<QJsonArray> trackers = await(this, client.torrentsTrackers(hash));
    QVERIFY2(!trackers.isError(), qPrintable(trackers.error().toString()));
    const Result<QJsonObject> peers = await(this, client.torrentsPeers(hash, 0));
    QVERIFY2(!peers.isError(), qPrintable(peers.error().toString()));
    QVERIFY(peers.value().contains(QStringLiteral("peers")));

    // A hash the server does not know is a clean 404.
    const Result<QJsonObject> missing = await(
        this,
        client.torrentsProperties(QStringLiteral("0000000000000000000000000000000000000000")));
    QVERIFY(missing.isError());
    QCOMPARE(missing.error().kind(), ApiError::Kind::NotFound);
}

// docs/28 (#54): warnings and critical lines only, and only those after the id given.
void TestQbtClientLive::logMainProblemsOnBothServers()
{
    QbtClient playground;
    playground.setBaseUrl(m_playground);
    const Result<QJsonArray> all = await(this, playground.logMainProblems(-1));
    QVERIFY2(!all.isError(), qPrintable(all.error().toString()));
    int lastId = -1;
    for (const QJsonValue &value : all.value()) {
        const QJsonObject entry = value.toObject();
        QVERIFY(entry.contains(QStringLiteral("message")));
        QVERIFY(entry.contains(QStringLiteral("timestamp")));
        const int type = entry.value(QStringLiteral("type")).toInt();
        QVERIFY2(type == 4 || type == 8, "log/main sent a normal or info line");
        lastId = entry.value(QStringLiteral("id")).toInt();
    }
    const Result<QJsonArray> after = await(this, playground.logMainProblems(lastId));
    QVERIFY2(!after.isError(), qPrintable(after.error().toString()));
    for (const QJsonValue &value : after.value())
        QVERIFY(value.toObject().value(QStringLiteral("id")).toInt() > lastId);

    if (m_apiKey.isEmpty())
        QSKIP("QBT_STRICT_API_KEY is unset: the strict half needs dev.env loaded");
    QbtClient strict;
    strict.setBaseUrl(m_strict);
    strict.setApiKey(m_apiKey);
    const Result<QJsonArray> signedIn = await(this, strict.logMainProblems(-1));
    QVERIFY2(!signedIn.isError(), qPrintable(signedIn.error().toString()));
}

void TestQbtClientLive::probeReportsServerInfo()
{
    QbtClient client;
    client.setBaseUrl(m_playground);
    QSignalSpy info(&client, &QbtClient::serverInfoChanged);

    const Result<ServerInfo> probed = await(this, client.probe());
    QVERIFY2(!probed.isError(), qPrintable(probed.error().toString()));
    QVERIFY(probed.value().version.startsWith(QStringLiteral("v5")));
    QVERIFY(probed.value().apiVersion.startsWith(QStringLiteral("2.")));
    QCOMPARE(info.size(), 1);
    QCOMPARE(info.at(0).at(0).toString(), probed.value().version);
    QCOMPARE(info.at(0).at(1).toString(), probed.value().apiVersion);
}

// #44 (docs/29 §2): a torrent the library already holds answers 200 at once, with its whole
// metadata; the paths carry the torrent's own folder. Only library torrents are asked, so no
// lookup is left running on either server (there is no way to cancel one).
void TestQbtClientLive::fetchMetadataOnBothServers()
{
    // `asked`: whether `client`'s library had a torrent to ask about.
    const auto check = [this](QbtClient &client, const char *server, bool *asked) {
        *asked = false;
        const Result<QJsonArray> all = await(this, client.torrentsInfo());
        QVERIFY2(!all.isError(), qPrintable(all.error().toString()));
        QJsonObject torrent;
        for (const QJsonValue &value : all.value()) {
            const QJsonObject candidate = value.toObject();
            // A magnet still fetching has no metadata: its lookup would answer 202 forever.
            if (candidate.value(QStringLiteral("has_metadata")).toBool(true) &&
                candidate.value(QStringLiteral("state")).toString() != QStringLiteral("metaDL")) {
                torrent = candidate;
                break;
            }
        }
        if (torrent.isEmpty()) {
            qInfo("%s has no torrent with metadata", server);
            return;
        }
        const QString hash = torrent.value(QStringLiteral("hash")).toString();
        const Result<QbtClient::FetchedMetadata> fetched =
            await(this, client.torrentsFetchMetadata(hash));
        QVERIFY2(!fetched.isError(), qPrintable(fetched.error().toString()));
        QVERIFY2(fetched.value().ready, "a library torrent should answer 200 at once");
        const QJsonObject body = fetched.value().body;
        QCOMPARE(body.value(QStringLiteral("hash")).toString(), hash);
        const QJsonObject info = body.value(QStringLiteral("info")).toObject();
        const QString name = info.value(QStringLiteral("name")).toString();
        QVERIFY(!name.isEmpty());
        const QJsonArray files = info.value(QStringLiteral("files")).toArray();
        QVERIFY(!files.isEmpty());
        for (const QJsonValue &value : files) {
            const QString path = value.toObject().value(QStringLiteral("path")).toString();
            QVERIFY2(path == name || path.startsWith(name + QLatin1Char('/')), qPrintable(path));
        }
        *asked = true;
    };

    QbtClient playground;
    playground.setBaseUrl(m_playground);
    bool asked = false;
    check(playground, "the playground", &asked);

    if (m_apiKey.isEmpty())
        QSKIP("QBT_STRICT_API_KEY is unset: the strict half needs dev.env loaded");
    // The API key's session is keyed by the key and never ends, so a lookup's result stays
    // with it between polls.
    QbtClient strict;
    strict.setBaseUrl(m_strict);
    strict.setApiKey(m_apiKey);
    check(strict, "the strict server", &asked);
    if (asked)
        return;
    // An empty strict library: a fixture added stopped for the question, then taken off.
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QbtClient::AddOptions options;
    options.stopped = true;
    const auto added = await(
        this, strict.torrentsAddFile(QStringLiteral("single.torrent"), file.readAll(), options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    const QString hash = QStringLiteral("857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04");
    for (int attempt = 0; attempt < 20 && !asked; ++attempt) {
        check(strict, "the strict server", &asked);
        if (!asked)
            QTest::qWait(250);
    }
    await(this, strict.torrentsDelete({hash}, true));
    QVERIFY(asked);
}

QTEST_MAIN(TestQbtClientLive)

#include "tst_qbtclient_live.moc"
