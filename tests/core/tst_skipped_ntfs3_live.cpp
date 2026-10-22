// Board 44's misclick on a real ntfs3 disk (DECISIONS C17 D1, D3, D8; #116, #118, #120), against
// the :8081 playground. A 256 MiB NTFS image is made, set up as a loop device and mounted with
// the in-kernel ntfs3 driver through udisks (as the user, no root), as
// scripts/dev/research/116-fs-allocation does. A torrent of four 16 MiB files is added there,
// stopped, with one file wanted; every file is wanted for 3 s and skipped again. libtorrent
// creates each newly wanted file at full length, and on ntfs3 that allocates it: SkippedOnDisk
// reads the three skipped files holding their whole size, FreeSkippedJob deletes them, and the
// torrent's selection is as it was. The box walks (actions-walk.sh) can't do this part: a box
// can't write to a mount udisks made here.
//
// QSKIPs when the playground doesn't answer, or udisksctl or mkfs.ntfs is missing or refuses.

#include "LiveServer.hpp"

#include "core/FreeSpace.hpp"
#include "core/QbtClient.hpp"
#include "core/SkippedJobs.hpp"
#include "core/SkippedOnDisk.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
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

using namespace omaseed::core;

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kFileSize = 16 * kMiB;
constexpr qint64 kPiece = kMiB;
const QString kName = QStringLiteral("omaseed-118-ntfs3");
const QString kLabel = QStringLiteral("omaseed118");

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
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
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

/// A v1 .torrent of four files of zeros under `kName`.
QByteArray zeroTorrent()
{
    const qint64 total = 4 * kFileSize;
    const QByteArray piece =
        QCryptographicHash::hash(QByteArray(kPiece, '\0'), QCryptographicHash::Sha1);
    QByteArray files = "l";
    for (const char *file : {"a.bin", "b.bin", "c.bin", "d.bin"})
        files += "d6:lengthi" + QByteArray::number(kFileSize) + "e4:pathl" + bencode(file) + "ee";
    files += "e";
    const QByteArray info = "d5:files" + files + "4:name" + bencode(kName.toUtf8()) +
                            "12:piece lengthi" + QByteArray::number(kPiece) + "e6:pieces" +
                            bencode(piece.repeated(int(total / kPiece))) + "e";
    return "d10:created by" + bencode("omaseed tst_skipped_ntfs3_live") + "4:info" + info + "e";
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

class TestSkippedNtfs3Live : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void aMisclickOnNtfs3HoldsTheSkippedFilesAndFreeingGivesItBack();

private:
    QList<int> priorities();

    QbtClient *m_client = nullptr;
    QTemporaryDir m_dir;
    QString m_device;
    QString m_mount;
    QString m_hash;
};

