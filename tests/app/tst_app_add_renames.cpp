// Renaming files and folders in the add sheet (docs/UX-NOTES M18): `torrents/add` takes no
// per-file names, so App adds the torrent stopped, reads `torrents/files` to learn the root
// folder the server puts in front of the paths, sends each rename in the order it was made,
// then the file priorities, then starts it. A fake server records what arrives: the order,
// the paths with their root, the wait while the file list is still empty, and a refused
// rename, which leaves the torrent stopped and says so without losing the add.

#include "core/AddFilesModel.hpp"
#include "core/TorrentFilesModel.hpp"
#include "ui/app/App.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <memory>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
using omaseed::app::App;

namespace {

/// The eight-file tree of the add sheet's tests, and its id on the server.
const char *kTreeFile = "open_movies_v1.torrent";
const char *kTreeHash = "429d28a2cdae51f937735430e0a88cec3ee76cbc";
/// The root folder qBittorrent puts in front of the metainfo's paths ("Original" layout).
const char *kRoot = "Blender Open Movies/";

/// Answers what App asks while it adds: the version probe, an empty library, the add, the
/// torrent's files (empty for the first `emptyFileLists` reads, as a server still loading
/// the metadata answers), the renames, the priorities and the start. Every call after the
/// add is logged as "endpoint form" in `calls`.
class AddServer : public QTcpServer
{
public:
    QStringList paths;
    int emptyFileLists = 0;
    bool refuseFolderRename = false;
    QStringList calls;

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

            const QString endpoint =
                QString::fromUtf8(target.mid(QByteArrayLiteral("/api/v2/").size()))
                    .section(QLatin1Char('?'), 0, 0);
            QByteArray status = "200 OK";
            QByteArray body = "Ok.";
            if (endpoint == QLatin1String("app/version")) {
                body = "v5.2.3";
            } else if (endpoint == QLatin1String("app/webapiVersion")) {
                body = "2.15.1";
            } else if (endpoint == QLatin1String("sync/maindata")) {
                body = R"({"rid":1,"full_update":true,"torrents":{}})";
            } else if (endpoint == QLatin1String("torrents/add")) {
                calls.append(endpoint);
            } else if (endpoint == QLatin1String("torrents/files")) {
                calls.append(endpoint);
                QJsonArray files;
                if (emptyFileLists > 0) {
                    --emptyFileLists;
                } else {
                    for (int index = 0; index < paths.size(); ++index)
                        files.append(QJsonObject{{QStringLiteral("index"), index},
                                                 {QStringLiteral("name"), paths.at(index)}});
                }
                body = QJsonDocument(files).toJson(QJsonDocument::Compact);
            } else if (endpoint.startsWith(QLatin1String("torrents/"))) {
                const QUrlQuery form(QString::fromUtf8(requestBody));
                const QString old =
                    form.queryItemValue(QStringLiteral("oldPath"), QUrl::FullyDecoded);
                const QString next =
                    form.queryItemValue(QStringLiteral("newPath"), QUrl::FullyDecoded);
                if (endpoint == QLatin1String("torrents/renameFolder") ||
                    endpoint == QLatin1String("torrents/renameFile")) {
                    calls.append(endpoint + QLatin1Char(' ') + old + QStringLiteral(" -> ") + next);
                    if (endpoint == QLatin1String("torrents/renameFolder") && refuseFolderRename) {
                        status = "409 Conflict";
                        body = "Conflict";
                    }
                } else if (endpoint == QLatin1String("torrents/filePrio")) {
                    calls.append(endpoint + QLatin1Char(' ') +
                                 form.queryItemValue(QStringLiteral("id")) + QLatin1Char('=') +
                                 form.queryItemValue(QStringLiteral("priority")));
                } else {
                    calls.append(endpoint);
                }
            }
            socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

/// The row of `model` whose `name` is `name`, or -1.
int rowNamed(const AddFilesModel &model, const QString &name)
{
    for (int row = 0; row < model.count(); ++row) {
        if (model.data(model.index(row, 0), TorrentFilesModel::NameRole).toString() == name)
            return row;
    }
    return -1;
}

} // namespace

class TestAppAddRenames : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void renamesGoOutAfterTheAddThenPrioritiesThenStart();
    void aRefusedRenameLeavesItStoppedAndSaysSo();
    void noRenamesNoFileList();

private:
    /// Starts `app` on `server` and parses the fixture into a request with the picker loaded.
    QVariantMap startAndParse(App &app, AddServer &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppAddRenames::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppAddRenames::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

QVariantMap TestAppAddRenames::startAndParse(App &app, AddServer &server)
{
    if (!server.listen(QHostAddress::LocalHost))
        return {};
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    app.start();

    QSignalSpy parsed(&app, &App::addableParsed);
    app.parseAddable(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kTreeFile));
    if (!parsed.wait(10000))
        return {};
    QVariantMap request = parsed.first().first().toMap();
    app.loadAddFiles(request.value(QStringLiteral("token")).toString());

    // What the server will list: the metainfo's paths under the torrent's root folder.
    const QStringList original{
        QStringLiteral("Big Buck Bunny/bbb_sunflower_2160p_60fps.mp4"),
        QStringLiteral("Big Buck Bunny/subs/bbb.en.srt"),
        QStringLiteral("Big Buck Bunny/subs/bbb.de.srt"),
        QStringLiteral("Sintel/sintel_4k.mkv"),
        QStringLiteral("Sintel/subs/sintel.en.srt"),
        QStringLiteral("Tears of Steel/tears_of_steel_4k.mov"),
        QStringLiteral("Cosmos Laundromat/cosmos_laundromat_2k.mp4"),
        QStringLiteral("README.txt"),
    };
    for (const QString &path : original)
        server.paths.append(QString::fromLatin1(kRoot) + path);
    return request;
}

void TestAppAddRenames::renamesGoOutAfterTheAddThenPrioritiesThenStart()
{
    AddServer server;
    server.emptyFileLists = 2; // the metadata is still loading: App waits for it
    App app;
    QVariantMap request = startAndParse(app, server);
    QVERIFY(request.value(QStringLiteral("valid")).toBool());

    AddFilesModel *files = app.addFiles();
    QVERIFY(files->rename(rowNamed(*files, QStringLiteral("Sintel/")),
                          QStringLiteral("Sintel (2010)")));
    QVERIFY(files->rename(rowNamed(*files, QStringLiteral("README.txt")),
                          QStringLiteral("read me.txt")));
    files->setPriority(rowNamed(*files, QStringLiteral("read me.txt")), 7);

    request.insert(QStringLiteral("filePriorities"), files->priorityList());
    request.insert(QStringLiteral("fileRenames"), files->renameList());
    request.insert(QStringLiteral("renameAnchor"), files->renameAnchor());
    request.insert(QStringLiteral("stopped"), false);

    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(10000));
    QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
    QCOMPARE(finished.first().at(2).toString(), QString::fromLatin1(kTreeHash));

