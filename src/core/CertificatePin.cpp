#include "core/CertificatePin.hpp"

#include <QCryptographicHash>
#include <QSslCertificate>
#include <QStringList>

namespace omaseed::core::CertificatePin {

namespace {

constexpr qsizetype kHexLength = 64; // SHA-256: 32 bytes

bool isHex(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
           (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
}

} // namespace

QString fingerprint(const QSslCertificate &certificate)
{
    if (certificate.isNull())
        return {};
    return QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex());
}

QString normalized(const QString &text)
{
    QString hex;
    hex.reserve(kHexLength);
    for (const QChar c : text) {
        if (c == QLatin1Char(':') || c.isSpace())
            continue;
        const QChar lower = c.toLower();
        if (!isHex(lower))
            return {};
        hex.append(lower);
    }
    return hex.size() == kHexLength ? hex : QString();
}

QString displayText(const QString &fingerprint)
{
    const QString hex = normalized(fingerprint);
    if (hex.isEmpty())
        return fingerprint;
    QStringList pairs;
    pairs.reserve(kHexLength / 2);
    for (qsizetype i = 0; i < hex.size(); i += 2)
        pairs.append(hex.mid(i, 2).toUpper());
    return pairs.join(QLatin1Char(':'));
}

} // namespace omaseed::core::CertificatePin
