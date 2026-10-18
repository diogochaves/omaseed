#include "core/ErrorWords.hpp"

#include "core/Format.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ErrorWords", text);
}

/// The name a headline gives the server: its name, else its address, else "the server".
QString serverName(const ErrorWords::Subject &subject)
{
    if (!subject.server.isEmpty())
        return subject.server;
    if (!subject.address.isEmpty())
        return subject.address;
    return tr("the server");
}

QString torrentName(const ErrorWords::Subject &subject)
{
    return subject.torrent.isEmpty() ? tr("the torrent") : subject.torrent;
}

QString addressOf(const ErrorWords::Subject &subject)
{
    return subject.address.isEmpty() ? tr("its address") : subject.address;
}

/// The text after the request: "Connection refused" of "GET sync/maindata: Connection refused".
QString reasonOf(const ApiError &error)
{
    const QString message = error.message();
    const QString request = ErrorWords::request(error);
    QString reason = message;
    if (!request.isEmpty() && message.startsWith(request)) {
        reason = message.mid(request.size());
        if (reason.startsWith(QLatin1String(": ")))
            reason = reason.mid(2);
        else if (reason.startsWith(QLatin1Char(':')))
            reason = reason.mid(1).trimmed();
    }
    return reason.trimmed();
}

/// qBittorrent's (or Qt's) own words, without Qt's "Error transferring URL - " in front.
QString ownText(const ApiError &error)
{
    QString reason = reasonOf(error);
    static const QRegularExpression transferring(QStringLiteral("^Error transferring \\S+ - "));
    reason.remove(transferring);
    return reason;
}

bool hostNotFound(const ApiError &error)
{
    static const QRegularExpression notFound(QStringLiteral("\\bhost\\b.*\\bnot found\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    return notFound.match(error.message()).hasMatch();
}

/// "no reply within 8000 ms" → 8; -1 when the message has no figure.
int timeoutSeconds(const ApiError &error)
{
    static const QRegularExpression within(QStringLiteral("within (\\d+) ms"));
    const QRegularExpressionMatch match = within.match(error.message());
    if (!match.hasMatch())
        return -1;
    return int(std::lround(match.captured(1).toDouble() / 1000.0));
}

QString joined(const QString &first, const QString &second)
{
    if (first.isEmpty())
        return second;
    if (second.isEmpty())
        return first;
    return first + QLatin1Char(' ') + second;
}

bool isStatus(const ApiError &error, int status)
{
    return error.httpStatus() == status;
}

/// A 403 from setLocation that is about the folder (qBittorrent's "Cannot write to
/// directory"), not the bare "Forbidden" of a session it doesn't know, nor a sign-in omaseed
/// held back (QbtClient keeps the body's text for setLocation's 403).
bool isFolderRefusal(const ApiError &error)
{
    if (error.kind() != ApiError::Kind::Forbidden)
        return false;
    const QString own = ownText(error);
    return !own.isEmpty() && !own.contains(QLatin1String("Forbidden"), Qt::CaseInsensitive) &&
           !own.contains(QLatin1String("sign-in"));
}

QString cantWrite(const QString &folder)
{
    return folder.isEmpty() ? tr("qBittorrent can't write to that folder")
                            : tr("qBittorrent can't write to %1").arg(folder);
}

} // namespace

QString ErrorWords::Text::sentence() const
{
    if (line.isEmpty())
        return headline;
    // The line's first sentence, run on after the headline: "…wasn't moved: qBittorrent can't
    // create or write /proc/omaseed".
    QString first = line;
    const qsizetype stop = first.indexOf(QStringLiteral(". "));
    if (stop >= 0)
        first = first.left(stop);
    if (first.endsWith(QLatin1Char('.')))
        first.chop(1);
    // "Nothing is listening" reads on as "nothing is listening"; "API", "ADDRESS" or a name
    // keep their capitals.
    if (first.size() > 1 && first.at(0).isUpper() && first.at(1).isLower())
        first.replace(0, 1, first.at(0).toLower());
    return QStringLiteral("%1: %2").arg(headline, first);
}

QVariantMap ErrorWords::Text::toMap() const
{
    return {
        {QStringLiteral("headline"), headline},
        {QStringLiteral("line"), line},
        {QStringLiteral("details"), details},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("sentence"), sentence()},
    };
}

