// DECISIONS C28 (#159): on a disk that takes every ticked file's full size at once (C17 D8:
// ntfs3, exfat, vfat), a tick that doesn't fit waits in D5's band whatever its size, so it can
// be undone before libtorrent writes anything (#116). Through App, against the :8081
// playground: a 16 MiB FAT image is set up as a loop device and mounted through udisks (as the
// user, no root), as tst_skipped_ntfs3_live does with ntfs3. A torrent of four files of zeros
// is added there, stopped, with one small file wanted. Ticking a 24 MiB file waits with the
// band's red room line; undo sends nothing and nothing is written on the image. A 1 MiB tick
// fits and goes at once. The box walks can't do this: a box can't see a mount udisks made.
//
// QSKIPs when the playground doesn't answer, udisksctl or mkfs.vfat is missing or refuses, or
// this machine doesn't see the playground's disks.

#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include "LiveServer.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>

using namespace omaseed::core;
using omaseed::app::App;

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
const QString kName = QStringLiteral("omaseed-159-vfat");
const QString kLabel = QStringLiteral("OMASEED159");
/// a.bin wanted; b.bin and c.bin too big for the image; d.bin small enough.
const QList<qint64> kSizes = {1 * kMiB, 24 * kMiB, 24 * kMiB, 1 * kMiB};

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

/// The stdout of `program args`, or a null string when it failed.
QString run(const QString &program, const QStringList &args)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return QString();
    return QString::fromUtf8(process.readAllStandardOutput()) + QStringLiteral(" ");
}

QByteArray bencode(const QByteArray &text)
{
    return QByteArray::number(text.size()) + ':' + text;
}

/// A v1 .torrent of kSizes' files of zeros under `kName`, in 1 MiB pieces.
QByteArray zeroTorrent()
{
    qint64 total = 0;
    QByteArray files = "l";
    const char *names[] = {"a.bin", "b.bin", "c.bin", "d.bin"};
    for (qsizetype i = 0; i < kSizes.size(); ++i) {
        files +=
            "d6:lengthi" + QByteArray::number(kSizes.at(i)) + "e4:pathl" + bencode(names[i]) + "ee";
        total += kSizes.at(i);
    }
    files += "e";
    const QByteArray piece =
        QCryptographicHash::hash(QByteArray(kMiB, '\0'), QCryptographicHash::Sha1);
    const QByteArray info = "d5:files" + files + "4:name" + bencode(kName.toUtf8()) +
                            "12:piece lengthi" + QByteArray::number(kMiB) + "e6:pieces" +
                            bencode(piece.repeated(int(total / kMiB))) + "e";
    return "d10:created by" + bencode("omaseed tst_app_tickroom_live") + "4:info" + info + "e";
}

QString infoHashOf(const QByteArray &torrent)
{
    const qsizetype at = torrent.indexOf("4:infod");
    return QString::fromLatin1(
        QCryptographicHash::hash(torrent.mid(at + 6, torrent.size() - at - 7),
                                 QCryptographicHash::Sha1)
            .toHex());
}

} // namespace

class TestAppTickRoomLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void aTickThatDoesntFitWaitsAndUndoWritesNothing();

private:
    QList<int> priorities();

    QTemporaryDir m_dir;
    QbtClient *m_client = nullptr;
    QString m_device;
    QString m_mount;
    QString m_hash;
};

