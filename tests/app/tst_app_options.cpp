// A torrent option from the actions menu or General (docs/12 G4) shows at once (docs/UX-NOTES
// M12): the menu opened right after reads the new value, a refusal puts the old one back, a
// poll the server answered before it took the change does not flip it back, and a server that
// ignored the change ends up showing what it really holds.

#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;

namespace {

/// One torrent whose four options change the way qBittorrent's endpoints change them, and a
/// full update with the current state on every poll (every field, the harshest case for a
/// stale value). `refuse` answers the option endpoints with 409; `ignore` answers 200 and
/// changes nothing. `holdNextPoll` keeps the next maindata answer, made from the state as the
/// request arrived, until `releasePoll()`: a poll in flight while the change goes out.
class OptionServer : public QTcpServer
{
public:
    QString hash = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    QJsonObject options{
        {QStringLiteral("seq_dl"), false},
        {QStringLiteral("f_l_piece_prio"), false},
        {QStringLiteral("super_seeding"), false},
        {QStringLiteral("auto_tmm"), false},
    };
    bool refuse = false;
    bool ignore = false;
    bool holdNextPoll = false;
    int optionRequests = 0;
    int polls = 0;
    QPointer<QTcpSocket> heldSocket;
    QByteArray heldResponse;

    void releasePoll()
    {
        if (heldSocket) {
            heldSocket->write(heldResponse);
            heldSocket->disconnectFromHost();
        }
        heldSocket.clear();
    }

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
            const QByteArray requestBody = buffer->mid(headerEnd + 4, length);
            buffer->clear();

            QByteArray status = "200 OK";
            QByteArray body = "Ok.";
            bool hold = false;
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.3";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = "2.15.1";
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                ++polls;
                QJsonObject torrent = options;
                torrent.insert(QStringLiteral("name"), QStringLiteral("t"));
                torrent.insert(QStringLiteral("state"), QStringLiteral("downloading"));
                const QJsonObject data{{QStringLiteral("rid"), polls},
                                       {QStringLiteral("full_update"), true},
                                       {QStringLiteral("torrents"), QJsonObject{{hash, torrent}}}};
                body = QJsonDocument(data).toJson(QJsonDocument::Compact);
                hold = holdNextPoll;
                holdNextPoll = false;
            } else if (target.startsWith("/api/v2/torrents/")) {
                ++optionRequests;
                if (refuse) {
                    status = "409 Conflict";
                    body = "Conflict";
                } else if (!ignore) {
                    const QUrlQuery form(QString::fromUtf8(requestBody));
                    const auto flip = [this](const QString &key) {
                        options.insert(key, !options.value(key).toBool());
                    };
                    const auto set = [this, &form](const QString &key, const QString &field) {
                        options.insert(key, form.queryItemValue(field) == QStringLiteral("true"));
                    };
                    if (target.startsWith("/api/v2/torrents/toggleSequentialDownload"))
                        flip(QStringLiteral("seq_dl"));
                    else if (target.startsWith("/api/v2/torrents/toggleFirstLastPiecePrio"))
                        flip(QStringLiteral("f_l_piece_prio"));
                    else if (target.startsWith("/api/v2/torrents/setSuperSeeding"))
                        set(QStringLiteral("super_seeding"), QStringLiteral("value"));
                    else if (target.startsWith("/api/v2/torrents/setAutoManagement"))
                        set(QStringLiteral("auto_tmm"), QStringLiteral("enable"));
                }
            }
            const QByteArray response = "HTTP/1.1 " + status +
                                        "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                        "\r\nConnection: close\r\n\r\n" + body;
            if (hold) {
                heldSocket = socket;
                heldResponse = response;
                return;
            }
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
};

bool rowFlag(const App &app, const QString &hash, const char *name)
{
    return app.torrentRow(hash).value(QString::fromLatin1(name)).toBool();
}

} // namespace

class TestAppOptions : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void everyOptionShowsBeforeTheServerAnswers();
    void aRefusedOptionGoesBack();
    void aPollInFlightDoesNotFlipItBack();
    void anIgnoredOptionEndsUpShowingTheServersValue();