bool ErrorWords::isRefusedSignIn(const ApiError &error, AuthMode mode)
{
    if (error.kind() == ApiError::Kind::Unauthorized)
        return true;
    // qBittorrent 5.2 answers a bad Bearer key with 403, and a server that wants a login
    // answers 403 to a request with none; to a cookie login, 403 is a ban.
    return error.kind() == ApiError::Kind::Forbidden && mode != AuthMode::Cookie;
}

QString ErrorWords::retryLine(int seconds)
{
    if (seconds < 0)
        return {};
    if (seconds == 0)
        return tr("omaseed tries again now.");
    return tr("omaseed tries again in %1 s.").arg(seconds);
}

QString ErrorWords::request(const ApiError &error)
{
    // "GET sync/maindata: …", "POST torrents/setLocation: …", "auth/login: …".
    static const QRegularExpression verbPath(
        QStringLiteral("^((?:GET|POST|PUT|DELETE) [A-Za-z0-9_/.-]+|auth/login)"));
    const QRegularExpressionMatch match = verbPath.match(error.message());
    return match.hasMatch() ? match.captured(1) : QString();
}

QString ErrorWords::details(const ApiError &error, const QString &address)
{
    if (!error.isError())
        return {};
    QString kind = ApiError::kindName(error.kind());
    if (error.kind() == ApiError::Kind::Network)
        kind = tr("network error");
    if (error.httpStatus() != 0)
        kind += QStringLiteral(" (%1)").arg(error.httpStatus());
    QStringList parts{kind};
    const QString req = request(error);
    if (!req.isEmpty())
        parts << req;
    const QString own = ownText(error);
    if (!own.isEmpty())
        parts << own;
    if (!address.isEmpty())
        parts << address;
    return parts.join(QStringLiteral(" · "));
}

QString ErrorWords::shortReason(const ApiError &error)
{
    switch (error.kind()) {
    case ApiError::Kind::None:
    case ApiError::Kind::Cancelled:
        return {};
    case ApiError::Kind::Network:
        return hostNotFound(error) ? tr("not found") : tr("didn't answer");
    case ApiError::Kind::Timeout:
        return tr("too slow");
    case ApiError::Kind::Certificate:
        return tr("certificate refused");
    case ApiError::Kind::Unauthorized:
        return tr("sign-in refused");
    case ApiError::Kind::Forbidden:
        // Without the auth mode a 403 is read as a refusal: the server said no to omaseed.
        return tr("sign-in refused");
    case ApiError::Kind::Server:
        return tr("internal error");
    case ApiError::Kind::Parse:
        return tr("unreadable answer");
    default:
        break;
    }
    return tr("error");
}

QString ErrorWords::guard(const QString &server, const QString &key, bool refused)
{
    const QString name = server.isEmpty() ? tr("the server") : server;
    if (refused)
        return tr("%1 refused the sign-in: %2 waits until the login is fixed").arg(name, key);
    return tr("%1 is unreachable: %2 waits until it answers").arg(name, key);
}

