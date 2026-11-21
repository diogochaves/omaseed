// EngineClient::pointAt against the real playgrounds (#294, DECISIONS C50 GT2): the same flow
// as scripts/dev/research/294-locate, through omaseed's engines. On each of qBittorrent (:8081),
// Transmission (:8085) and Deluge (:8086): a two-file torrent of the test's own, made here and
// complete at OLD, has its files moved by hand to NEW (missing at OLD, the engine told by a
// recheck); pointAt(NEW) leaves every file at NEW as it was (inode, mtime, bytes) and brings
// nothing back at OLD, and a recheck then finds it whole. Then the files are copied, one byte
// changed, to THIRD while NEW still has them: pointAt(THIRD) replaces or deletes nothing on
// either side. The torrent is removed without its data and its folders deleted with the test.
// Each engine QSKIPs when nothing listens on its port (Deluge also without DELUGE_TEST_PASS:
// set -a; . ~/.config/omaseed/dev.env; set +a). Never :8080.

#include "FakeHttp.hpp"
#include "LiveServer.hpp"

#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/qbt/QbtEngine.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

#include <functional>
#include <map>
#include <memory>

using namespace omaseed::core;
using fakehttp::await;

namespace {

constexpr int kPiece = 16384;
const QList<QPair<QString, int>> kFiles{{QStringLiteral("a.bin"), 3 * kPiece + 1000},
                                        {QStringLiteral("sub/b.bin"), 2 * kPiece}};

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

/// A v1 multi-file .torrent of `content` (kFiles under it), no tracker; its info hash in `hash`.
QByteArray makeTorrent(const QString &content, QString *hash)
{
    QByteArray all;
    QByteArray files = "l";
    for (const auto &[rel, size] : kFiles) {
        QFile f(content + QLatin1Char('/') + rel);
        if (!f.open(QIODevice::ReadOnly))
            qFatal("can't read %s", qPrintable(f.fileName()));
        all += f.readAll();
        QByteArray path = "l";
        for (const QString &part : rel.split(QLatin1Char('/')))
            path += bstr(part.toUtf8());
        path += 'e';
        files += "d" + bstr("length") + bint(size) + bstr("path") + path + "e";
    }
    files += 'e';
    QByteArray pieces;
    for (qsizetype at = 0; at < all.size(); at += kPiece)
        pieces += QCryptographicHash::hash(all.mid(at, kPiece), QCryptographicHash::Sha1);
    const QByteArray info = "d" + bstr("files") + files + bstr("name") +
                            bstr(QFileInfo(content).fileName().toUtf8()) + bstr("piece length") +
                            bint(kPiece) + bstr("pieces") + bstr(pieces) + "e";
    *hash = QString::fromLatin1(QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex());
    return "d" + bstr("info") + info + "e";
}

void writeRandom(const QString &path, int size)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("can't write %s", qPrintable(path));
    QByteArray bytes(size, Qt::Uninitialized);
    for (char &c : bytes)
        c = char(QRandomGenerator::global()->bounded(256));
    f.write(bytes);
}

/// inode, mtime (ns) and sha256 of each file under folder, "absent" for a missing one.
QStringList snapshot(const QString &folder)
{
    QStringList seen;
    for (const auto &[rel, size] : kFiles) {
        const QString path = folder + QLatin1Char('/') + rel;
        struct stat st{};
        QFile f(path);
        if (::stat(QFile::encodeName(path).constData(), &st) != 0 || !f.open(QIODevice::ReadOnly)) {
            seen << rel + QStringLiteral(" absent");
            continue;
        }
        const qint64 mtime = qint64(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
        seen << QStringLiteral("%1 %2 %3 %4")
                    .arg(rel)
                    .arg(st.st_ino)
                    .arg(mtime)
                    .arg(QString::fromLatin1(
                        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex()));
    }
    return seen;
}

bool copyTree(const QString &from, const QString &to)
{
    for (const auto &[rel, size] : kFiles) {
        QDir().mkpath(QFileInfo(to + QLatin1Char('/') + rel).path());
        if (!QFile::copy(from + QLatin1Char('/') + rel, to + QLatin1Char('/') + rel))
            return false;
    }
    return true;
}

QString delugePassword()
{
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("DELUGE_TEST_PASS"));
}

} // namespace

class TestPointAtLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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
    Torrent row(EngineClient &engine, const QString &hash);
    /// Polls the torrent until `done` holds (the engines lag their calls by a second or two).
    Torrent until(EngineClient &engine,
                  const QString &hash,
                  const std::function<bool(const Torrent &)> &done,
                  const char *what);
    void flow(EngineClient &engine);
};

Torrent TestPointAtLive::row(EngineClient &engine, const QString &hash)
{
    const QList<TorrentPatch> rows = ok(engine.torrents({hash}));
    return rows.isEmpty() ? Torrent{} : rows.first().values;
}

Torrent TestPointAtLive::until(EngineClient &engine,
                               const QString &hash,
                               const std::function<bool(const Torrent &)> &done,
                               const char *what)
{
    QElapsedTimer clock;
    clock.start();
    Torrent seen = row(engine, hash);
    while (!done(seen) && clock.elapsed() < 30000) {
        QTest::qWait(250);
        seen = row(engine, hash);
    }
    if (!done(seen))
        qWarning("%s: not reached (progress %f, save path %s)",
                 what,
                 seen.progress,
                 qPrintable(seen.savePath));
    return seen;
}

