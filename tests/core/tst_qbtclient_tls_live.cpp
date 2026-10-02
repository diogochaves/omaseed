// Trust on first use against a real qBittorrent (issue #2, docs/QUESTIONS.md Q27): a
// qbittorrent-nox of the test's own (tests/ThrowawayQbt.hpp) serving its Web UI over HTTPS
// with a self-signed certificate made for the run. Without a pin the client refuses it with
// Kind::Certificate and says which certificate it saw; pinned to that certificate every kind
// of request goes through (login, GET, form POST, multipart upload); pinned to another one it
// is refused again, also on a connection that was open under the right pin. Skips without
// qbittorrent-nox or openssl.

#include "core/CertificatePin.hpp"
#include "core/QbtClient.hpp"

#include "ThrowawayQbt.hpp"

#include <QEventLoop>
#include <QFile>
#include <QSslCertificate>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
namespace CertificatePin = omaseed::core::CertificatePin;

namespace {

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &value) {
        result = value;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

QString fingerprintOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return CertificatePin::fingerprint(QSslCertificate(file.readAll(), QSsl::Pem));
}

} // namespace

class TestQbtClientTlsLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void refusedWithoutAPinAndSaysWhichCertificate();
    void acceptedWhenPinnedForEveryKindOfRequest();
    void refusedWithAnotherPin();
    void aNewPinDropsTheOpenConnection();

private:
    QbtClient *signedInClient(const QString &pin);

    QTemporaryDir m_dir;
    ThrowawayQbt m_qbt;
    QString m_fingerprint;
    QString m_otherFingerprint;
};

void TestQbtClientTlsLive::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QString cert;
    QString key;
    QString error;
    if (!ThrowawayQbt::makeCertificate(m_dir.path(), QStringLiteral("server"), &cert, &key, &error))
        QSKIP(qPrintable(error));
    QString otherCert;
    QString otherKey;
    QVERIFY2(ThrowawayQbt::makeCertificate(
                 m_dir.path(), QStringLiteral("other"), &otherCert, &otherKey, &error),
             qPrintable(error));
    m_fingerprint = fingerprintOf(cert);
    m_otherFingerprint = fingerprintOf(otherCert);
    QCOMPARE(m_fingerprint.size(), 64);
    QVERIFY(m_fingerprint != m_otherFingerprint);

    m_qbt.useHttps(cert, key);
    if (!m_qbt.start()) {
        if (m_qbt.skipReason().isEmpty())
            QFAIL(qPrintable(m_qbt.error()));
        QSKIP(qPrintable(m_qbt.skipReason()));
    }
    QCOMPARE(m_qbt.url().scheme(), QStringLiteral("https"));
}

void TestQbtClientTlsLive::cleanupTestCase()
{
    m_qbt.stop();
}

QbtClient *TestQbtClientTlsLive::signedInClient(const QString &pin)
{
    auto *client = new QbtClient(this);
    client->setPinnedCertificate(pin);
    client->setBaseUrl(m_qbt.url());
    client->setCredentials(m_qbt.username(), m_qbt.password());
    client->setMaxRetries(0);
    return client;
}

void TestQbtClientTlsLive::refusedWithoutAPinAndSaysWhichCertificate()
{
    QbtClient *client = signedInClient(QString());
    const auto result = await(this, client->appVersion());
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Certificate);
    QCOMPARE(result.error().certificateSha256(), m_fingerprint);
    QVERIFY(!result.error().isTransient());
    QVERIFY(result.error().toString().startsWith(QLatin1String("certificate")));
    client->deleteLater();
}

void TestQbtClientTlsLive::acceptedWhenPinnedForEveryKindOfRequest()
{
    QbtClient *client = signedInClient(m_fingerprint);

    // A login, then a GET with the session it made.
    const auto login = await(this, client->login());
    QVERIFY2(!login.isError(), qPrintable(login.error().toString()));
    const auto version = await(this, client->appVersion());
    QVERIFY2(!version.isError(), qPrintable(version.error().toString()));
    QVERIFY(version.value().startsWith(QLatin1Char('v')));
    const auto maindata = await(this, client->syncMainData(0));
    QVERIFY2(!maindata.isError(), qPrintable(maindata.error().toString()));

    // A form POST that changes the server, read back.
    const QString tag = QStringLiteral("tls-pinned");
    const auto created = await(this, client->torrentsCreateTags({tag}));
    QVERIFY2(!created.isError(), qPrintable(created.error().toString()));
    const auto tags = await(this, client->torrentsTags());
    QVERIFY(!tags.isError());
    QVERIFY(tags.value().contains(tag));

    // A multipart upload, stopped so it never reaches for a peer.
    QFile torrent(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    QVERIFY(torrent.open(QIODevice::ReadOnly));
    QbtClient::AddOptions options;
    options.stopped = true;
    const auto added =
        await(this,
              client->torrentsAddFile(
                  QStringLiteral("single_file_v1.torrent"), torrent.readAll(), options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            const auto list = await(this, client->torrentsInfo());
            return !list.isError() && list.value().size() == 1;
        }(),
        10000);
    client->deleteLater();
}

void TestQbtClientTlsLive::refusedWithAnotherPin()
{
    QbtClient *client = signedInClient(m_otherFingerprint);
    const auto result = await(this, client->login());
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Certificate);
    QCOMPARE(result.error().certificateSha256(), m_fingerprint);
    client->deleteLater();
}

void TestQbtClientTlsLive::aNewPinDropsTheOpenConnection()
{
    QbtClient *client = signedInClient(m_fingerprint);
    const auto first = await(this, client->appVersion());
    QVERIFY2(!first.isError(), qPrintable(first.error().toString()));

    // The keep-alive connection accepted under the right pin must not carry the next request.
    client->setPinnedCertificate(m_otherFingerprint);
    const auto second = await(this, client->appVersion());
    QVERIFY(second.isError());
    QCOMPARE(second.error().kind(), ApiError::Kind::Certificate);

    client->setPinnedCertificate(CertificatePin::displayText(m_fingerprint));
    QCOMPARE(client->pinnedCertificate(), m_fingerprint);
    const auto third = await(this, client->appVersion());
    QVERIFY2(!third.isError(), qPrintable(third.error().toString()));
    client->deleteLater();
}

QTEST_GUILESS_MAIN(TestQbtClientTlsLive)
#include "tst_qbtclient_tls_live.moc"
