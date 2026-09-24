#include "core/PeerInput.hpp"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrl>

namespace omaseed::core::PeerInput {

namespace {

QStringList split(const QString &text, const QRegularExpression &separators)
{
    QStringList out;
    const QStringList parts = text.split(separators, Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString entry = part.trimmed();
        if (!entry.isEmpty() && !out.contains(entry))
            out.append(entry);
    }
    return out;
}

bool isPort(QStringView text)
{
    if (text.isEmpty() || text.size() > 5)
        return false;
    for (const QChar c : text) {
        if (!c.isDigit() || c.unicode() > 0x7f)
            return false;
    }
    const int port = text.toInt();
    return port >= 1 && port <= 65535;
}

/// A host name as DNS allows it: dot-separated labels of letters, digits and '-', not
/// starting or ending with '-'. An all-digit name that is not a valid IPv4 address (such
/// as `1.2.3`) is refused, since it is almost certainly a mistyped address.
bool isHostName(const QString &host)
{
    static const QRegularExpression label(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    if (host.isEmpty() || host.size() > 253)
        return false;
    const QStringList labels = host.split(QLatin1Char('.'));
    bool allDigits = true;
    for (const QString &part : labels) {
        if (!label.match(part).hasMatch())
            return false;
        for (const QChar c : part) {
            if (!c.isDigit())
                allDigits = false;
        }
    }
    if (allDigits) {
        // Four parts: QHostAddress also takes inet_aton's short forms ("1.2.3" is 1.2.0.3).
        QHostAddress address;
        return labels.size() == 4 && address.setAddress(host) &&
               address.protocol() == QHostAddress::IPv4Protocol;
    }
    return true;
}

PeerInputResult validate(const QStringList &entries, bool (*accept)(const QString &))
{
    PeerInputResult result;
    for (const QString &entry : entries) {
        if (!accept(entry)) {
            result.invalid = entry;
            return result;
        }
        result.entries.append(entry);
    }
    return result;
}

bool isTrackerUrl(const QString &entry)
{
    const QUrl url(entry, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
           scheme == QLatin1String("udp") || scheme == QLatin1String("ws") ||
           scheme == QLatin1String("wss");
}

} // namespace

bool isPeerAddress(const QString &entry)
{
    if (entry.startsWith(QLatin1Char('['))) {
        const qsizetype close = entry.indexOf(QLatin1String("]:"));
        if (close < 0)
            return false;
        QHostAddress address;
        return address.setAddress(entry.mid(1, close - 1)) &&
               address.protocol() == QHostAddress::IPv6Protocol &&
               isPort(QStringView(entry).mid(close + 2));
    }
    const qsizetype colon = entry.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    const QString host = entry.left(colon);
    // A bare IPv6 address has colons of its own: it needs the brackets to tell the port.
    if (host.contains(QLatin1Char(':')))
        return false;
    return isPort(QStringView(entry).mid(colon + 1)) && isHostName(host);
}

PeerInputResult trackerUrls(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("\\s+"));
    return validate(split(text, separators), isTrackerUrl);
}

PeerInputResult peers(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
    return validate(split(text, separators), isPeerAddress);
}

} // namespace omaseed::core::PeerInput
