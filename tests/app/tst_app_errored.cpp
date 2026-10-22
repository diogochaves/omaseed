// An errored torrent (docs/DECISIONS.md C20, #107): `p` stops it rather than starting it, and
// omaseed remembers the stop, since qBittorrent ranks the error above the stop and reports
// `state: error` either way. The row then reads "error · stopped"; `p` again, or "retry",
// starts it and forgets the stop; one that leaves the error is forgotten too.

#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;
using omaseed::core::TorrentModel;

namespace {

/// Two torrents the way qBittorrent 5.2 reports them: `errored` stays in `error` through a
/// stop and a start (its disk still refuses it), `healthy` downloads and stops as asked. Every
/// poll is a full update. `stops` and `starts` are the hashes each endpoint was sent.
class ErroredServer : public QTcpServer
{
public:
    QString errored = QStringLiteral("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    QString healthy = QStringLiteral("hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh");
    QString erroredState = QStringLiteral("error");
    QString healthyState = QStringLiteral("downloading");
    QStringList stops;
    QStringList starts;
    int polls = 0;

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
            const QUrlQuery form(QString::fromUtf8(buffer->mid(headerEnd + 4, length)));
            buffer->clear();

            QByteArray body = "Ok.";
            const QStringList hashes =
                form.queryItemValue(QStringLiteral("hashes")).split(QLatin1Char('|'));
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.4";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = "2.15.1";
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                ++polls;
                const auto torrent = [](const QString &name, const QString &state) {
                    return QJsonObject{{QStringLiteral("name"), name},
                                       {QStringLiteral("state"), state}};
                };
                const QJsonObject torrents{{errored, torrent(QStringLiteral("e"), erroredState)},
                                           {healthy, torrent(QStringLiteral("h"), healthyState)}};
                const QJsonObject data{{QStringLiteral("rid"), polls},
                                       {QStringLiteral("full_update"), true},
                                       {QStringLiteral("torrents"), torrents}};
                body = QJsonDocument(data).toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/torrents/stop")) {
                stops << hashes;
                if (hashes.contains(healthy))
                    healthyState = QStringLiteral("stoppedDL");
            } else if (target.startsWith("/api/v2/torrents/start")) {
                starts << hashes;
                if (hashes.contains(healthy))
                    healthyState = QStringLiteral("downloading");
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

bool rowFlag(const App &app, const QString &hash, const char *name)
{
    return app.torrentRow(hash).value(QString::fromLatin1(name)).toBool();
}

QString statusCause(App &app, const QString &hash)
{
    TorrentModel *model = app.torrents();
    return model->data(model->index(model->indexOfHash(hash)), TorrentModel::StatusCauseRole)
        .toString();
}

} // namespace

class TestAppErrored : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pStopsAnErroredTorrentAndRemembersIt();
    void pAgainStartsItAndForgetsTheStop();
    void retryForgetsTheStopAtOnce();
    void theSelectionBarsStopIsRememberedToo();
    void oneThatLeavesTheErrorIsForgotten();
    void aHealthyStopIsNotAnErrorStop();

private:
    /// Starts `app` on `server`, polling every 100 ms, and waits for both torrents.
    bool startOn(App &app, ErroredServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppErrored::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppErrored::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppErrored::startOn(App &app, ErroredServer &server)
{
    if (!server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.torrents()->count() == 2; }, 5000);
}

void TestAppErrored::pStopsAnErroredTorrentAndRemembersIt()
{
    ErroredServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QString hash = server.errored;
    QVERIFY(!rowFlag(app, hash, "stopped"));
    QVERIFY(!rowFlag(app, hash, "errorStopped"));
    QCOMPARE(statusCause(app, hash), QString());

    app.stopStart({hash});
    QTRY_COMPARE_WITH_TIMEOUT(server.stops, QStringList{hash}, 3000);
    QVERIFY(server.starts.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, hash, "errorStopped"), 3000);
    QVERIFY(rowFlag(app, hash, "stopped"));
    QCOMPARE(statusCause(app, hash), QStringLiteral("error · stopped"));

