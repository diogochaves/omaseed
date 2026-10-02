// CertificatePin: the fingerprint a trusted certificate is saved under, and the two text
// forms it travels in. The fixture is a self-signed certificate whose key was thrown away;
// its SHA-256 is what `openssl x509 -noout -fingerprint -sha256` prints for it.

#include "core/CertificatePin.hpp"

#include <QFile>
#include <QSslCertificate>
#include <QTest>

namespace CertificatePin = omaseed::core::CertificatePin;

namespace {

const auto kFixtureDisplay = QStringLiteral("9C:E0:61:8D:3D:BA:07:B6:AB:CE:06:A4:C4:DB:22:22:"
                                            "F6:92:96:05:A3:44:B4:F8:EC:21:78:9F:27:61:A2:A2");
const auto kFixtureHex =
    QStringLiteral("9ce0618d3dba07b6abce06a4c4db2222f6929605a344b4f8ec21789f2761a2a2");

} // namespace

class TestCertificatePin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fingerprintIsTheCertificatesSha256();
    void aNullCertificateHasNone();
    void normalizes_data();
    void normalizes();
    void displaysAsColonPairs();
};

void TestCertificatePin::fingerprintIsTheCertificatesSha256()
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/selfsigned.crt"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QSslCertificate certificate(file.readAll(), QSsl::Pem);
    QVERIFY(!certificate.isNull());
    QCOMPARE(CertificatePin::fingerprint(certificate), kFixtureHex);
}

void TestCertificatePin::aNullCertificateHasNone()
{
    QVERIFY(CertificatePin::fingerprint(QSslCertificate()).isEmpty());
}

void TestCertificatePin::normalizes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("stored form") << kFixtureHex << kFixtureHex;
    QTest::newRow("display form") << kFixtureDisplay << kFixtureHex;
    QTest::newRow("upper case") << kFixtureHex.toUpper() << kFixtureHex;
    QTest::newRow("spaces") << QString(kFixtureDisplay).replace(QLatin1Char(':'), QLatin1Char(' '))
                            << kFixtureHex;
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("too short") << kFixtureHex.left(62) << QString();
    QTest::newRow("too long") << kFixtureHex + QStringLiteral("00") << QString();
    QTest::newRow("not hex") << QString(kFixtureHex).replace(0, 1, QLatin1Char('g')) << QString();
    QTest::newRow("sha-1 length") << kFixtureHex.left(40) << QString();
}

void TestCertificatePin::normalizes()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(CertificatePin::normalized(text), expected);
}

void TestCertificatePin::displaysAsColonPairs()
{
    QCOMPARE(CertificatePin::displayText(kFixtureHex), kFixtureDisplay);
    QCOMPARE(CertificatePin::displayText(kFixtureDisplay), kFixtureDisplay);
    QVERIFY(CertificatePin::displayText(QString()).isEmpty());
    QCOMPARE(CertificatePin::displayText(QStringLiteral("nonsense")), QStringLiteral("nonsense"));
}

QTEST_GUILESS_MAIN(TestCertificatePin)
#include "tst_certificatepin.moc"
