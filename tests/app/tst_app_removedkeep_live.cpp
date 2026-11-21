// The removed folder against the real playgrounds (#295, DECISIONS C50 GT6): on each of
// qBittorrent (:8081), Transmission (:8085) and Deluge (:8086), a one-file torrent of the
// test's own, started at OLD, is kept through App::keepRemoved (the engine's own exporter, that
// one torrent alone, whatever else the playground holds), removed from the server without its
// data, and added back through App::addBackRemoved: on the server again, stopped, at OLD, its
// file untouched, the kept entry gone. The torrent is removed again (without data) and its
// folders deleted with the test. Each engine QSKIPs when nothing listens on its port (Deluge
// also without DELUGE_TEST_PASS: set -a; . ~/.config/omaseed/dev.env; set +a). Never :8080.

#include "FakeHttp.hpp"
#include "LiveServer.hpp"

#include "core/GlobalState.hpp"
#include "core/RemovedKeep.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/qbt/QbtEngine.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "ui/app/App.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using omaseed::app::App;
using namespace omaseed::core;
using fakehttp::await;

namespace {

constexpr int kPiece = 16384;
constexpr int kSize = 3 * kPiece + 1000;

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

QByteArray bstr(const QByteArray &s)
{
    return QByteArray::number(s.size()) + ':' + s;
}

QByteArray bint(qint64 n)
{
    return 'i' + QByteArray::number(n) + 'e';
}

/// A single-file v1 .torrent of `bytes` named `name`, no tracker; its info hash in `hash`.
QByteArray makeTorrent(const QString &name, const QByteArray &bytes, QString *hash)
{
    QByteArray pieces;
    for (qsizetype at = 0; at < bytes.size(); at += kPiece)
        pieces += QCryptographicHash::hash(bytes.mid(at, kPiece), QCryptographicHash::Sha1);
    const QByteArray info = "d" + bstr("length") + bint(bytes.size()) + bstr("name") +
                            bstr(name.toUtf8()) + bstr("piece length") + bint(kPiece) +
                            bstr("pieces") + bstr(pieces) + "e";
    *hash = QString::fromLatin1(QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex());
    return "d" + bstr("info") + info + "e";
}

QString delugePassword()
{
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("DELUGE_TEST_PASS"));
}

} // namespace

class TestAppRemovedKeepLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void qbittorrent();
    void transmission();
    void deluge();

private:
    template<class T>
    T ok(Reply<T> *reply)
    {
        const Result<T> result = await(this, reply, 30000);
        [&]() { QVERIFY2(result.isValid(), qPrintable(result.error().toString())); }();
        return result.isValid() ? result.value() : T{};
    }
    /// The torrent's row, polled until `done` holds; nullopt while the server lacks it.
    std::optional<Torrent> until(EngineClient &engine,
                                 const QString &hash,
                                 const std::function<bool(const std::optional<Torrent> &)> &done);
    void flow(EngineClient &engine, const QString &url, const QString &kind);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppRemovedKeepLive::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("XDG_CONFIG_HOME", m_home->filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", m_home->filePath(QStringLiteral("data")).toUtf8());
}

void TestAppRemovedKeepLive::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_ENGINE");
    qunsetenv("OMASEED_PASSWORD");
    m_home.reset();
}

std::optional<Torrent>
TestAppRemovedKeepLive::until(EngineClient &engine,
                              const QString &hash,
                              const std::function<bool(const std::optional<Torrent> &)> &done)
{
    const auto read = [&]() -> std::optional<Torrent> {
        const Result<QList<TorrentPatch>> rows = await(this, engine.torrents({hash}), 30000);
        if (!rows.isValid() || rows.value().isEmpty() || rows.value().first().hash != hash)
            return std::nullopt;
        return rows.value().first().values;
    };
    QElapsedTimer clock;
    clock.start();
    std::optional<Torrent> seen = read();
    while (!done(seen) && clock.elapsed() < 30000) {
        QTest::qWait(250);
        seen = read();
    }
    return seen;
}

