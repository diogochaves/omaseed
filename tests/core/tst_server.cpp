#include "core/ApiError.hpp"
#include "core/Server.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using omaseed::core::ApiError;
using omaseed::core::AuthMode;
using omaseed::core::PathMapping;
using omaseed::core::Server;

class TestServer : public QObject
{
    Q_OBJECT

private slots:
    void newIdIsStableAndUnique();
    void roundTripsThroughJson();
    void ignoresUnknownKeys();
    void keepsTheKeysANewerBuildWrote(); // C28, #84
    void anAuthModeItCantReadGoesBackAsItWas();
    void tellsAQbittorrentFromAnotherEngine_data();
    void tellsAQbittorrentFromAnotherEngine();
    void defaultsWhenFieldsAreMissing();
    void missingIdFailsToParse();
    void comparesByValue();
    void authModeRoundTrips();
    void credentialRefusedSaysWhichCredential();
    void trustedCertificateRoundTrips();
    void certificateRefusedSaysWhetherItChanged();
};

void TestServer::newIdIsStableAndUnique()
{
    const QString a = Server::newId();
    const QString b = Server::newId();
    QVERIFY(!a.isEmpty());
    QVERIFY(!b.isEmpty());
    QVERIFY(a != b);
}

void TestServer::roundTripsThroughJson()
{
    Server server;
    server.id = Server::newId();
    server.name = QStringLiteral("Playground");
    server.url = QUrl(QStringLiteral("http://127.0.0.1:8081"));
    server.authMode = AuthMode::ApiKey;
    server.username = QStringLiteral("diogo");
    server.pathMap = {
        PathMapping{QStringLiteral("/downloads"), QStringLiteral("/home/diogo/downloads")},
        PathMapping{QStringLiteral("/media"), QStringLiteral("/mnt/media")},
    };
    server.isDefault = true;
    server.addedAt = QDateTime::currentDateTimeUtc();

    const QJsonObject json = server.toJson();
    bool ok = false;
    const Server parsed = Server::fromJson(json, &ok);

    QVERIFY(ok);
    QCOMPARE(parsed.id, server.id);
    QCOMPARE(parsed.name, server.name);
    QCOMPARE(parsed.url, server.url);
    QCOMPARE(parsed.authMode, server.authMode);
    QCOMPARE(parsed.username, server.username);
    QCOMPARE(parsed.pathMap, server.pathMap);
    QCOMPARE(parsed.isDefault, server.isDefault);
    // Round-tripped through an ISO-8601 string; compare at second precision.
    QCOMPARE(parsed.addedAt.toSecsSinceEpoch(), server.addedAt.toSecsSinceEpoch());
}

void TestServer::ignoresUnknownKeys()
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("abc-123");
    json[QStringLiteral("name")] = QStringLiteral("Strict");
    json[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:8082");
    json[QStringLiteral("authMode")] = QStringLiteral("cookie");
    json[QStringLiteral("fromTheFuture")] = QStringLiteral("some field this build does not know");
    json[QStringLiteral("anotherOne")] = 42;

    bool ok = false;
    const Server server = Server::fromJson(json, &ok);
    QVERIFY(ok);
    QCOMPARE(server.id, QStringLiteral("abc-123"));
    QCOMPARE(server.name, QStringLiteral("Strict"));
    QCOMPARE(server.authMode, AuthMode::Cookie);
}