private:
    /// Starts `app` on `server`, polling every 100 ms, and waits for the torrent.
    bool startOn(App &app, OptionServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppOptions::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppOptions::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

bool TestAppOptions::startOn(App &app, OptionServer &server)
{
    if (!server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.torrents()->count() == 1; }, 5000);
}

void TestAppOptions::everyOptionShowsBeforeTheServerAnswers()
{
    OptionServer server;
    App app;
    QVERIFY(startOn(app, server));
    const QString hash = server.hash;

    // Synchronously, before any answer: what the actions menu reads when it opens again.
    app.setSequentialDownload({hash}, true);
    app.setFirstLastPiecePrio({hash}, true);
    app.setSuperSeeding({hash}, true);
    app.setAutoManagement({hash}, true);
    QVERIFY(rowFlag(app, hash, "seqDl"));
    QVERIFY(rowFlag(app, hash, "firstLastPiecePrio"));
    QVERIFY(rowFlag(app, hash, "superSeeding"));
    QVERIFY(rowFlag(app, hash, "autoTmm"));

    // The server took them all; once settled, the model still agrees with it.
    QTRY_COMPARE_WITH_TIMEOUT(app.engine()->pendingTorrentValueCount(), 0, 5000);
    QCOMPARE(server.optionRequests, 4);
    QVERIFY(rowFlag(app, hash, "seqDl"));
    QVERIFY(rowFlag(app, hash, "firstLastPiecePrio"));
    QVERIFY(rowFlag(app, hash, "superSeeding"));
    QVERIFY(rowFlag(app, hash, "autoTmm"));

    // A second press of an option already on sends nothing.
    app.setSuperSeeding({hash}, true);
    QCOMPARE(server.optionRequests, 4);
    app.engine()->stop();
}

void TestAppOptions::aRefusedOptionGoesBack()
{
    OptionServer server;
    server.refuse = true;
    App app;
    QVERIFY(startOn(app, server));

    app.setSequentialDownload({server.hash}, true);
    QVERIFY(rowFlag(app, server.hash, "seqDl"));
    QTRY_VERIFY_WITH_TIMEOUT(!rowFlag(app, server.hash, "seqDl"), 3000);
    QCOMPARE(app.engine()->pendingTorrentValueCount(), 0);

    QTest::qWait(300); // a few polls: still off
    QVERIFY(!rowFlag(app, server.hash, "seqDl"));
    app.engine()->stop();
}

void TestAppOptions::aPollInFlightDoesNotFlipItBack()
{
    OptionServer server;
    App app;
    QVERIFY(startOn(app, server));

    // A poll reaches the server and is held, carrying `seq_dl: false`...
    server.holdNextPoll = true;
    QTRY_VERIFY_WITH_TIMEOUT(server.heldSocket, 3000);

    // ...then the change goes out, is taken and answered...
    app.setSequentialDownload({server.hash}, true);
    QTRY_VERIFY_WITH_TIMEOUT(server.options.value(QStringLiteral("seq_dl")).toBool(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(server.optionRequests, 1, 3000);
    QTest::qWait(50); // the answer is read

    // ...and only then does the old poll land: the model must not flip back.
    QSignalSpy synced(app.engine(), &omaseed::core::SyncEngine::synced);
    server.releasePoll();
    QTRY_VERIFY_WITH_TIMEOUT(synced.count() >= 1, 3000);
    QVERIFY(rowFlag(app, server.hash, "seqDl"));

    // The next poll, sent after the answer, settles it on the server's value.
    QTRY_COMPARE_WITH_TIMEOUT(app.engine()->pendingTorrentValueCount(), 0, 3000);
    QVERIFY(rowFlag(app, server.hash, "seqDl"));
    app.engine()->stop();
}

void TestAppOptions::anIgnoredOptionEndsUpShowingTheServersValue()
{
    OptionServer server;
    server.ignore = true;
    App app;
    QVERIFY(startOn(app, server));

    app.setSuperSeeding({server.hash}, true);
    QVERIFY(rowFlag(app, server.hash, "superSeeding"));
    QTRY_COMPARE_WITH_TIMEOUT(app.engine()->pendingTorrentValueCount(), 0, 3000);
    QVERIFY(!rowFlag(app, server.hash, "superSeeding"));
    app.engine()->stop();
}

QTEST_MAIN(TestAppOptions)
#include "tst_app_options.moc"