    // The server still says `error`, poll after poll, and that does not undo it.
    const int polls = server.polls;
    QTRY_VERIFY_WITH_TIMEOUT(server.polls >= polls + 3, 3000);
    QVERIFY(rowFlag(app, hash, "errorStopped"));
    app.engine()->stop();
}

void TestAppErrored::pAgainStartsItAndForgetsTheStop()
{
    ErroredServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QString hash = server.errored;
    app.stopStart({hash});
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, hash, "errorStopped"), 3000);

    app.stopStart({hash});
    QTRY_COMPARE_WITH_TIMEOUT(server.starts, QStringList{hash}, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!rowFlag(app, hash, "errorStopped"), 3000);
    QVERIFY(!rowFlag(app, hash, "stopped"));
    QCOMPARE(statusCause(app, hash), QString());
    app.engine()->stop();
}

void TestAppErrored::retryForgetsTheStopAtOnce()
{
    ErroredServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QString hash = server.errored;
    app.stopTorrents({hash});
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, hash, "errorStopped"), 3000);

    app.retryTorrents({hash});
    QVERIFY2(!rowFlag(app, hash, "errorStopped"), "the row still read stopped as retry went out");
    QTRY_COMPARE_WITH_TIMEOUT(server.starts, QStringList{hash}, 3000);
    QTest::qWait(300); // a few polls, still `error`: not remembered again
    QVERIFY(!rowFlag(app, hash, "errorStopped"));
    app.engine()->stop();
}

void TestAppErrored::theSelectionBarsStopIsRememberedToo()
{
    ErroredServer server;
    App app;
    QVERIFY(startOn(app, server));
    // The bar's stop on a selection of both: only the errored one is an error stop.
    app.stopTorrents({server.errored, server.healthy});
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, server.errored, "errorStopped"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, server.healthy, "stopped"), 3000);
    QVERIFY(!rowFlag(app, server.healthy, "errorStopped"));
    // The bar's start forgets it.
    app.startTorrents({server.errored, server.healthy});
    QTRY_VERIFY_WITH_TIMEOUT(!rowFlag(app, server.errored, "errorStopped"), 3000);
    app.engine()->stop();
}

void TestAppErrored::oneThatLeavesTheErrorIsForgotten()
{
    ErroredServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QString hash = server.errored;
    app.stopStart({hash});
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, hash, "errorStopped"), 3000);

    // Started from elsewhere and fixed: it downloads again, and the stop is gone with the error.
    server.erroredState = QStringLiteral("downloading");
    QTRY_COMPARE_WITH_TIMEOUT(app.torrentRow(hash).value(QStringLiteral("stateText")).toString(),
                              QStringLiteral("downloading"),
                              3000);
    QVERIFY(!rowFlag(app, hash, "errorStopped"));
    // In error again later: a new error, not one omaseed stopped.
    server.erroredState = QStringLiteral("error");
    QTRY_COMPARE_WITH_TIMEOUT(app.torrentRow(hash).value(QStringLiteral("stateText")).toString(),
                              QStringLiteral("error"),
                              3000);
    QVERIFY(!rowFlag(app, hash, "errorStopped"));
    QVERIFY(!rowFlag(app, hash, "stopped"));
    app.engine()->stop();
}

void TestAppErrored::aHealthyStopIsNotAnErrorStop()
{
    ErroredServer server;
    App app;
    QVERIFY(startOn(app, server));
    app.stopStart({server.healthy});
    QTRY_VERIFY_WITH_TIMEOUT(rowFlag(app, server.healthy, "stopped"), 3000);
    QVERIFY(!rowFlag(app, server.healthy, "errorStopped"));
    QCOMPARE(statusCause(app, server.healthy), QString());
    app.engine()->stop();
}

QTEST_MAIN(TestAppErrored)
#include "tst_app_errored.moc"