void TestServer::keepsTheKeysANewerBuildWrote()
{
    // An entry as the engines build (servers.json v2, #84) writes it, with one more key of a
    // later shape: everything this build doesn't read comes back out as it went in.
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("abc-123");
    json[QStringLiteral("name")] = QStringLiteral("seedbox");
    json[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:9091");
    json[QStringLiteral("authMode")] = QStringLiteral("cookie");
    json[QStringLiteral("username")] = QStringLiteral("user");
    json[QStringLiteral("pathMap")] = QJsonArray();
    json[QStringLiteral("isDefault")] = false;
    json[QStringLiteral("addedAt")] = QStringLiteral("2026-10-22T09:00:00Z");
    json[QStringLiteral("engine")] = QStringLiteral("transmission");
    json[QStringLiteral("engineVersion")] = QStringLiteral("4.1.0");
    json[QStringLiteral("later")] = QJsonObject{{QStringLiteral("nested"), QJsonArray{1, 2}}};

    bool ok = false;
    const Server server = Server::fromJson(json, &ok);
    QVERIFY(ok);
    QCOMPARE(server.extra.keys(),
             (QStringList{QStringLiteral("engine"),
                          QStringLiteral("engineVersion"),
                          QStringLiteral("later")}));
    QCOMPARE(server.engine(), QStringLiteral("transmission"));
    QCOMPARE(server.toJson(), json);

    // A change made here keeps them too.
    Server edited = server;
    edited.name = QStringLiteral("renamed");
    QJsonObject expected = json;
    expected[QStringLiteral("name")] = QStringLiteral("renamed");
    QCOMPARE(edited.toJson(), expected);
}

void TestServer::anAuthModeItCantReadGoesBackAsItWas()
{
    // The engines build's Deluge "password" reads as None here and is written back as it
    // was, unless someone picks a mode this build knows.
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("d-1");
    json[QStringLiteral("url")] = QStringLiteral("http://127.0.0.1:8112");
    json[QStringLiteral("authMode")] = QStringLiteral("password");
    json[QStringLiteral("engine")] = QStringLiteral("deluge");

    const Server server = Server::fromJson(json);
    QCOMPARE(server.authMode, AuthMode::None);
    QCOMPARE(server.toJson().value(QStringLiteral("authMode")).toString(),
             QStringLiteral("password"));

    Server picked = server;
    picked.authMode = AuthMode::Cookie;
    QCOMPARE(picked.toJson().value(QStringLiteral("authMode")).toString(),
             QStringLiteral("cookie"));
}

void TestServer::tellsAQbittorrentFromAnotherEngine_data()
{
    QTest::addColumn<QJsonValue>("engine"); // undefined: no key
    QTest::addColumn<bool>("qbittorrent");
    QTest::addColumn<QString>("name");
    QTest::newRow("none (v1)") << QJsonValue(QJsonValue::Undefined) << true << QString();
    QTest::newRow("qbittorrent") << QJsonValue(QStringLiteral("qbittorrent")) << true << QString();
    QTest::newRow("transmission") << QJsonValue(QStringLiteral("transmission")) << false
                                  << QStringLiteral("Transmission");
    QTest::newRow("deluge") << QJsonValue(QStringLiteral("deluge")) << false
                            << QStringLiteral("Deluge");
    QTest::newRow("unknown") << QJsonValue(QStringLiteral("rtorrent")) << false
                             << QStringLiteral("rtorrent");
    QTest::newRow("other spelling")
        << QJsonValue(QStringLiteral("qBittorrent")) << false << QString();
    QTest::newRow("empty") << QJsonValue(QString()) << false << QString();
    QTest::newRow("not text") << QJsonValue(7) << false << QString();
}

void TestServer::tellsAQbittorrentFromAnotherEngine()
{
    QFETCH(QJsonValue, engine);
    QFETCH(bool, qbittorrent);
    QFETCH(QString, name);
    QJsonObject json{{QStringLiteral("id"), QStringLiteral("x")}};
    if (!engine.isUndefined())
        json[QStringLiteral("engine")] = engine;
    const Server server = Server::fromJson(json);
    QCOMPARE(server.isQbittorrent(), qbittorrent);
    QCOMPARE(server.engineName(), name);
    // Never rewritten as qBittorrent, nor given an engine it didn't have.
    QCOMPARE(server.toJson().value(QStringLiteral("engine")), engine);
}

void TestServer::defaultsWhenFieldsAreMissing()
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("only-id");

    bool ok = false;
    const Server server = Server::fromJson(json, &ok);
    QVERIFY(ok);
    QCOMPARE(server.id, QStringLiteral("only-id"));
    QVERIFY(server.name.isEmpty());
    QCOMPARE(server.authMode, AuthMode::None);
    QVERIFY(server.pathMap.isEmpty());
    QVERIFY(!server.isDefault);
    QVERIFY(!server.addedAt.isValid());
}

void TestServer::missingIdFailsToParse()
{
    QJsonObject json;
    json[QStringLiteral("name")] = QStringLiteral("No id");

    bool ok = true;
    Server::fromJson(json, &ok);
    QVERIFY(!ok);
}

void TestServer::comparesByValue()
{
    Server a;
    a.id = QStringLiteral("same-id");
    a.name = QStringLiteral("A");

    Server b = a;
    QCOMPARE(a, b);

    b.name = QStringLiteral("B");
    QVERIFY(!(a == b));
}

