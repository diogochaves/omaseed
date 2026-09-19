#include "core/TorrentFile.hpp"

#include <QEventLoop>
#include <QFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

// Talks to the :8081 playground only (see CLAUDE.md). Uses QNetworkAccessManager directly,
// not T2.1's QbtClient, because that task is not merged yet.

using omaseed::core::TorrentFile;

namespace {

constexpr int kTimeoutMs = 2000;
const QString kBaseUrl = QStringLiteral("http://127.0.0.1:8081");

/// Runs the event loop until `reply` finishes or kTimeoutMs elapses. Returns false on timeout,
/// so the caller can QSKIP instead of failing when the playground isn't running.
bool waitForReply(QNetworkReply *reply)
{
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&timedOut, &loop]() {
        timedOut = true;
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(kTimeoutMs);
    loop.exec();
    return !timedOut;
}

} // namespace

class TestTorrentFileLive : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void addAndDeleteRoundTripsInfoHash();

private:
    void deleteFixtureTorrent(const QString &hash);

    // Set as soon as the fixture's hash is known, so cleanup() can delete it from the
    // playground even when an assertion aborts the test before the explicit delete at the
    // end runs. Cleared once deleted so cleanup() is a no-op on a clean pass.
    QString m_hash;
};

void TestTorrentFileLive::cleanup()
{
    if (!m_hash.isEmpty()) {
        deleteFixtureTorrent(m_hash);
        m_hash.clear();
    }
}

void TestTorrentFileLive::deleteFixtureTorrent(const QString &hash)
{
    QNetworkAccessManager manager;
    QUrl deleteUrl(kBaseUrl + QStringLiteral("/api/v2/torrents/delete"));
    const QByteArray deleteBody = QStringLiteral("hashes=%1&deleteFiles=true").arg(hash).toUtf8();
    QNetworkRequest deleteRequest(deleteUrl);
    deleteRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                            QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *deleteReply = manager.post(deleteRequest, deleteBody);
    waitForReply(deleteReply);
    deleteReply->deleteLater();
}

void TestTorrentFileLive::addAndDeleteRoundTripsInfoHash()
{
    QNetworkAccessManager manager;

    // Probe first so the whole test can QSKIP cleanly when the playground isn't running,
    // per the plan's "QSKIP if the playground does not answer within 2 s".
    QNetworkReply *probeReply =
        manager.get(QNetworkRequest(QUrl(kBaseUrl + QStringLiteral("/api/v2/app/version"))));
    const bool probeAnswered = waitForReply(probeReply);
    const bool probeOk = probeAnswered && probeReply->error() == QNetworkReply::NoError;
    probeReply->deleteLater();
    if (!probeOk)
        QSKIP("test-server playground at :8081 did not answer within 2s");

    const QString fixturePath = QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent");
    QFile file(fixturePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(fixturePath));
    const QByteArray torrentBytes = file.readAll();
    file.close();

    QString parseError;
    const TorrentFile torrent = TorrentFile::parse(torrentBytes, &parseError);
    QVERIFY2(torrent.isValid(), qPrintable(parseError));
    const QString hash = torrent.infoHashV1;
    QCOMPARE(hash.size(), 40);

    // A leftover from an interrupted run must not confuse the add/info assertions below.
    deleteFixtureTorrent(hash);
    // From here on cleanup() deletes this hash after the test, pass or fail, so a failed
    // QVERIFY below never leaves Single.bin behind for the next run to trip over.
    m_hash = hash;

    // torrents/add: multipart with the .torrent file, added stopped so nothing seeds.
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart stoppedPart;
    stoppedPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant(QStringLiteral("form-data; name=\"stopped\"")));
    stoppedPart.setBody("true");
    multiPart->append(stoppedPart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/x-bittorrent"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral(
                           "form-data; name=\"torrents\"; filename=\"single_file_v1.torrent\"")));
    filePart.setBody(torrentBytes);
    multiPart->append(filePart);

    QNetworkRequest addRequest(QUrl(kBaseUrl + QStringLiteral("/api/v2/torrents/add")));
    QNetworkReply *addReply = manager.post(addRequest, multiPart);
    multiPart->setParent(addReply);
    QVERIFY2(waitForReply(addReply), "torrents/add timed out");
    QCOMPARE(addReply->error(), QNetworkReply::NoError);
    addReply->deleteLater();

    // torrents/info?hashes=<hash>: must report exactly the torrent we just added, by hash.
    QUrl infoUrl(kBaseUrl + QStringLiteral("/api/v2/torrents/info"));
    QUrlQuery infoQuery;
    infoQuery.addQueryItem(QStringLiteral("hashes"), hash);
    infoUrl.setQuery(infoQuery);

    QNetworkReply *infoReply = manager.get(QNetworkRequest(infoUrl));
    QVERIFY2(waitForReply(infoReply), "torrents/info timed out");
    QCOMPARE(infoReply->error(), QNetworkReply::NoError);
    const QByteArray infoBody = infoReply->readAll();
    infoReply->deleteLater();

    const QJsonArray torrents = QJsonDocument::fromJson(infoBody).array();
    QCOMPARE(torrents.size(), 1);
    const QJsonObject entry = torrents.first().toObject();
    QCOMPARE(entry.value(QStringLiteral("hash")).toString().toLower(), hash);

    // cleanup() deletes m_hash unconditionally, run after this function whether it passed
    // or failed, so no explicit torrents/delete is needed here.
}

QTEST_GUILESS_MAIN(TestTorrentFileLive)

#include "tst_torrentfile_live.moc"