void TestPointAtLive::flow(EngineClient &engine)
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString name =
        QStringLiteral("omaseed-pointat-%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString old = root.filePath(QStringLiteral("old"));
    const QString neu = root.filePath(QStringLiteral("new"));
    const QString third = root.filePath(QStringLiteral("third"));
    for (const auto &[rel, size] : kFiles)
        writeRandom(old + QLatin1Char('/') + name + QLatin1Char('/') + rel, size);
    QString hash;
    const QByteArray torrent = makeTorrent(old + QLatin1Char('/') + name, &hash);

    AddOptions options;
    options.savePath = old;
    options.stopped = true;
    ok(engine.addFile(name + QStringLiteral(".torrent"), torrent, options));
    const auto removeAtEnd =
        qScopeGuard([&]() { await(this, engine.remove({hash}, false), 30000); });
    QVERIFY(!until(
                 engine, hash, [](const Torrent &t) { return !t.savePath.isEmpty(); }, "the add")
                 .savePath.isEmpty());
    ok(engine.recheck({hash}));
    QCOMPARE(until(
                 engine, hash, [](const Torrent &t) { return t.progress >= 1.0; }, "complete")
                 .progress,
             1.0);

    // Missing at OLD: moved by hand, the engine told by a recheck.
    QVERIFY(QDir().mkpath(neu));
    QVERIFY(QDir().rename(old + QLatin1Char('/') + name, neu + QLatin1Char('/') + name));
    ok(engine.recheck({hash}));
    QVERIFY(until(
                engine, hash, [](const Torrent &t) { return t.progress < 1.0; }, "missing")
                .progress < 1.0);

    const QStringList atNew = snapshot(neu + QLatin1Char('/') + name);
    ok(engine.pointAt({hash}, neu));
    QCOMPARE(until(
                 engine, hash, [&](const Torrent &t) { return t.savePath == neu; }, "re-pointed")
                 .savePath,
             neu);
    QCOMPARE(snapshot(neu + QLatin1Char('/') + name), atNew);
    QVERIFY(!QFileInfo::exists(old + QLatin1Char('/') + name));
    ok(engine.recheck({hash}));
    QCOMPARE(until(
                 engine, hash, [](const Torrent &t) { return t.progress >= 1.0; }, "whole")
                 .progress,
             1.0);
    QCOMPARE(snapshot(neu + QLatin1Char('/') + name), atNew);

    // Both hold the files: THIRD a copy with one byte changed, so an overwrite would show.
    QVERIFY(copyTree(neu + QLatin1Char('/') + name, third + QLatin1Char('/') + name));
    {
        QFile f(third + QLatin1Char('/') + name + QStringLiteral("/a.bin"));
        QVERIFY(f.open(QIODevice::ReadWrite));
        const char first = f.read(1).at(0);
        f.seek(0);
        f.write(QByteArray(1, char(first ^ 0xFF)));
    }
    const QStringList atThird = snapshot(third + QLatin1Char('/') + name);
    ok(engine.pointAt({hash}, third));
    QCOMPARE(until(
                 engine,
                 hash,
                 [&](const Torrent &t) { return t.savePath == third; },
                 "re-pointed over a copy")
                 .savePath,
             third);
    QTest::qWait(2000); // libtorrent checks on its own after a move over existing files
    QCOMPARE(snapshot(neu + QLatin1Char('/') + name), atNew);
    QCOMPARE(snapshot(third + QLatin1Char('/') + name), atThird);
}

void TestPointAtLive::qbittorrent()
{
    if (!listening(8081))
        QSKIP("no qBittorrent on :8081: scripts/dev/test-server.sh start");
    QbtEngine engine;
    engine.setBaseUrl(LiveServer::playground());
    ok(engine.probe());
    flow(engine);
}

void TestPointAtLive::transmission()
{
    if (!listening(8085))
        QSKIP("no Transmission on :8085: scripts/dev/test-server.sh start --transmission");
    TransmissionEngine engine;
    engine.setBaseUrl(
        LiveServer::fromEnv("OMASEED_TRANSMISSION_URL", QStringLiteral("http://127.0.0.1:8085")));
    engine.setTimeout(std::chrono::seconds(15));
    ok(engine.probe());
    flow(engine);
}

void TestPointAtLive::deluge()
{
    if (!listening(8086))
        QSKIP("no deluge-web on :8086: scripts/dev/test-server.sh start --deluge");
    if (delugePassword().isEmpty())
        QSKIP("no DELUGE_TEST_PASS: set -a; . ~/.config/omaseed/dev.env; set +a");
    DelugeEngine engine;
    engine.setBaseUrl(
        LiveServer::fromEnv("OMASEED_DELUGE_URL", QStringLiteral("http://127.0.0.1:8086")));
    engine.setCredentials(QString(), delugePassword());
    engine.setTimeout(std::chrono::seconds(15));
    ok(engine.probe());
    flow(engine);
}

QTEST_GUILESS_MAIN(TestPointAtLive)
#include "tst_pointat_live.moc"