ErrorWords::Text ErrorWords::forServer(const ApiError &error, const Subject &subject)
{
    Text text;
    text.details = details(error, subject.address);
    const QString name = serverName(subject);
    const QString retry = retryLine(subject.retrySeconds);

    switch (error.kind()) {
    case ApiError::Kind::None:
    case ApiError::Kind::Cancelled:
        return {};
    case ApiError::Kind::Network:
        text.kind = QStringLiteral("down");
        if (hostNotFound(error)) {
            text.headline = tr("%1 couldn't be found").arg(name);
            text.line =
                joined(tr("%1 doesn't resolve on this computer.").arg(addressOf(subject)), retry);
        } else {
            text.headline = tr("%1 didn't answer").arg(name);
            text.line = joined(tr("Nothing is listening at %1.").arg(addressOf(subject)), retry);
        }
        return text;
    case ApiError::Kind::Timeout: {
        text.kind = QStringLiteral("down");
        text.headline = tr("%1 is taking too long").arg(name);
        const int seconds = timeoutSeconds(error);
        text.line = joined(
            seconds > 0 ? tr("No answer in %1 s.").arg(seconds) : tr("No answer in time."), retry);
        return text;
    }
    case ApiError::Kind::Certificate:
        text.kind = QStringLiteral("certificate");
        // Q27's words, as built (certificateRefusedText).
        text.headline = subject.pinnedCertificate.isEmpty()
                            ? tr("%1's certificate isn't trusted").arg(name)
                            : tr("%1's certificate changed").arg(name);
        text.line = tr("Review it in Settings › Servers before omaseed trusts it.");
        return text;
    case ApiError::Kind::Unauthorized:
    case ApiError::Kind::Forbidden:
        if (isRefusedSignIn(error, subject.authMode)) {
            text.kind = QStringLiteral("refused");
            switch (subject.authMode) {
            case AuthMode::Cookie:
                text.headline = tr("%1 refused the sign-in").arg(name);
                text.line = tr("The username or password omaseed has for it is wrong. It won't "
                               "try again until you change them.");
                break;
            case AuthMode::ApiKey:
                text.headline = tr("%1 refused the sign-in").arg(name);
                text.line = tr("The API key omaseed has for it is wrong. It won't try again "
                               "until you change it.");
                break;
            case AuthMode::None:
                text.headline = tr("%1 asks for a login").arg(name);
                text.line = tr("omaseed has none for it. Add one in Settings › Servers.");
                break;
            }
            return text;
        }
        text.kind = QStringLiteral("banned");
        text.headline = tr("%1 refused omaseed").arg(name);
        text.line = tr("Too many failed sign-ins, or this computer's address is banned in its "
                       "Web UI settings.");
        return text;
    case ApiError::Kind::Server:
        text.kind = QStringLiteral("server");
        text.headline = tr("%1 had an internal error").arg(name);
        text.line =
            error.httpStatus() > 0
                ? tr("qBittorrent answered %1; its log may say why.").arg(error.httpStatus())
                : tr("Its log may say why.");
        return text;
    case ApiError::Kind::Parse:
        text.kind = QStringLiteral("parse");
        text.headline = tr("%1 answered something omaseed can't read").arg(name);
        text.line = tr("Is %1 a qBittorrent Web UI?").arg(addressOf(subject));
        return text;
    default:
        break;
    }
    text.kind = QStringLiteral("request");
    const QString own = ownText(error);
    text.headline =
        own.isEmpty() ? tr("%1 refused").arg(name) : tr("%1 refused: %2").arg(name, own);
    const QString req = request(error);
    if (!req.isEmpty())
        text.line = tr("omaseed asked for %1.").arg(req);
    return text;
}

ErrorWords::Text ErrorWords::forRequest(const ApiError &error, const Subject &subject)
{
    if (!error.isError() || error.kind() == ApiError::Kind::Cancelled)
        return {};
    const QString req = request(error);
    const bool add = req.endsWith(QLatin1String("torrents/add"));

    // setLocation (C15 M19, board 42 §8): what qBittorrent refused is the folder, never the
    // sign-in.
    if (req.endsWith(QLatin1String("torrents/setLocation"))) {
        Text text;
        text.kind = QStringLiteral("request");
        text.details = details(error, subject.address);
        if (isStatus(error, 400)) {
            text.headline = tr("No folder given");
            text.line = tr("Type the folder's new path.");
            return text;
        }
        if (error.kind() == ApiError::Kind::Conflict) {
            text.headline = subject.folder.isEmpty()
                                ? tr("qBittorrent can't create that folder")
                                : tr("qBittorrent can't create %1").arg(subject.folder);
            text.line = tr("Check that the drive is mounted and its parent folder exists.");
            return text;
        }
        if (isFolderRefusal(error)) {
            text.headline = cantWrite(subject.folder);
            text.line = tr("Its user has no write access there. This is not about the sign-in.");
            return text;
        }
    }
    // 415 on add: the .torrent itself (C9).
    if (add && isStatus(error, 415)) {
        Text text;
        text.kind = QStringLiteral("request");
        text.details = details(error, subject.address);
        text.headline =
            tr("qBittorrent couldn't read the .torrent of %1").arg(torrentName(subject));
        return text;
    }
    // An add the server stopped answering in the middle of (docs/33 N3).
    if (add &&
        (error.kind() == ApiError::Kind::Network || error.kind() == ApiError::Kind::Timeout)) {
        Text text;
        text.kind = QStringLiteral("down");
        text.details = details(error, subject.address);
        const QString name = serverName(subject);
        text.headline = tr("%1 didn't take the torrent").arg(name);
        text.line = tr("It stopped answering. Nothing was added; what you typed is kept, and "
                       "“add when back” sends it as soon as %1 answers.")
                        .arg(name);
        return text;
    }
    if (error.kind() == ApiError::Kind::NotFound && !subject.torrent.isEmpty()) {
        Text text;
        text.kind = QStringLiteral("request");
        text.details = details(error, subject.address);
        text.headline = tr("%1 isn't on %2 any more").arg(subject.torrent, serverName(subject));
        return text;
    }
    if (error.kind() == ApiError::Kind::Conflict || error.kind() == ApiError::Kind::NotFound ||
        error.kind() == ApiError::Kind::Unknown) {
        Text text;
        text.kind = QStringLiteral("request");
        text.details = details(error, subject.address);
        const QString own = ownText(error);
        const QString name = serverName(subject);
        text.headline =
            own.isEmpty() ? tr("%1 refused").arg(name) : tr("%1 refused: %2").arg(name, own);
        return text;
    }
    // A server's problem met by an action: the server's words, without a retry figure.
    Subject plain = subject;
    plain.retrySeconds = -1;
    return forServer(error, plain);
}

