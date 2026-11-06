#include "core/AnnounceMask.hpp"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace omaseed::core::AnnounceMask {

namespace {

/// Path words a tracker's URL shows openly, never a key.
bool isOpenWord(const QString &segment)
{
    const QString word = segment.toLower();
    return word == QLatin1String("announce") || word == QLatin1String("announce.php") ||
           word == QLatin1String("scrape") || word == QLatin1String("scrape.php");
}

} // namespace

QString dots()
{
    return QString(8, QChar(0x2022));
}

QString maskUrl(const QString &url)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty() || parsed.scheme().isEmpty())
        return dots();
    QString out = parsed.scheme() + QStringLiteral("://") + parsed.host();
    if (parsed.port() >= 0)
        out += QLatin1Char(':') + QString::number(parsed.port());
    // The path's encoded form: a percent-escaped key stays one segment, as it was given.
    const QStringList segments =
        parsed.path(QUrl::FullyEncoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    bool lastHidden = false;
    for (const QString &segment : segments) {
        if (isOpenWord(segment)) {
            out += QLatin1Char('/') + segment;
            lastHidden = false;
        } else if (!lastHidden) {
            out += QLatin1Char('/') + dots();
            lastHidden = true;
        }
    }
    if (parsed.hasQuery())
        out += QLatin1Char('?') + dots();
    return out;
}

QString maskText(const QString &text)
{
    static const QRegularExpression url(QStringLiteral(R"((?:https?|udp|wss?)://[^\s"'<>|]+)"),
                                        QRegularExpression::CaseInsensitiveOption);
    QString out;
    qsizetype at = 0;
    QRegularExpressionMatchIterator matches = url.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        out += text.mid(at, match.capturedStart() - at);
        out += maskUrl(match.captured());
        at = match.capturedEnd();
    }
    out += text.mid(at);
    return out;
}

} // namespace omaseed::core::AnnounceMask
