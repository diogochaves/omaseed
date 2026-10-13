#include "core/TorrentProblem.hpp"

#include "core/Format.hpp"
#include "core/SharedData.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TorrentProblem", text);
}

/// Every "…" segment of `message`, in order. A quote left open runs to the end.
QStringList quotedSegments(const QString &message)
{
    QStringList segments;
    qsizetype open = -1;
    for (qsizetype i = 0; i < message.size(); ++i) {
        if (message.at(i) != QLatin1Char('"'))
            continue;
        if (open < 0) {
            open = i;
        } else {
            segments << message.mid(open + 1, i - open - 1);
            open = -1;
        }
    }
    if (open >= 0)
        segments << message.mid(open + 1);
    return segments;
}

/// libtorrent's head: 6 hex characters on 2.1, the torrent's name on 2.0.
void takeHead(const QString &head, ProblemLine &line)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{6}$"));
    if (hex.match(head).hasMatch())
        line.hashPrefix = head;
    else
        line.libtorrentName = head;
}

/// `file_error_alert::message()`: "<head> <operation> (<file>) error: <message>".
bool parseFileError(const QString &reason, ProblemLine &line)
{
    static const QRegularExpression re(QStringLiteral("^(.*?) ([a-z_]+) \\((.*)\\) error: (.*)$"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(reason);
    if (!m.hasMatch())
        return false;
    line.kind = ProblemLine::Kind::FileError;
    takeHead(m.captured(1), line);
    line.operation = m.captured(2);
    line.file = m.captured(3);
    line.systemMessage = m.captured(4).trimmed();
    return true;
}

/// `fastresume_rejected_alert::message()`: "<head> fast resume rejected. [<op>(<file>): ]<msg>".
bool parseRestoreFailed(const QString &reason, ProblemLine &line)
{
    static const QRegularExpression re(
        QStringLiteral("^(.*?) fast resume rejected\\.\\s*(?:([a-z_]+)\\((.*)\\): )?(.*)$"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(reason);
    if (!m.hasMatch())
        return false;
    line.kind = ProblemLine::Kind::RestoreFailed;
    takeHead(m.captured(1), line);
    line.operation = m.captured(2);
    line.file = m.captured(3);
    line.systemMessage = m.captured(4).trimmed();
    return true;
}

/// "Torrent errored. Torrent: "<name>". Error: "<text>"": the error code's text with no
/// libtorrent wrapper, so only the English line can be told apart from other warnings.
/// The upload-only variant wraps the text again ("Couldn't write to file. Reason: "…"…").
bool parseTorrentErrored(const QString &message, ProblemLine &line)
{
    static const QRegularExpression re(QStringLiteral("^Torrent errored\\. Torrent: \"(.*)\"\\. "
                                                      "Error: \"(.*)\"$"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(message);
    if (!m.hasMatch())
        return false;
    line.kind = ProblemLine::Kind::TorrentErrored;
    line.quoted = {m.captured(1)};
    QString error = m.captured(2);
    static const QRegularExpression wrapped(QStringLiteral("Reason: \"(.*?)\""));
    if (const QRegularExpressionMatch inner = wrapped.match(error); inner.hasMatch())
        error = inner.captured(1);
    line.systemMessage = error.trimmed();
    return true;
}

QString baseName(const QString &path)
{
    const QString p = SharedData::normalize(path);
    return p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
}

QString folderOf(const QString &path)
{
    const QString p = SharedData::normalize(path);
    const qsizetype slash = p.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? p.left(slash) : QString();
}

/// The hash libtorrent prints the prefix of: v2 when there is one, otherwise v1.
QString printedHash(const Torrent &torrent)
{
    if (!torrent.infohashV2.isEmpty())
        return torrent.infohashV2;
    if (!torrent.infohashV1.isEmpty())
        return torrent.infohashV1;
    return torrent.hash;
}

/// "opening", "writing"… for "Permission denied, opening x.bin".
QString verbFor(const QString &operation)
{
    if (operation == QLatin1String("file_open"))
        return tr("opening");
    if (operation == QLatin1String("file_write") || operation == QLatin1String("partfile_write"))
        return tr("writing");
    if (operation == QLatin1String("file_read") || operation == QLatin1String("partfile_read"))
        return tr("reading");
    if (operation == QLatin1String("mkdir"))
        return tr("creating the folder for");
    if (operation == QLatin1String("file_fallocate"))
        return tr("making room for");
    if (operation == QLatin1String("file_rename") || operation == QLatin1String("partfile_move"))
        return tr("moving");
    if (operation == QLatin1String("file_stat"))
        return tr("looking at");
    if (operation == QLatin1String("file_remove"))
        return tr("deleting");
    if (operation == QLatin1String("file_copy"))
        return tr("copying");
    if (operation == QLatin1String("file_hard_link") || operation == QLatin1String("symlink"))
        return tr("linking");
    if (operation == QLatin1String("file_truncate"))
        return tr("truncating");
    return QString();
}

bool isRead(const QString &operation)
{
    return operation == QLatin1String("file_read") || operation == QLatin1String("partfile_read") ||
           operation == QLatin1String("file_stat");
}

QString capitalised(QString text)
{
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

} // namespace

ProblemLine ProblemLine::parse(const QString &message)
{
    ProblemLine line;
    if (parseTorrentErrored(message, line))
        return line;

    const QStringList segments = quotedSegments(message);
    if (segments.size() < 2)
        return line;
    const QString &reason = segments.last();
    if (!parseFileError(reason, line) && !parseRestoreFailed(reason, line))
        return line;
    line.quoted = segments.mid(0, segments.size() - 1);
    return line;
}

QVariantMap TorrentProblem::toMap() const
{
    return {
        {QStringLiteral("found"), found},
        {QStringLiteral("missingFiles"), missingFiles},
        {QStringLiteral("headline"), headline},
        {QStringLiteral("cause"), cause},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("file"), file},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("loggedAtText"), Format::relativeTime(loggedAt)},
        {QStringLiteral("count"), count},
        {QStringLiteral("line"), line},
        {QStringLiteral("ambiguous"), ambiguous},
        {QStringLiteral("actions"), actions},
    };
}

namespace ProblemMatch {

bool isUnder(const QString &path, const QString &root)
{
    return !path.isEmpty() && !root.isEmpty() && SharedData::isUnder(path, root);
}

QStringList match(const ProblemLine &line, const QList<Torrent> &candidates, bool *ambiguous)
{
    if (ambiguous)
        *ambiguous = false;
    if (line.kind == ProblemLine::Kind::Unknown)
        return {};

    QList<const Torrent *> pool;
    for (const Torrent &torrent : candidates)
        pool << &torrent;

    if (!line.hashPrefix.isEmpty()) {
        QList<const Torrent *> byPrefix;
        for (const Torrent *torrent : std::as_const(pool)) {
            if (printedHash(*torrent).startsWith(line.hashPrefix, Qt::CaseInsensitive))
                byPrefix << torrent;
        }
        if (byPrefix.size() == 1)
            return {byPrefix.first()->hash};
        // None: a 2.0 name that happens to be six hex characters. Several: 24 bits clashed.
        if (!byPrefix.isEmpty())
            pool = byPrefix;
    }

    QList<const Torrent *> byName;
    for (const Torrent *torrent : std::as_const(pool)) {
        if (line.quoted.contains(torrent->name) ||
            (!line.libtorrentName.isEmpty() && torrent->name == line.libtorrentName))
            byName << torrent;
    }
    if (byName.size() <= 1)
        return byName.isEmpty() ? QStringList() : QStringList{byName.first()->hash};

    if (!line.file.isEmpty()) {
        QList<const Torrent *> byPath;
        for (const Torrent *torrent : std::as_const(byName)) {
            if (isUnder(line.file, torrent->savePath) ||
                isUnder(line.file, torrent->downloadPath) ||
                isUnder(line.file, torrent->contentPath))
                byPath << torrent;
        }
        if (byPath.size() == 1)
            return {byPath.first()->hash};
        if (!byPath.isEmpty())
            byName = byPath;
    }

    if (ambiguous)
        *ambiguous = true;
    QStringList hashes;
    for (const Torrent *torrent : std::as_const(byName))
        hashes << torrent->hash;
    return hashes;
}

TorrentProblem describe(const ProblemLine &line)
{
    TorrentProblem problem;
    problem.found = true;
    problem.missingFiles = line.kind == ProblemLine::Kind::RestoreFailed;
    problem.file = line.file;
    problem.folder = folderOf(line.file);

    const QString message = line.systemMessage;
    const QString lower = message.toLower();
    const QString verb = verbFor(line.operation);
    const QString name = baseName(line.file);
    problem.detail = verb.isEmpty() || name.isEmpty()
                         ? capitalised(message)
                         : tr("%1, %2 %3").arg(capitalised(message), verb, name);

    const QString location = QStringLiteral("location");
    const QString start = QStringLiteral("start");
    const QString recheck = QStringLiteral("recheck");
    const QString rename = QStringLiteral("rename");

    if (problem.missingFiles) {
        problem.headline = tr("Its files aren't where qBittorrent left them");
        problem.cause = tr("missing files");
        problem.detail =
            tr("Moved, deleted or changed since qBittorrent last ran (%1)").arg(message);
        problem.actions = {location, recheck};
    } else if (lower.contains(QLatin1String("permission denied")) ||
               lower.contains(QLatin1String("operation not permitted"))) {
        problem.headline = isRead(line.operation) ? tr("Can't read its files")
                                                  : tr("Can't write to its save folder");
        problem.cause = tr("no access");
        problem.actions = {location, start};
    } else if (lower.contains(QLatin1String("read-only file system"))) {
        problem.headline = tr("Its save folder is on a read-only disk");
        problem.cause = tr("read-only");
        problem.actions = {location, start};
    } else if (lower.contains(QLatin1String("no space left")) ||
               lower.contains(QLatin1String("disk quota exceeded"))) {
        problem.headline = tr("The disk is full");
        problem.cause = tr("disk full");
        problem.actions = {location, start};
    } else if (lower.contains(QLatin1String("no such file or directory"))) {
        problem.headline = tr("A file or folder it needs is gone");
        problem.cause = tr("missing");
        problem.actions = {location, recheck};
    } else if (lower.contains(QLatin1String("input/output error"))) {
        problem.headline =
            isRead(line.operation) ? tr("The disk failed a read") : tr("The disk failed a write");
        problem.cause = tr("disk error");
        problem.actions = {recheck, start};
    } else if (lower.contains(QLatin1String("file name too long"))) {
        problem.headline = tr("A file name is too long for this disk");
        problem.cause = tr("name too long");
        problem.actions = {rename, start};
    } else if (lower.contains(QLatin1String("invalid argument")) &&
               line.operation == QLatin1String("file_open")) {
        problem.headline = tr("A file name isn't allowed on this disk");
        problem.cause = tr("bad name");
        problem.actions = {rename, start};
    } else {
        problem.headline = capitalised(message);
        problem.cause = tr("error");
        problem.actions = {start, recheck};
    }
    return problem;
}

TorrentProblem notLogged(bool missingFiles)
{
    TorrentProblem problem;
    problem.missingFiles = missingFiles;
    problem.headline = tr("qBittorrent didn't log a reason");
    problem.cause = missingFiles ? tr("missing files") : tr("error");
    problem.actions = missingFiles
                          ? QStringList{QStringLiteral("location"), QStringLiteral("recheck")}
                          : QStringList{QStringLiteral("start"), QStringLiteral("recheck")};
    return problem;
}

} // namespace ProblemMatch

} // namespace omaseed::core