ErrorWords::Text ErrorWords::forMoveFailure(const QString &torrent,
                                            const QString &folder,
                                            const QString &drive,
                                            const QString &causeKey,
                                            const QString &reason)
{
    Text text;
    text.kind = QStringLiteral("request");
    text.details = reason;
    const QString name = torrent.isEmpty() ? tr("the torrent") : torrent;
    if (causeKey == QLatin1String("noAccess") || causeKey == QLatin1String("readOnly")) {
        // T2: answered 200, then "Permission denied" in the log.
        text.headline = cantWrite(folder);
        text.line = tr("%1 didn't move: “%2”.").arg(name, reason);
        return text;
    }
    if (causeKey == QLatin1String("diskFull")) {
        const QString where = drive.isEmpty() ? folder : drive;
        text.headline = where.isEmpty() ? tr("The drive is full") : tr("%1 is full").arg(where);
        text.line = tr("%1 didn't move. The move is paused.").arg(name);
        return text;
    }
    if (causeKey == QLatin1String("missing")) {
        // libtorrent names the source file even when the destination is what went away, so
        // the words don't guess which side: a drive unmounted mid-move, on either side.
        text.headline = folder.isEmpty() ? tr("qBittorrent can't reach that folder")
                                         : tr("qBittorrent can't reach %1").arg(folder);
        text.line =
            tr("%1 didn't move: “%2”. Check that both drives are mounted.").arg(name, reason);
        return text;
    }
    text.headline = tr("%1 didn't move").arg(name);
    text.line = reason.isEmpty() ? QString() : tr("“%1”.").arg(reason);
    return text;
}

ErrorWords::Text
ErrorWords::forNearlyFull(const QString &torrent, const QString &drive, qint64 freeAfter)
{
    Text text;
    text.kind = QStringLiteral("request");
    text.headline =
        drive.isEmpty() ? tr("The drive is nearly full") : tr("%1 is nearly full").arg(drive);
    text.line = tr("%1 would leave %2 free, under what you keep free. The move is paused.")
                    .arg(torrent.isEmpty() ? tr("The next torrent") : torrent,
                         Format::size(std::max<qint64>(0, freeAfter)));
    return text;
}

ErrorWords::Text ErrorWords::forStuck(const QString &torrent,
                                      std::chrono::seconds moving,
                                      const QDateTime &lastLanded,
                                      std::chrono::seconds expected)
{
    Text text;
    text.kind = QStringLiteral("request");
    text.headline = tr("qBittorrent stopped moving");
    const QString name = torrent.isEmpty() ? tr("A torrent") : torrent;
    const qint64 minutes = std::max<qint64>(1, moving.count() / 60);
    QString first = tr("%1 has been moving for %2 min").arg(name).arg(minutes);
    if (lastLanded.isValid())
        first = tr("%1 and nothing has landed since %2")
                    .arg(first, lastLanded.toLocalTime().toString(QStringLiteral("HH:mm")));
    QString second;
    if (expected.count() > 0)
        second = tr(" (at this move's speed it needs %1)").arg(Format::eta(expected.count()));
    text.line = tr("%1%2. Restarting qBittorrent usually frees it.").arg(first, second);
    return text;
}

ErrorWords::Text ErrorWords::forLeftOut(int count, const QString &folder)
{
    Text text;
    text.kind = QStringLiteral("request");
    text.headline = tr("%1 left out").arg(count);
    text.line = tr("%1 already has a file with their file's name. qBittorrent would keep that "
                   "file and report the move as done.")
                    .arg(folder.isEmpty() ? tr("The new folder") : folder);
    return text;
}

} // namespace omaseed::core
