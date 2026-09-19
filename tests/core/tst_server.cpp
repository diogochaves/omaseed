#include "core/Server.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

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
    void defaultsWhenFieldsAreMissing();
    void missingIdFailsToParse();
    void comparesByValue();
    void authModeRoundTrips();
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

QTEST_APPLESS_MAIN(TestServer)

#include "tst_server.moc"