void TestAppTickRoomLive::initTestCase()
{
    const QUrl url = LiveServer::playground();
    if (!url.isValid() || !answers(url))
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");
    if (QStandardPaths::findExecutable(QStringLiteral("udisksctl")).isEmpty() ||
        QStandardPaths::findExecutable(QStringLiteral("mkfs.vfat")).isEmpty())
        QSKIP("needs udisksctl and mkfs.vfat");
    QVERIFY(m_dir.isValid());
    qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
    qputenv("OMASEED_SERVER_URL", url.toString().toUtf8());

    const QString image = m_dir.filePath(QStringLiteral("fat.img"));
    QFile file(image);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(16 * kMiB));
    file.close();
    if (run(QStringLiteral("mkfs.vfat"), {QStringLiteral("-n"), kLabel, image}).isNull())
        QSKIP("mkfs.vfat failed");
    const QString setup = run(QStringLiteral("udisksctl"),
                              {QStringLiteral("loop-setup"),
                               QStringLiteral("-f"),
                               image,
                               QStringLiteral("--no-user-interaction")});
    const QRegularExpressionMatch device =
        QRegularExpression(QStringLiteral("(/dev/loop\\d+)")).match(setup);
    if (!device.hasMatch())
        QSKIP("udisksctl loop-setup refused");
    m_device = device.captured(1);
    // A desktop automounter may have mounted it already: mount it as asked.
    run(QStringLiteral("udisksctl"),
        {QStringLiteral("unmount"),
         QStringLiteral("-b"),
         m_device,
         QStringLiteral("--no-user-interaction")});
    const QString mounted = run(QStringLiteral("udisksctl"),
                                {QStringLiteral("mount"),
                                 QStringLiteral("-b"),
                                 m_device,
                                 QStringLiteral("-t"),
                                 QStringLiteral("vfat"),
                                 QStringLiteral("--no-user-interaction")});
    const QRegularExpressionMatch at =
        QRegularExpression(QStringLiteral(" at (/\\S+)")).match(mounted);
    if (!at.hasMatch())
        QSKIP("udisksctl mount -t vfat refused");
    m_mount = at.captured(1);

    m_client = new QbtClient(this);
    m_client->setBaseUrl(url);
    const QByteArray torrent = zeroTorrent();
    m_hash = infoHashOf(torrent);
    await(this, m_client->torrentsDelete({m_hash}, true));
    QbtClient::AddOptions options;
    options.savepath = m_mount;
    options.stopped = true;
    options.autoTMM = false;
    const auto added = await(this, m_client->torrentsAddFile(kName + ".torrent", torrent, options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    QVERIFY2(waitFor([this]() { return priorities().size() == 4; }, 15000), "never listed");
    // Added with only a.bin wanted, as the add sheet would leave it.
    QVERIFY(!await(this, m_client->torrentsSetFilePrio(m_hash, {1, 2, 3}, 0)).isError());
    QVERIFY(waitFor([this]() { return priorities() == QList<int>{1, 0, 0, 0}; }, 10000));
}

void TestAppTickRoomLive::cleanupTestCase()
{
    if (m_client && !m_hash.isEmpty())
        await(this, m_client->torrentsDelete({m_hash}, true));
    if (!m_device.isEmpty()) {
        // qBittorrent lets go of the files a moment after the delete.
        waitFor(
            [this]() {
                return !run(QStringLiteral("udisksctl"),
                            {QStringLiteral("unmount"),
                             QStringLiteral("-b"),
                             m_device,
                             QStringLiteral("--no-user-interaction")})
                            .isNull();
            },
            15000);
        run(QStringLiteral("udisksctl"),
            {QStringLiteral("loop-delete"),
             QStringLiteral("-b"),
             m_device,
             QStringLiteral("--no-user-interaction")});
    }
}

QList<int> TestAppTickRoomLive::priorities()
{
    const auto files = await(this, m_client->torrentsFiles(m_hash), 5000);
    QList<int> result;
    if (files.isError())
        return result;
    for (const QJsonValue &value : files.value())
        result.append(value.toObject().value(QStringLiteral("priority")).toInt());
    return result;
}

void TestAppTickRoomLive::aTickThatDoesntFitWaitsAndUndoWritesNothing()
{
    App app;
    app.start();
    QVERIFY(app.hasServer());
    QVERIFY2(waitFor([&]() { return app.torrents()->indexOfHash(m_hash) >= 0; }, 10000),
             "the torrent never reached the table");
    app.selectTorrent(m_hash);
    TorrentFilesModel *files = app.details()->files();
    QVERIFY2(waitFor([files]() { return files->fileCount() == 4; }, 10000),
             "the Files tab never filled");
    if (!waitFor(
            [&]() {
                const QVariantMap answer = app.freeSpaceFor(m_mount);
                return answer.value(QStringLiteral("source")) == QStringLiteral("local") &&
                       answer.value(QStringLiteral("fileSystem")) == QStringLiteral("vfat");
            },
            10000))
        QSKIP("this machine does not see the playground's disk");
    app.setSelectionHoldWindow(1500);
    QSignalSpy sent(&app, &App::filePrioritySent);
    const auto waiting = [&app]() {
        return app.heldSelectionMap().value(QStringLiteral("waiting")).toBool();
    };
    const QString b = m_mount + QLatin1Char('/') + kName + QStringLiteral("/b.bin");

    // 24 MiB on a 16 MiB vfat: under D5's 1 GiB and not the whole torrent, yet it waits.
    app.setFilePriority(m_hash, {1}, 1);
    QVERIFY(waiting());
    QCOMPARE(sent.count(), 0);
    const QVariantMap room = app.heldSelectionMap().value(QStringLiteral("room")).toMap();
    QCOMPARE(room.value(QStringLiteral("fileSystem")).toString(), QStringLiteral("vfat"));
    QVERIFY(room.value(QStringLiteral("short")).toBool());
    QVERIFY2(room.value(QStringLiteral("text"))
                 .toString()
                 .contains(QStringLiteral("Select fewer files, or save to another disk")),
             qPrintable(room.value(QStringLiteral("text")).toString()));
    QCOMPARE(files->filePriority(1), 1);

    // Undo: the row goes back, nothing reaches qBittorrent, nothing is written on the disk.
    app.undoHeldSelection();
    QVERIFY(!waiting());
    QCOMPARE(files->filePriority(1), 0);
    QTest::qWait(2500);
    QCOMPARE(sent.count(), 0);
    QCOMPARE(priorities(), (QList<int>{1, 0, 0, 0}));
    QVERIFY2(!QFileInfo::exists(b) || QFileInfo(b).size() == 0, qPrintable(b));

    // 1 MiB fits: sent at once, as before.
    app.setFilePriority(m_hash, {3}, 1);
    QVERIFY(!waiting());
    QCOMPARE(sent.count(), 1);
    QVERIFY2(waitFor([this]() { return priorities() == QList<int>{1, 0, 0, 1}; }, 10000),
             "the small tick never arrived");

    app.engine()->stop();
}

QTEST_MAIN(TestAppTickRoomLive)
#include "tst_app_tickroom_live.moc"