void TestAppRemovedKeepLive::flow(EngineClient &engine, const QString &url, const QString &kind)
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString name =
        QStringLiteral("omaseed-removed-%1.bin").arg(QDateTime::currentMSecsSinceEpoch());
    const QString old = root.filePath(QStringLiteral("old"));
    QVERIFY(QDir().mkpath(old));
    QByteArray bytes(kSize, Qt::Uninitialized);
    for (char &c : bytes)
        c = char(QRandomGenerator::global()->bounded(256));
    {
        QFile file(old + QLatin1Char('/') + name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    const QDateTime written = QFileInfo(old + QLatin1Char('/') + name).lastModified();
    QString hash;
    const QByteArray torrent = makeTorrent(name, bytes, &hash);

    // Started, so the add back has a reason to say stopped.
    AddOptions options;
    options.savePath = old;
    options.stopped = false;
    ok(engine.addFile(name + QStringLiteral(".torrent"), torrent, options));
    const auto removeAtEnd =
        qScopeGuard([&]() { await(this, engine.remove({hash}, false), 30000); });
    const std::optional<Torrent> added =
        until(engine, hash, [](const auto &t) { return t && !t->savePath.isEmpty(); });
    QVERIFY(added.has_value());
    QCOMPARE(added->savePath, old);

    // App on the playground, as a person runs it.
    qputenv("OMASEED_SERVER_URL", url.toUtf8());
    qputenv("OMASEED_SERVER_ENGINE", kind.toUtf8());
    if (kind == QLatin1String("deluge"))
        qputenv("OMASEED_PASSWORD", delugePassword().toUtf8());
    App app;
    using namespace std::chrono_literals;
    app.engine()->setIntervals(200ms, 200ms, 200ms, 200ms);
    app.start();
    QVERIFY(QTest::qWaitFor(
        [&]() {
            return app.engine()->globalState()->connected() &&
                   app.torrents()->indexOfHash(hash) >= 0;
        },
        20000));

    // Kept: this one torrent alone, whatever else the playground has.
    QString id = QStringLiteral("never");
    QString error;
    app.keepRemoved(hash, [&](const QString &kept, const QString &why) {
        id = kept;
        error = why;
    });
    QVERIFY(QTest::qWaitFor([&id] { return id != QLatin1String("never"); }, 30000));
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    const std::optional<RemovedKeep::Entry> kept = RemovedKeep::entry(id);
    QVERIFY(kept.has_value());
    QCOMPARE(kept->hash, hash);
    QCOMPARE(kept->name, name);
    QCOMPARE(kept->savePath, old);
    QVERIFY(!kept->link);
    const std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(kept->path);
    QVERIFY(contents.has_value());
    QCOMPARE(contents->manifest.count, 1);
    QCOMPARE(contents->records.size(), 1);
    QVERIFY(contents->torrents.contains(hash));
    QVERIFY(contents->records.at(0).started());

    // Removed without its data (S10's step), seen by the app.
    ok(engine.remove({hash}, false));
    QVERIFY(!until(engine, hash, [](const auto &t) { return !t.has_value(); }).has_value());
    QVERIFY(QTest::qWaitFor([&]() { return app.torrents()->indexOfHash(hash) < 0; }, 20000));
    QVERIFY(QFileInfo::exists(old + QLatin1Char('/') + name));

    // Added back: stopped, at OLD, unchecked; the entry forgotten.
    QString back = QStringLiteral("never");
    app.addBackRemoved(id, [&back](const QString &why) { back = why; });
    QVERIFY(QTest::qWaitFor([&back] { return back != QLatin1String("never"); }, 60000));
    QVERIFY2(back.isEmpty(), qPrintable(back));
    QVERIFY(!RemovedKeep::entry(id).has_value());
    const std::optional<Torrent> again = until(engine, hash, [&](const auto &t) {
        return t && t->savePath == old && t->inGroup(StatusGroup::Stopped);
    });
    QVERIFY(again.has_value());
    QCOMPARE(again->savePath, old);
    QVERIFY2(again->inGroup(StatusGroup::Stopped), qPrintable(again->stateText()));
    // A second look a moment on: nothing started it.
    QTest::qWait(1500);
    const std::optional<Torrent> later =
        until(engine, hash, [](const auto &t) { return t.has_value(); });
    QVERIFY(later && later->inGroup(StatusGroup::Stopped));
    QFile file(old + QLatin1Char('/') + name);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
    QCOMPARE(QFileInfo(file).lastModified(), written);
    app.engine()->stop();
}

void TestAppRemovedKeepLive::qbittorrent()
{
    if (!listening(8081))
        QSKIP("no qBittorrent on :8081: scripts/dev/test-server.sh start");
    QbtEngine engine;
    engine.setBaseUrl(LiveServer::playground());
    ok(engine.probe());
    flow(engine, LiveServer::playground().toString(), QStringLiteral("qbittorrent"));
}

void TestAppRemovedKeepLive::transmission()
{
    if (!listening(8085))
        QSKIP("no Transmission on :8085: scripts/dev/test-server.sh start --transmission");
    const QUrl url =
        LiveServer::fromEnv("OMASEED_TRANSMISSION_URL", QStringLiteral("http://127.0.0.1:8085"));
    TransmissionEngine engine;
    engine.setBaseUrl(url);
    engine.setTimeout(std::chrono::seconds(15));
    ok(engine.probe());
    flow(engine, url.toString(), QStringLiteral("transmission"));
}

void TestAppRemovedKeepLive::deluge()
{
    if (!listening(8086))
        QSKIP("no deluge-web on :8086: scripts/dev/test-server.sh start --deluge");
    if (delugePassword().isEmpty())
        QSKIP("no DELUGE_TEST_PASS: set -a; . ~/.config/omaseed/dev.env; set +a");
    const QUrl url =
        LiveServer::fromEnv("OMASEED_DELUGE_URL", QStringLiteral("http://127.0.0.1:8086"));
    DelugeEngine engine;
    engine.setBaseUrl(url);
    engine.setCredentials(QString(), delugePassword());
    engine.setTimeout(std::chrono::seconds(15));
    ok(engine.probe());
    flow(engine, url.toString(), QStringLiteral("deluge"));
}

QTEST_MAIN(TestAppRemovedKeepLive)
#include "tst_app_removedkeep_live.moc"