void TestServer::authModeRoundTrips()
{
    for (AuthMode mode : {AuthMode::ApiKey, AuthMode::Cookie, AuthMode::None}) {
        bool ok = false;
        const QString text = omaseed::core::authModeToString(mode);
        QCOMPARE(omaseed::core::authModeFromString(text, &ok), mode);
        QVERIFY(ok);
    }

    bool ok = true;
    omaseed::core::authModeFromString(QStringLiteral("bogus"), &ok);
    QVERIFY(!ok);
}

void TestServer::credentialRefusedSaysWhichCredential()
{
    using omaseed::core::credentialRefusedText;
    // What qBittorrent 5.2 sends: 401 for a bad login, 403 for a bad key. Either way round
    // reads the same, since the two have moved between versions.
    const ApiError login(ApiError::Kind::Unauthorized, QStringLiteral("auth/login: Fails."), 401);
    const ApiError key(ApiError::Kind::Forbidden, QStringLiteral("app/version: Forbidden"), 403);
    QCOMPARE(credentialRefusedText(login, AuthMode::Cookie),
             QStringLiteral("wrong username or password"));
    QCOMPARE(credentialRefusedText(key, AuthMode::Cookie),
             QStringLiteral("wrong username or password"));
    QCOMPARE(credentialRefusedText(key, AuthMode::ApiKey),
             QStringLiteral("the server refused this API key"));
    QCOMPARE(credentialRefusedText(login, AuthMode::ApiKey),
             QStringLiteral("the server refused this API key"));

    // No credential sent to a server that wants one: say so, not "forbidden".
    QCOMPARE(credentialRefusedText(key, AuthMode::None),
             QStringLiteral("this server asks for a login"));
    QCOMPARE(credentialRefusedText(login, AuthMode::None),
             QStringLiteral("this server asks for a login"));

    // Nothing to say beyond the raw error: it never got that far.
    const ApiError refused(ApiError::Kind::Network, QStringLiteral("Connection refused"));
    QVERIFY(credentialRefusedText(refused, AuthMode::Cookie).isEmpty());
    const ApiError timeout(ApiError::Kind::Timeout, QStringLiteral("8 s"));
    QVERIFY(credentialRefusedText(timeout, AuthMode::ApiKey).isEmpty());
}

void TestServer::trustedCertificateRoundTrips()
{
    Server server;
    server.id = QStringLiteral("tls");
    server.url = QUrl(QStringLiteral("https://nas.invalid:8080"));

    // None trusted: the key is not written at all.
    QVERIFY(!server.toJson().contains(QStringLiteral("certificateSha256")));
    QVERIFY(Server::fromJson(server.toJson()).certificateSha256.isEmpty());

    server.certificateSha256 =
        QStringLiteral("9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");
    const QJsonObject json = server.toJson();
    QCOMPARE(json.value(QStringLiteral("certificateSha256")).toString(), server.certificateSha256);
    QCOMPARE(Server::fromJson(json).certificateSha256, server.certificateSha256);
    QCOMPARE(Server::fromJson(json), server);

    // Typed by hand in the colon form, it reads as the stored one.
    QJsonObject typed = json;
    typed[QStringLiteral("certificateSha256")] =
        QStringLiteral("9C:E0:61:8D:3D:BA:07:B6:AB:CE:06:A4:C4:DB:22:22:"
                       "F6:92:96:05:A3:44:B4:F8:EC:21:78:9F:27:61:A2:A2");
    QCOMPARE(Server::fromJson(typed).certificateSha256, server.certificateSha256);

    // A damaged value is no pin, never "accept anything".
    QJsonObject damaged = json;
    damaged[QStringLiteral("certificateSha256")] = QStringLiteral("9ce0618d");
    QVERIFY(Server::fromJson(damaged).certificateSha256.isEmpty());
    damaged[QStringLiteral("certificateSha256")] = 42;
    QVERIFY(Server::fromJson(damaged).certificateSha256.isEmpty());
}

void TestServer::certificateRefusedSaysWhetherItChanged()
{
    using omaseed::core::certificateRefusedText;
    QCOMPARE(certificateRefusedText(QString()), QStringLiteral("untrusted certificate"));
    QCOMPARE(certificateRefusedText(QStringLiteral(
                 "9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2")),
             QStringLiteral("the certificate changed"));
}

QTEST_APPLESS_MAIN(TestServer)

#include "tst_server.moc"
