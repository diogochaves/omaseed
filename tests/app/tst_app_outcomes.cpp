// What a quick action on torrents came to (board 45 §2, DECISIONS C18 D1–D3, #130): every
// action from the table says it in the strip (App.outcomes), done or failed, never in the
// footer; several requests for one action (limits) are one strip.

#include "core/ActionOutcome.hpp"
#include "ui/app/App.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>

using omaseed::app::App;

namespace {

/// Two torrents, "Arcade Set 01" and "Short Film 02". Every POST answers "Ok." but those
/// listed in `refuse` (an endpoint's path, "torrents/recheck"), which answer 409.
class OutcomeServer : public QTcpServer
{
public:
    QString one = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    QString two = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    QStringList refuse;
    QStringList posts;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            qsizetype length = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            buffer->clear();
            const QString path = QUrl(QString::fromUtf8(target)).path().mid(8); // after /api/v2/

            QByteArray status = "200 OK";
            QByteArray body = "Ok.";
            if (path == QLatin1String("app/version")) {
                body = "v5.2.4";
            } else if (path == QLatin1String("app/webapiVersion")) {
                body = "2.15.1";
            } else if (path == QLatin1String("sync/maindata")) {
                const auto torrent = [](const QString &name) {
                    return QJsonObject{{QStringLiteral("name"), name},
                                       {QStringLiteral("state"), QStringLiteral("uploading")},
                                       {QStringLiteral("save_path"), QStringLiteral("/srv/a")}};
                };
                const QJsonObject data{
                    {QStringLiteral("rid"), 1},
                    {QStringLiteral("full_update"), true},
                    {QStringLiteral("torrents"),
                     QJsonObject{{one, torrent(QStringLiteral("Arcade Set 01"))},
                                 {two, torrent(QStringLiteral("Short Film 02"))}}}};
                body = QJsonDocument(data).toJson(QJsonDocument::Compact);
            } else if (path == QLatin1String("torrents/info")) {
                body = "[]";
            } else if (path.startsWith(QLatin1String("torrents/"))) {
                posts << path;
                if (refuse.contains(path)) {
                    status = "409 Conflict";
                    body = "Torrent queueing must be enabled";
                }
            }
            socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

QString current(App &app, const char *key)
{
    return app.outcomes()->current().value(QString::fromLatin1(key)).toString();
}

QString markText(App &app, const QString &hash)
{
    return app.outcomes()->rowMark(hash).value(QStringLiteral("text")).toString();
}

} // namespace

class TestAppOutcomes : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aStopSaysSoInTheStripAndOnTheRow();
    void aRefusalIsAFailedStripNotAFooterNote();
    void limitsAreOneStrip();
    void aDeleteNamesWhatWentAndMarksNoRow();

private:
    bool startOn(App &app, OutcomeServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppOutcomes::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppOutcomes::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppOutcomes::startOn(App &app, OutcomeServer &server)
{
    if (!server.isListening() && !server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.torrents()->count() == 2; }, 5000);
}

void TestAppOutcomes::aStopSaysSoInTheStripAndOnTheRow()
{
    OutcomeServer server;
    App app;
    QVERIFY(startOn(app, server));
    app.stopStart({server.one});
    QTRY_COMPARE_WITH_TIMEOUT(
        current(app, "headline"), QStringLiteral("Arcade Set 01 stopped"), 3000);
    QCOMPARE(current(app, "kind"), QStringLiteral("done"));
    QCOMPARE(markText(app, server.one), QStringLiteral("stopped"));
    QVERIFY(markText(app, server.two).isEmpty());

    app.reannounce({server.one, server.two});
    QTRY_COMPARE_WITH_TIMEOUT(current(app, "headline"), QStringLiteral("2 reannounced"), 3000);
    app.engine()->stop();
}

void TestAppOutcomes::aRefusalIsAFailedStripNotAFooterNote()
{
    OutcomeServer server;
    server.refuse << QStringLiteral("torrents/increasePrio");
    App app;
    QVERIFY(startOn(app, server));
    QSignalSpy footer(&app, &App::actionFailed);
    app.queueUp({server.two});
    QTRY_COMPARE_WITH_TIMEOUT(current(app, "kind"), QStringLiteral("failed"), 3000);
    QCOMPARE(current(app, "headline"), QStringLiteral("Short Film 02 didn't move in the queue"));
    QVERIFY(!current(app, "line").isEmpty());
    QVERIFY(!current(app, "details").isEmpty());
    QCOMPARE(markText(app, server.two), QStringLiteral("didn't move in the queue"));
    QCOMPARE(footer.count(), 0);
    // × takes it, and the row's failure with it.
    app.outcomes()->dismiss();
    QVERIFY(markText(app, server.two).isEmpty());
    app.engine()->stop();
}

void TestAppOutcomes::limitsAreOneStrip()
{
    OutcomeServer server;
    App app;
    QVERIFY(startOn(app, server));
    QSignalSpy strips(app.outcomes(), &omaseed::core::ActionOutcomes::currentChanged);
    app.setLimits({server.one}, 1024, 2048, -2, -2);
    QTRY_COMPARE_WITH_TIMEOUT(server.posts.size(), 3, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(strips.count(), 1, 3000);
    QTest::qWait(200);
    QCOMPARE(strips.count(), 1);
    QCOMPARE(current(app, "headline"), QStringLiteral("Arcade Set 01 given new limits"));
    app.engine()->stop();
}

void TestAppOutcomes::aDeleteNamesWhatWentAndMarksNoRow()
{
    OutcomeServer server;
    App app;
    QVERIFY(startOn(app, server));
    app.remove({server.one}, false);
    QTRY_COMPARE_WITH_TIMEOUT(
        current(app, "headline"), QStringLiteral("Arcade Set 01 deleted"), 3000);
    QVERIFY(markText(app, server.one).isEmpty());
    app.engine()->stop();
}

QTEST_MAIN(TestAppOutcomes)
#include "tst_app_outcomes.moc"
