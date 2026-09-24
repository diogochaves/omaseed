#include "core/MagnetLink.hpp"

#include <QUrl>

#include <algorithm>
#include <cctype>

namespace omaseed::core {

namespace {

QByteArray base32Decode(const QString &input)
{
    static const QByteArray alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    const QByteArray upper = input.toUpper().toLatin1();
    QByteArray out;
    int buffer = 0;
    int bitsLeft = 0;
    for (const char c : upper) {
        const int index = alphabet.indexOf(c);
        if (index < 0)
            return {};
        buffer = (buffer << 5) | index;
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            bitsLeft -= 8;
            out.append(static_cast<char>((buffer >> bitsLeft) & 0xFF));
        }
    }
    return out;
}

bool isHex(const QString &s)
{
    // ASCII only: toLatin1() of U+0080..U+00FF is a negative char, which <cctype> may not see.
    return !s.isEmpty() && std::all_of(s.begin(), s.end(), [](QChar c) {
        return c.unicode() < 128 && std::isxdigit(static_cast<unsigned char>(c.unicode()));
    });
}

/// Normalises a btih hash (40-hex or 32-char base32) to lowercase hex. Empty on failure.
QString normalizeInfoHashV1(const QString &hash)
{
    if (hash.size() == 40 && isHex(hash))
        return hash.toLower();
    if (hash.size() == 32) {
        const QByteArray bytes = base32Decode(hash);
        if (bytes.size() == 20)
            return QString::fromLatin1(bytes.toHex());
    }
    return {};
}

/// Parses one `xt` topic value ("urn:btih:..." or "urn:btmh:1220..."). Returns true and fills
/// the matching hash field in `out` when it is a hash omaseed recognises.
bool parseExactTopic(const QString &value, MagnetLink &out)
{
    static const QString btihPrefix = QStringLiteral("urn:btih:");
    static const QString btmhPrefix = QStringLiteral("urn:btmh:");

    if (value.startsWith(btihPrefix, Qt::CaseInsensitive)) {
        const QString normalized = normalizeInfoHashV1(value.mid(btihPrefix.size()));
        if (normalized.isEmpty())
            return false;
        out.infoHashV1 = normalized;
        return true;
    }
    if (value.startsWith(btmhPrefix, Qt::CaseInsensitive)) {
        const QString rest = value.mid(btmhPrefix.size());
        // "1220" is the multihash header for sha2-256 (0x12) with a 32-byte digest (0x20).
        if (rest.size() == 68 && rest.startsWith(QStringLiteral("1220"), Qt::CaseInsensitive)) {
            const QString digest = rest.mid(4).toLower();
            if (isHex(digest)) {
                out.infoHashV2 = digest;
                return true;
            }
        }
        return false;
    }
    return false;
}

bool isNumberedTopic(const QString &key)
{
    if (!key.startsWith(QStringLiteral("xt.")) || key.size() == 3)
        return false;
    return std::all_of(key.begin() + 3, key.end(), [](QChar c) {
        return c >= QLatin1Char('0') && c <= QLatin1Char('9');
    });
}

QString decodePercent(const QString &raw, bool plusAsSpace = false)
{
    QByteArray bytes = raw.toUtf8();
    if (plusAsSpace)
        bytes.replace('+', ' ');
    return QUrl::fromPercentEncoding(bytes);
}

} // namespace

MagnetLink MagnetLink::parse(const QString &uri)
{
    MagnetLink result;

    const QString trimmed = uri.trimmed();
    if (!trimmed.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive))
        return result;

    const qsizetype queryStart = trimmed.indexOf(QLatin1Char('?'));
    const QString queryPart = queryStart >= 0 ? trimmed.mid(queryStart + 1) : QString();

    bool hasHash = false;
    const QStringList pairs = queryPart.split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        const QString rawKey = eq >= 0 ? pair.left(eq) : pair;
        const QString rawValue = eq >= 0 ? pair.mid(eq + 1) : QString();
        const QString key = decodePercent(rawKey).toLower();

        // BEP 9 numbers repeated topics as xt.1, xt.2, ... (a hybrid's v1 and v2 hash).
        if (key == QStringLiteral("xt") || isNumberedTopic(key)) {
            if (parseExactTopic(decodePercent(rawValue), result))
                hasHash = true;
        } else if (key == QStringLiteral("dn")) {
            result.displayName = decodePercent(rawValue, true);
        } else if (key == QStringLiteral("tr")) {
            result.trackers.append(decodePercent(rawValue));
        } else if (key == QStringLiteral("ws")) {
            result.webSeeds.append(decodePercent(rawValue));
        } else if (key == QStringLiteral("xl")) {
            bool ok = false;
            const qint64 len = decodePercent(rawValue).toLongLong(&ok);
            if (ok && len >= 0)
                result.exactLength = len;
        } else if (key == QStringLiteral("x.pe")) {
            result.peers.append(decodePercent(rawValue));
        }
    }

    result.m_valid = hasHash;
    return result;
}

QString MagnetLink::toString() const
{
    if (!isValid())
        return QString();

    QStringList parts;
    if (!infoHashV1.isEmpty())
        parts << QStringLiteral("xt=urn:btih:") + infoHashV1;
    if (!infoHashV2.isEmpty())
        parts << QStringLiteral("xt=urn:btmh:1220") + infoHashV2;
    if (!displayName.isEmpty())
        parts << QStringLiteral("dn=") + QString::fromUtf8(QUrl::toPercentEncoding(displayName));
    if (exactLength >= 0)
        parts << QStringLiteral("xl=%1").arg(exactLength);
    for (const QString &tracker : trackers)
        parts << QStringLiteral("tr=") + QString::fromUtf8(QUrl::toPercentEncoding(tracker));
    for (const QString &webSeed : webSeeds)
        parts << QStringLiteral("ws=") + QString::fromUtf8(QUrl::toPercentEncoding(webSeed));
    for (const QString &peer : peers)
        parts << QStringLiteral("x.pe=") + QString::fromUtf8(QUrl::toPercentEncoding(peer));

    return QStringLiteral("magnet:?") + parts.join(QLatin1Char('&'));
}

} // namespace omaseed::core
