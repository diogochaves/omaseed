// ServerPreferences against a small fake qBittorrent that keeps its preferences in a JSON
// object: a change shows at once, a refusal puts the old value back, a value the server
// ignores comes back as the server's, and a reset drops late answers (docs/16 §3).

#include "core/ServerPreferences.hpp"
#include "core/engines/qbt/QbtClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <memory>

using omaseed::core::QbtClient;
using omaseed::core::ServerPreferences;

namespace {

/// Answers app/preferences from `prefs`, and app/setPreferences by merging its `json` into
/// them, except the keys in `ignored` (taken silently, like qBittorrent does with values it
/// does not like). `refuse` answers every POST with 400.
class FakeQbt : public QTcpServer
{
public:
    FakeQbt()
    {
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    QJsonObject prefs{{QStringLiteral("listen_port"), 26999},
                      {QStringLiteral("dht"), false},
                      {QStringLiteral("max_ratio"), -1},
                      {QStringLiteral("save_path"), QStringLiteral("/srv/downloads")}};
    QStringList ignored;
    bool refuse = false;
    int posts = 0;
    int gets = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QByteArray head = buffer->left(end);
            qsizetype length = 0;
            for (const QByteArray &line : head.split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            }
            if (buffer->size() - end - 4 < length)
                return;
            const QByteArray body = buffer->mid(end + 4, length);
            const QByteArray target = head.split(' ').value(1);
            buffer->clear();
            socket->write(answer(target, body));
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    static QByteArray reply(int code, const QByteArray &body)
    {
        return "HTTP/1.1 " + QByteArray::number(code) +
               " X\r\nContent-Length: " + QByteArray::number(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body;
    }

    QByteArray answer(const QByteArray &target, const QByteArray &body)
    {
        if (target.endsWith("app/preferences")) {
            ++gets;
            return reply(200, QJsonDocument(prefs).toJson(QJsonDocument::Compact));
        }
        if (target.endsWith("app/setPreferences")) {
            ++posts;
            if (refuse)
                return reply(400, "Bad Request");
            const QUrlQuery form(QString::fromUtf8(body));
            const QJsonObject changes =
                QJsonDocument::fromJson(
                    form.queryItemValue(QStringLiteral("json"), QUrl::FullyDecoded).toUtf8())
                    .object();
            for (auto it = changes.begin(); it != changes.end(); ++it) {
                if (!ignored.contains(it.key()))
                    prefs.insert(it.key(), it.value());
            }
            return reply(200, "");
        }
        return reply(404, "Not Found");
    }
};

} // namespace

class TestServerPreferences : public QObject
{
    Q_OBJECT

private slots:
    void loadsEveryKey();
    void appliesAtOnceAndReadsBack();
    void refusalPutsTheOldValueBack();
    void ignoredValueComesBackAsTheServers();
    void resetDropsLateAnswers();
    void numbersCompareByValue();
    void offlineChangesLandAtOnce();
};

void TestServerPreferences::loadsEveryKey()
{
    FakeQbt server;
    QbtClient client;
    client.setBaseUrl(server.url());
    ServerPreferences prefs(&client);
    QVERIFY(!prefs.isLoaded());
    prefs.reload();
    QVERIFY(prefs.isLoading());
    QTRY_VERIFY(prefs.isLoaded());
    QVERIFY(!prefs.isLoading());
    QCOMPARE(prefs.value(QStringLiteral("listen_port")).toInt(), 26999);
    QVERIFY(prefs.has(QStringLiteral("dht")));
    QVERIFY(!prefs.has(QStringLiteral("no_such_key")));
}

void TestServerPreferences::appliesAtOnceAndReadsBack()
{
    FakeQbt server;
    QbtClient client;
    client.setBaseUrl(server.url());
    ServerPreferences prefs(&client);
    prefs.reload();
    QTRY_VERIFY(prefs.isLoaded());

    QSignalSpy applied(&prefs, &ServerPreferences::applied);
    QSignalSpy posted(&prefs, &ServerPreferences::posted);
    prefs.apply({{QStringLiteral("dht"), true}});
    // Shown before the server answers, and pending until it does.
    QCOMPARE(prefs.value(QStringLiteral("dht")).toBool(), true);
    QCOMPARE(prefs.pendingKeys(), QStringList{QStringLiteral("dht")});
    QTRY_COMPARE(applied.count(), 1);
    QCOMPARE(posted.count(), 1);
    QVERIFY(prefs.pendingKeys().isEmpty());
    QCOMPARE(applied.at(0).at(0).toStringList(), QStringList{QStringLiteral("dht")});
    QVERIFY(applied.at(0).at(1).toStringList().isEmpty());
    QCOMPARE(server.prefs.value(QLatin1String("dht")).toBool(), true);
    QCOMPARE(server.gets, 2);
}

void TestServerPreferences::refusalPutsTheOldValueBack()
{
    FakeQbt server;
    QbtClient client;
    client.setBaseUrl(server.url());
    ServerPreferences prefs(&client);
    prefs.reload();
    QTRY_VERIFY(prefs.isLoaded());

    server.refuse = true;
    QSignalSpy failed(&prefs, &ServerPreferences::applyFailed);
    prefs.apply({{QStringLiteral("listen_port"), 30000}});
    QCOMPARE(prefs.value(QStringLiteral("listen_port")).toInt(), 30000);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(prefs.value(QStringLiteral("listen_port")).toInt(), 26999);
    QVERIFY(prefs.pendingKeys().isEmpty());
    QVERIFY(!failed.at(0).at(1).toString().isEmpty());
}

void TestServerPreferences::ignoredValueComesBackAsTheServers()
{
    FakeQbt server;
    server.ignored = {QStringLiteral("max_ratio")};
    QbtClient client;
    client.setBaseUrl(server.url());
    ServerPreferences prefs(&client);
    prefs.reload();
    QTRY_VERIFY(prefs.isLoaded());

    QSignalSpy applied(&prefs, &ServerPreferences::applied);
    prefs.apply({{QStringLiteral("max_ratio"), 2.5}, {QStringLiteral("dht"), true}});
    QTRY_COMPARE(applied.count(), 1);
    QCOMPARE(applied.at(0).at(1).toStringList(), QStringList{QStringLiteral("max_ratio")});
    QCOMPARE(prefs.value(QStringLiteral("max_ratio")).toInt(), -1);
    QCOMPARE(prefs.value(QStringLiteral("dht")).toBool(), true);
}

void TestServerPreferences::resetDropsLateAnswers()
{
    FakeQbt server;
    QbtClient client;
    client.setBaseUrl(server.url());
    ServerPreferences prefs(&client);
    QSignalSpy values(&prefs, &ServerPreferences::valuesChanged);
    prefs.reload();
    prefs.reset();
    QTest::qWait(300);
    QVERIFY(!prefs.isLoaded());
    QVERIFY(prefs.values().isEmpty());
    QCOMPARE(values.count(), 0);
}

void TestServerPreferences::numbersCompareByValue()
{
    QVERIFY(ServerPreferences::sameValue(10240, 10240.0));
    QVERIFY(!ServerPreferences::sameValue(1, 2));
    QVERIFY(ServerPreferences::sameValue(QStringLiteral("a"), QStringLiteral("a")));
    QVERIFY(!ServerPreferences::sameValue(true, 1));
}

void TestServerPreferences::offlineChangesLandAtOnce()
{
    ServerPreferences prefs(nullptr);
    prefs.setOffline({{QStringLiteral("dht"), false}});
    QVERIFY(prefs.isLoaded());
    QSignalSpy applied(&prefs, &ServerPreferences::applied);
    QSignalSpy posted(&prefs, &ServerPreferences::posted);
    prefs.apply({{QStringLiteral("dht"), true}});
    QCOMPARE(prefs.value(QStringLiteral("dht")).toBool(), true);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(posted.count(), 1);
    QCOMPARE(posted.at(0).at(1).toMap().value(QStringLiteral("dht")).toBool(), false);
    prefs.reset();
    QVERIFY(!prefs.isLoaded());
}

QTEST_GUILESS_MAIN(TestServerPreferences)
#include "tst_serverpreferences.moc"