    const QString root = QString::fromLatin1(kRoot);
    const QStringList expected{
        QStringLiteral("torrents/add"),
        QStringLiteral("torrents/files"),
        QStringLiteral("torrents/files"),
        QStringLiteral("torrents/files"),
        QStringLiteral("torrents/renameFolder ") + root + QStringLiteral("Sintel -> ") + root +
            QStringLiteral("Sintel (2010)"),
        QStringLiteral("torrents/renameFile ") + root + QStringLiteral("README.txt -> ") + root +
            QStringLiteral("read me.txt"),
        QStringLiteral("torrents/filePrio 7=7"),
        QStringLiteral("torrents/start"),
    };
    QCOMPARE(server.calls, expected);
}

void TestAppAddRenames::aRefusedRenameLeavesItStoppedAndSaysSo()
{
    AddServer server;
    server.refuseFolderRename = true;
    App app;
    QVariantMap request = startAndParse(app, server);
    QVERIFY(request.value(QStringLiteral("valid")).toBool());

    AddFilesModel *files = app.addFiles();
    QVERIFY(files->rename(rowNamed(*files, QStringLiteral("Sintel/")),
                          QStringLiteral("Sintel (2010)")));
    request.insert(QStringLiteral("fileRenames"), files->renameList());
    request.insert(QStringLiteral("renameAnchor"), files->renameAnchor());
    request.insert(QStringLiteral("stopped"), false);

    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.first().at(0).toBool(), false);
    QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("names")));
    // The add itself happened: the id is there for the table to show it.
    QCOMPARE(finished.first().at(2).toString(), QString::fromLatin1(kTreeHash));
    QVERIFY(!server.calls.contains(QStringLiteral("torrents/start")));
}

void TestAppAddRenames::noRenamesNoFileList()
{
    AddServer server;
    App app;
    QVariantMap request = startAndParse(app, server);
    QVERIFY(request.value(QStringLiteral("valid")).toBool());
    request.insert(QStringLiteral("fileRenames"), QVariantList());
    request.insert(QStringLiteral("stopped"), false);

    QSignalSpy finished(&app, &App::addFinished);
    app.addTorrent(request);
    QVERIFY(finished.wait(10000));
    QVERIFY(finished.first().at(0).toBool());
    QCOMPARE(server.calls, QStringList{QStringLiteral("torrents/add")});
}

QTEST_MAIN(TestAppAddRenames)
#include "tst_app_add_renames.moc"