void TestSkippedNtfs3Live::initTestCase()
{
    const QUrl url = LiveServer::playground();
    if (!url.isValid() || !answers(url))
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");
    if (QStandardPaths::findExecutable(QStringLiteral("udisksctl")).isEmpty() ||
        QStandardPaths::findExecutable(QStringLiteral("mkfs.ntfs")).isEmpty())
        QSKIP("needs udisksctl and mkfs.ntfs");
    QVERIFY(m_dir.isValid());

    const QString image = m_dir.filePath(QStringLiteral("ntfs.img"));
    QFile file(image);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(256 * kMiB));
    file.close();
    if (run(QStringLiteral("mkfs.ntfs"),
            {QStringLiteral("-Q"), QStringLiteral("-F"), QStringLiteral("-L"), kLabel, image})
            .isNull())
        QSKIP("mkfs.ntfs failed");
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
    // udisks may have mounted it already, with its own choice of driver: mount it as asked.
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
                                 QStringLiteral("ntfs3"),
                                 QStringLiteral("--no-user-interaction")});
    const QRegularExpressionMatch at =
        QRegularExpression(QStringLiteral(" at (/\\S+)")).match(mounted);
    if (!at.hasMatch())
        QSKIP("udisksctl mount -t ntfs3 refused");
    m_mount = at.captured(1);
    // D8: QStorageInfo names the driver.
    QCOMPARE(FreeSpaceProbe::readLocal(m_mount).fileSystem, QStringLiteral("ntfs3"));

    m_client = new QbtClient(this);
    m_client->setBaseUrl(url);
    const QByteArray torrent = zeroTorrent();
    m_hash = infoHashOf(torrent);
    await(this, m_client->torrentsDelete({m_hash}, true));
    QbtClient::AddOptions options;
    options.savepath = m_mount;
    options.stopped = true;
    const auto added = await(this, m_client->torrentsAddFile(kName + ".torrent", torrent, options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    QVERIFY2(waitFor([this]() { return priorities().size() == 4; }, 15000), "never listed");
    // Added with one file wanted, as the add sheet would leave it.
    QVERIFY(!await(this, m_client->torrentsSetFilePrio(m_hash, {1, 2, 3}, 0)).isError());
    QVERIFY(waitFor([this]() { return priorities() == QList<int>{1, 0, 0, 0}; }, 10000));
}

void TestSkippedNtfs3Live::cleanupTestCase()
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

QList<int> TestSkippedNtfs3Live::priorities()
{
    const auto files = await(this, m_client->torrentsFiles(m_hash), 5000);
    QList<int> result;
    if (files.isError())
        return result;
    for (const QJsonValue &value : files.value())
        result.append(value.toObject().value(QStringLiteral("priority")).toInt());
    return result;
}

void TestSkippedNtfs3Live::aMisclickOnNtfs3HoldsTheSkippedFilesAndFreeingGivesItBack()
{
    const QString root = m_mount + QLatin1Char('/');
    const SkippedOnDisk::LocalPathOf localPathOf = [root](const QString &path) {
        return root + path;
    };
    // The misclick: every file wanted for 3 s on the stopped torrent, then skipped again.
    QVERIFY(!await(this, m_client->torrentsSetFilePrio(m_hash, {1, 2, 3}, 1)).isError());
    QTest::qWait(3000);
    QVERIFY(!await(this, m_client->torrentsSetFilePrio(m_hash, {1, 2, 3}, 0)).isError());
    QVERIFY(waitFor([this]() { return priorities() == QList<int>{1, 0, 0, 0}; }, 10000));

    // #116: each newly wanted file was created at its full length, and ntfs3 allocated it.
    SkippedReading reading;
    QVERIFY2(waitFor(
                 [&]() {
                     const auto files = await(this, m_client->torrentsFiles(m_hash), 5000);
                     if (files.isError())
                         return false;
                     reading = SkippedOnDisk::read(m_hash,
                                                   files.value(),
                                                   QByteArray(),
                                                   4 * kFileSize,
                                                   localPathOf,
                                                   SkippedOnDisk::readAllocated);
                     return reading.holding == 3;
                 },
                 20000),
             qPrintable(QStringLiteral("%1 skipped files hold space").arg(reading.holding)));
    QVERIFY2(reading.held >= 3 * kFileSize,
             qPrintable(QStringLiteral("they hold %1 bytes").arg(reading.held)));
    QVERIFY(reading.bandShown());

    // D3: no piece downloaded, so stop (already stopped), delete, no recheck.
    FreeSkippedJob job(m_client);
    QSignalSpy finished(&job, &FreeSkippedJob::finished);
    FreeSkippedJob::Input input;
    input.hash = m_hash;
    input.name = kName;
    input.running = false;
    input.torrentSize = 4 * kFileSize;
    input.localPathOf = localPathOf;
    input.contentLocal = root + kName;
    job.start(input);
    QVERIFY(finished.wait(30000));
    const auto outcome = finished.first().first().value<FreeSkippedJob::Outcome>();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY2(outcome.openBy.isEmpty(), qPrintable(outcome.openBy.join(QLatin1Char(','))));
    QCOMPARE(outcome.deleted, 3);
    QVERIFY(!outcome.rechecked);
    QVERIFY(outcome.freed >= 3 * kFileSize);
    for (const char *name : {"b.bin", "c.bin", "d.bin"})
        QVERIFY(!QFile::exists(root + kName + QLatin1Char('/') + QLatin1String(name)));
    QCOMPARE(priorities(), (QList<int>{1, 0, 0, 0}));
}

QTEST_GUILESS_MAIN(TestSkippedNtfs3Live)

#include "tst_skipped_ntfs3_live.moc"
