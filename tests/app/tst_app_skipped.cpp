// #180 (board 44 §7, DECISIONS C17 D1–D2): the Files tab's reading of what the skipped files
// hold on disk follows the server's file list. A skip or a tick made in the app is read once a
// torrents/files answer carries it, however many polls that takes: one in flight at the key
// still has the old priorities, and a tick waits 3 s before it is sent (C17 D5). A change made
// elsewhere (qBittorrent's own Web UI) is read the same way, and a poll that changes nothing
// reads nothing.
//
// Against a small fake server whose files live in a temporary folder on this machine, so the
// app sees its disk (FreeSpaceProbe's calibration: the same save path, the same free space).
// Its torrents/files answer comes a little after torrents/properties', as qBittorrent's
// bigger answer tends to.

#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QStorageInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;

namespace {

constexpr qint64 kBig = 1024 * 1024; ///< big.bin: most of the torrent, past D2's 5 %
constexpr qint64 kSmall = 64 * 1024; ///< small.bin
constexpr int kFilesDelayMs = 60;    ///< torrents/files answers this long after the ask

/// One finished, seeding torrent, "Pack" with big.bin and small.bin, saved in `dir`.
/// `priorities` is what the server has; torrents/filePrio sets it.
class SkippedServer : public QTcpServer
{
public:
    QString hash = QStringLiteral("5eed5eed5eed5eed5eed5eed5eed5eed5eed5eed");
    QString dir;
    QList<int> priorities{1, 1};
    int polls = 0;
    int fileReads = 0;
    QStringList prioritySends; ///< "id=priority" per torrents/filePrio

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
            answer(socket, target, form);
        });
    }

private:
    static void reply(QTcpSocket *socket, const QByteArray &body)
    {
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    static QByteArray json(const QJsonObject &object)
    {
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    QJsonArray files() const
    {
        const auto file = [this](int index, const QString &name, qint64 size) {
            return QJsonObject{{QStringLiteral("index"), index},
                               {QStringLiteral("name"), name},
                               {QStringLiteral("size"), size},
                               {QStringLiteral("progress"), 1.0},
                               {QStringLiteral("priority"), priorities.at(index)},
                               {QStringLiteral("availability"), 1.0}};
        };
        return {file(0, QStringLiteral("Pack/big.bin"), kBig),
                file(1, QStringLiteral("Pack/small.bin"), kSmall)};
    }

    void answer(QTcpSocket *socket, const QByteArray &target, const QUrlQuery &form)
    {
        const qint64 free = QStorageInfo(dir).bytesAvailable();
        if (target.startsWith("/api/v2/app/version")) {
            reply(socket, "v5.2.4");
        } else if (target.startsWith("/api/v2/app/webapiVersion")) {
            reply(socket, "2.15.1");
        } else if (target.startsWith("/api/v2/app/preferences")) {
            reply(socket, json({{QStringLiteral("save_path"), dir}}));
        } else if (target.startsWith("/api/v2/sync/maindata")) {
            ++polls;
            const QJsonObject torrent{
                {QStringLiteral("name"), QStringLiteral("Pack")},
                {QStringLiteral("state"), QStringLiteral("uploading")},
                {QStringLiteral("progress"), 1.0},
                {QStringLiteral("size"), kBig + kSmall},
                {QStringLiteral("total_size"), kBig + kSmall},
                {QStringLiteral("save_path"), dir},
                {QStringLiteral("content_path"), dir + QStringLiteral("/Pack")}};
            reply(socket,
                  json({{QStringLiteral("rid"), polls},
                        {QStringLiteral("full_update"), true},
                        {QStringLiteral("torrents"), QJsonObject{{hash, torrent}}},
                        {QStringLiteral("server_state"),
                         QJsonObject{{QStringLiteral("free_space_on_disk"), free}}}}));
        } else if (target.startsWith("/api/v2/torrents/properties")) {
            reply(socket,
                  json({{QStringLiteral("save_path"), dir},
                        {QStringLiteral("total_size"), kBig + kSmall}}));
        } else if (target.startsWith("/api/v2/torrents/files")) {
            // Answered a little after the properties, with what the server has by then.
            ++fileReads;
            QPointer<QTcpSocket> later(socket);
            QTimer::singleShot(kFilesDelayMs, this, [this, later]() {
                if (!later.isNull())
                    reply(later, QJsonDocument(files()).toJson(QJsonDocument::Compact));
            });
        } else if (target.startsWith("/api/v2/torrents/filePrio")) {
            const int priority = form.queryItemValue(QStringLiteral("priority")).toInt();
            for (const QString &id :
                 form.queryItemValue(QStringLiteral("id")).split(QLatin1Char('|'))) {
                const int index = id.toInt();
                if (index >= 0 && index < priorities.size())
                    priorities[index] = priority;
                prioritySends << QStringLiteral("%1=%2").arg(index).arg(priority);
            }
            reply(socket, "Ok.");
        } else if (target.startsWith("/api/v2/sync/torrentPeers")) {
            reply(socket, "{}");
        } else if (target.startsWith("/api/v2/torrents/")) {
            reply(socket, "[]"); // trackers, web seeds, piece states
        } else {
            reply(socket, "Ok.");
        }
    }
};

bool band(const App &app)
{
    return app.skippedOnDiskMap().value(QStringLiteral("band")).toBool();
}

int holding(const App &app)
{
    return app.skippedOnDiskMap().value(QStringLiteral("holding")).toInt();
}

bool writeFile(const QString &path, qint64 size)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray chunk(64 * 1024, 'x');
    for (qint64 written = 0; written < size; written += chunk.size())
        file.write(chunk.left(int(qMin<qint64>(chunk.size(), size - written))));
    return file.flush();
}

} // namespace

class TestAppSkipped : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aSkipInTheAppShowsTheBand();
    void aTickBackHidesTheBandOnceTheHoldIsSent();
    void aChangeFromElsewhereIsRead();
    void aPollThatChangesNothingReadsNothing();

private:
    /// Starts `app` on `server`, the torrent on show with its Files tab open, and waits for the
    /// first reading.
    bool openFilesTab(App &app, SkippedServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QTemporaryDir> m_data;
};

void TestAppSkipped::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
    m_data = std::make_unique<QTemporaryDir>();
    QVERIFY(m_data->isValid());
    QVERIFY(QDir(m_data->path()).mkpath(QStringLiteral("Pack")));
    QVERIFY(writeFile(m_data->filePath(QStringLiteral("Pack/big.bin")), kBig));
    QVERIFY(writeFile(m_data->filePath(QStringLiteral("Pack/small.bin")), kSmall));
}

void TestAppSkipped::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_data.reset();
    m_home.reset();
}

bool TestAppSkipped::openFilesTab(App &app, SkippedServer &server)
{
    server.dir = m_data->path();
    if (!server.isListening() && !server.listen(QHostAddress::LocalHost))
        return false;
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.details()->setPollInterval(150);
    app.setSelectionHoldWindow(400);
    app.start();
    if (!QTest::qWaitFor([&]() { return app.torrents()->indexOfHash(server.hash) >= 0; }, 5000))
        return false;
    app.selectTorrent(server.hash);
    app.details()->setVisible(true);
    if (!QTest::qWaitFor([&]() { return app.details()->files()->fileCount() == 2; }, 5000))
        return false;
    app.setFilesTabActive(true);
    // The first reading: the app sees the disk (the band's `seen`), whatever it found.
    return QTest::qWaitFor(
        [&]() {
            const QVariantMap reading = app.skippedOnDiskMap();
            return reading.value(QStringLiteral("hash")).toString() == server.hash &&
                   reading.value(QStringLiteral("seen")).toBool();
        },
        5000);
}

void TestAppSkipped::aSkipInTheAppShowsTheBand()
{
    SkippedServer server;
    App app;
    QVERIFY(openFilesTab(app, server));
    QVERIFY(!band(app));

    // `x` on big.bin: the row reads skip at once, the server follows, and so does the reading.
    app.toggleFilesWanted(server.hash, {0});
    QCOMPARE(app.details()->files()->filePriority(0), 0);
    QTRY_COMPARE_WITH_TIMEOUT(server.priorities, (QList<int>{0, 1}), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(band(app), 3000);
    QCOMPARE(holding(app), 1);
    app.engine()->stop();
}

void TestAppSkipped::aTickBackHidesTheBandOnceTheHoldIsSent()
{
    SkippedServer server;
    server.priorities = {0, 1};
    App app;
    QVERIFY(openFilesTab(app, server));
    QTRY_VERIFY_WITH_TIMEOUT(band(app), 3000);

    // `x` again: the whole torrent wanted, so it waits (C17 D5); the server still has it
    // skipped, and the band still says so, until the hold sends.
    app.toggleFilesWanted(server.hash, {0});
    QCOMPARE(app.details()->files()->filePriority(0), 1);
    QVERIFY(server.prioritySends.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(server.priorities, (QList<int>{1, 1}), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!band(app), 3000);
    QCOMPARE(holding(app), 0);
    app.engine()->stop();
}

void TestAppSkipped::aChangeFromElsewhereIsRead()
{
    SkippedServer server;
    App app;
    QVERIFY(openFilesTab(app, server));
    QVERIFY(!band(app));

    // qBittorrent's own Web UI skips big.bin, then wants it again: omaseed sent nothing.
    server.priorities = {0, 1};
    QTRY_VERIFY_WITH_TIMEOUT(band(app), 3000);
    server.priorities = {1, 1};
    QTRY_VERIFY_WITH_TIMEOUT(!band(app), 3000);
    QVERIFY(server.prioritySends.isEmpty());
    app.engine()->stop();
}

void TestAppSkipped::aPollThatChangesNothingReadsNothing()
{
    SkippedServer server;
    server.priorities = {0, 1};
    App app;
    QVERIFY(openFilesTab(app, server));
    QTRY_VERIFY_WITH_TIMEOUT(band(app), 3000);
    // Let a reading still in flight land, then count.
    const int settled = server.fileReads;
    QTRY_VERIFY_WITH_TIMEOUT(server.fileReads >= settled + 2, 3000);

    QSignalSpy readings(&app, &App::skippedOnDiskChanged);
    const int before = server.fileReads;
    QTRY_VERIFY_WITH_TIMEOUT(server.fileReads >= before + 5, 5000);
    QCOMPARE(readings.count(), 0);
    QVERIFY(band(app));
    app.engine()->stop();
}

QTEST_MAIN(TestAppSkipped)
#include "tst_app_skipped.moc"
