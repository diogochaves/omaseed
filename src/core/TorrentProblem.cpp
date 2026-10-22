#include "core/TorrentProblem.hpp"

#include "core/Format.hpp"
#include "core/SharedData.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

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

bool isAbsolutePath(const QString &path)
{
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:[/\\\\]"));
    return path.startsWith(QLatin1Char('/')) || drive.match(path).hasMatch();
}

bool looksLikeUrl(const QString &text)
{
    static const QRegularExpression url(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://\\S+$"));
    return url.match(text).hasMatch();
}

/// `storage_moved_failed_alert::message()`: "<head> storage move failed. <op> (<file>):
/// <message>". The Source and Destination segments come before it, in the translation's
/// order; the source is the one libtorrent's file lies under (the deepest, when one folder
/// holds the other), so no English word decides which is which.
bool parseMoveFailed(const QStringList &segments, ProblemLine &line)
{
    static const QRegularExpression re(
        QStringLiteral("^(.*?) storage move failed\\.\\s*(?:([a-z_]+) \\((.*)\\): )?(.*)$"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(segments.last());
    if (!m.hasMatch())
        return false;
    line.kind = ProblemLine::Kind::MoveFailed;
    takeHead(m.captured(1), line);
    line.operation = m.captured(2);
    line.file = m.captured(3);
    line.systemMessage = m.captured(4).trimmed();
    line.quoted = segments.mid(0, segments.size() - 1);

    QStringList paths;
    for (const QString &segment : std::as_const(line.quoted)) {
        if (isAbsolutePath(segment))
            paths << segment;
    }
    qsizetype best = -1;
    for (qsizetype i = 0; i < paths.size(); ++i) {
        if (!line.file.isEmpty() && SharedData::isUnder(line.file, paths.at(i)) &&
            (best < 0 || paths.at(i).size() > paths.at(best).size()))
            best = i;
    }
    if (best < 0 && !paths.isEmpty())
        best = 0;
    if (best >= 0) {
        line.source = paths.at(best);
        if (paths.size() > 1)
            line.destination = paths.at(best == 0 ? 1 : 0);
    }
    return true;
}

/// `url_seed_alert::message()`: "<head> url seed (<url>) <message>", where libtorrent's
/// message starts "failed: " when it could not connect.
bool parseUrlSeedConnection(const QStringList &segments, ProblemLine &line)
{
    static const QRegularExpression re(QStringLiteral("^(.*?) url seed \\((.*?)\\) (.*)$"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = re.match(segments.last());
    if (!m.hasMatch())
        return false;
    line.kind = ProblemLine::Kind::UrlSeedFailed;
    takeHead(m.captured(1), line);
    line.url = m.captured(2);
    QString message = m.captured(3).trimmed();
    if (message.startsWith(QLatin1String("failed: ")))
        message = message.mid(8);
    line.systemMessage = message;
    line.quoted = segments.mid(0, segments.size() - 1);
    return true;
}

/// The lines with no libtorrent text, by their shape (docs/32 §2): the torrent's name comes
/// first and qBittorrent's or the system's message last.
void parseByShape(const QString &message, const QStringList &segments, ProblemLine &line)
{
    const QString &name = segments.first();
    if (segments.size() == 3) {
        const QString &middle = segments.at(1);
        if (looksLikeUrl(middle)) {
            line.kind = ProblemLine::Kind::UrlSeedFailed;
            line.url = middle;
        } else if (isAbsolutePath(middle) &&
                   middle.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive)) {
            line.kind = ProblemLine::Kind::ExportFailed;
            line.file = middle;
        } else if (message.startsWith(QLatin1String("File rename failed.")) ||
                   (!isAbsolutePath(middle) &&
                    (middle == name || middle.startsWith(name + QLatin1Char('/'))))) {
            line.kind = ProblemLine::Kind::RenameFailed;
            line.torrentFile = middle;
        } else {
            return;
        }
        line.quoted = {name};
        line.systemMessage = segments.at(2).trimmed();
        return;
    }
    if (segments.size() == 2) {
        if (message.startsWith(QLatin1String("Failed to remove partfile.")))
            line.kind = ProblemLine::Kind::PartfileFailed;
        else if (message.startsWith(QLatin1String("Failed to remove torrent content.")))
            line.kind = ProblemLine::Kind::ContentRemovalFailed;
        else
            return;
        line.quoted = {name};
        line.systemMessage = segments.at(1).trimmed();
    }
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
    if (parseFileError(reason, line) || parseRestoreFailed(reason, line)) {
        line.quoted = segments.mid(0, segments.size() - 1);
        return line;
    }
    if (parseMoveFailed(segments, line) || parseUrlSeedConnection(segments, line))
        return line;
    parseByShape(message, segments, line);
    return line;
}

QVariantMap TorrentProblem::toMap() const
{
    return {
        {QStringLiteral("found"), found},
        {QStringLiteral("missingFiles"), missingFiles},
        {QStringLiteral("headline"), headline},
        {QStringLiteral("cause"), cause},
        {QStringLiteral("causeKey"), causeKey},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("advice"), advice},
        {QStringLiteral("file"), file},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("loggedAtText"), Format::relativeTime(loggedAt)},
        {QStringLiteral("count"), count},
        {QStringLiteral("line"), line},
        {QStringLiteral("ambiguous"), ambiguous},
        {QStringLiteral("actions"), actions},
        {QStringLiteral("stuckMove"), stuckMove},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("source"), source},
    };
}

QVariantMap TorrentFailure::toMap() const
{
    if (isEmpty())
        return {};
    return {
        {QStringLiteral("found"), true},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("headline"), headline},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("torrentFile"), torrentFile},
        {QStringLiteral("loggedAtText"), Format::relativeTime(loggedAt)},
        {QStringLiteral("count"), count},
        {QStringLiteral("line"), line},
        {QStringLiteral("ambiguous"), ambiguous},
        {QStringLiteral("actions"), actions},
    };
}

namespace ProblemMatch {

QStringList quotedSegments(const QString &message)
{
    return ::omaseed::core::quotedSegments(message);
}

QString hashPrefix(const Torrent &torrent)
{
    const QString &hash = !torrent.infohashV2.isEmpty()   ? torrent.infohashV2
                          : !torrent.infohashV1.isEmpty() ? torrent.infohashV1
                                                          : torrent.hash;
    return hash.left(6).toLower();
}

QString lastTwoFolders(const QString &path)
{
    const QStringList parts =
        SharedData::normalize(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.mid(std::max<qsizetype>(0, parts.size() - 2)).join(QLatin1Char('/'));
}

TorrentFailure describeMove(const ProblemLine &line)
{
    TorrentFailure failure;
    failure.kind = QStringLiteral("move");
    const QString where = lastTwoFolders(line.destination);
    failure.headline = where.isEmpty() ? tr("The move didn't happen")
                                       : tr("The move to %1 didn't happen").arg(where);
    const QString verb = verbFor(line.operation);
    const QString name = baseName(line.file);
    failure.detail = verb.isEmpty() || name.isEmpty()
                         ? capitalised(line.systemMessage)
                         : tr("%1, %2 %3").arg(capitalised(line.systemMessage), verb, name);
    if (!line.source.isEmpty())
        failure.folder = tr("Its files are still in %1").arg(SharedData::normalize(line.source));
    failure.destination = line.destination;
    failure.actions = {QStringLiteral("location"), QStringLiteral("folder")};
    return failure;
}

TorrentFailure describeRename(const QList<ProblemLine> &lines, const QString &savePath)
{
    TorrentFailure failure;
    if (lines.isEmpty())
        return failure;
    failure.kind = QStringLiteral("rename");
    const ProblemLine &first = lines.first();
    const qsizetype more = lines.size() - 1;
    failure.headline = more == 0 ? tr("A file kept its old name")
                                 : tr("%1 files kept their old names").arg(lines.size());
    failure.detail =
        tr("%1, renaming %2").arg(capitalised(lines.last().systemMessage), first.torrentFile);
    if (more > 0)
        failure.detail += tr(" and %1 more").arg(more);
    failure.torrentFile = first.torrentFile;
    const qsizetype slash = first.torrentFile.lastIndexOf(QLatin1Char('/'));
    const QString root = SharedData::normalize(savePath);
    failure.folder = slash > 0 && !root.isEmpty()
                         ? root + QLatin1Char('/') + first.torrentFile.left(slash)
                         : root;
    failure.actions = {QStringLiteral("rename"), QStringLiteral("folder")};
    return failure;
}

TorrentFailure describePartfile(const ProblemLine &line, const QString &savePath)
{
    TorrentFailure failure;
    failure.kind = QStringLiteral("partfile");
    failure.headline = tr("Its part file couldn't be removed");
    failure.detail = capitalised(line.systemMessage);
    const QString root = SharedData::normalize(savePath);
    failure.folder = root.isEmpty()
                         ? tr("the .parts file holds pieces of files you skipped")
                         : tr("%1 · the .parts file holds pieces of files you skipped").arg(root);
    failure.actions = {QStringLiteral("folder")};
    return failure;
}

QString webSeedStatusText(const ProblemLine &line)
{
    QString text = line.systemMessage.trimmed();
    // libtorrent's "Host not found (authoritative)": the resolver's aside says nothing here.
    static const QRegularExpression aside(QStringLiteral("\\s*\\([^()]*\\)$"));
    if (line.hashPrefix.size() + line.libtorrentName.size() > 0)
        text.remove(aside);
    // "Host not found" -> "host not found", but "HTTP error" and "404 …" keep their case.
    if (text.size() > 1 && text.at(0).isUpper() && text.at(1).isLower())
        text[0] = text.at(0).toLower();
    return text;
}

bool isWebSeedUrl(const QString &lineUrl, const QString &seedUrl)
{
    if (lineUrl.isEmpty() || seedUrl.isEmpty())
        return false;
    return lineUrl == seedUrl ||
           (seedUrl.endsWith(QLatin1Char('/')) && lineUrl.startsWith(seedUrl));
}

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

QString nameTheDiskRejects(const QString &path)
{
    static const QString forbidden = QStringLiteral("<>:\"\\|?*");
    static const QRegularExpression reserved(
        QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:$"));
    const QStringList parts = SharedData::normalize(path).split(QLatin1Char('/'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QString &part = parts.at(i);
        // A Windows server's "C:" is a drive, not a name.
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..") ||
            (i == 0 && drive.match(part).hasMatch()))
            continue;
        const bool badChar = std::any_of(part.cbegin(), part.cend(), [](QChar c) {
            return c.unicode() < 0x20 || forbidden.contains(c);
        });
        const QString stem = part.left(part.indexOf(QLatin1Char('.'))).trimmed();
        if (badChar || part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) ||
            reserved.match(stem).hasMatch() || part.toUtf8().size() > 255)
            return part;
    }
    return QString();
}

bool isStuckMove(const Torrent &torrent)
{
    if (torrent.progress < 1.0 || torrent.status.phase == TorrentPhase::Moving)
        return false;
    if (torrent.downloadPath.isEmpty() || torrent.contentPath.isEmpty())
        return false;
    // A torrent saved into its own incomplete folder never had anywhere to go.
    if (SharedData::normalize(torrent.downloadPath) == SharedData::normalize(torrent.savePath))
        return false;
    return isUnder(torrent.contentPath, torrent.downloadPath) &&
           !isUnder(torrent.contentPath, torrent.savePath);
}

TorrentProblem describeStuckMove(const Torrent &torrent, const ProblemLine *line)
{
    TorrentProblem problem;
    problem.stuckMove = true;
    problem.found = line != nullptr;
    problem.causeKey = QStringLiteral("stuckMove");
    problem.cause = tr("move failed");
    problem.destination = SharedData::normalize(torrent.savePath);
    problem.source = SharedData::normalize(torrent.downloadPath);
    problem.headline = tr("Finished, but its files never reached %1").arg(problem.destination);
    // The reason names the source file, not the full destination (#117): the system's words
    // alone say it.
    // Board 44 M1's order: why, where it seeds from, then the partial copies (`advice`).
    const QString why = line && !line->systemMessage.isEmpty()
                            ? tr("qBittorrent couldn't move them: %1.").arg(line->systemMessage)
                            : tr("Its files are still in its incomplete folder.");
    problem.detail = why + QLatin1Char(' ') + tr("It's seeding from %1.").arg(problem.source);
    problem.advice = tr("Partial copies may be left in the destination.");
    problem.actions = {QStringLiteral("bothFolders"), QStringLiteral("location")};
    return problem;
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
        problem.causeKey = QStringLiteral("missingFiles");
        problem.cause = tr("missing files");
        problem.detail =
            tr("Moved, deleted or changed since qBittorrent last ran (%1)").arg(message);
        problem.actions = {location, recheck};
    } else if (lower.contains(QLatin1String("permission denied")) ||
               lower.contains(QLatin1String("operation not permitted"))) {
        problem.headline = isRead(line.operation) ? tr("Can't read its files")
                                                  : tr("Can't write to its save folder");
        problem.causeKey = QStringLiteral("noAccess");
        problem.cause = tr("no access");
        problem.actions = {location, start};
    } else if (lower.contains(QLatin1String("read-only file system"))) {
        problem.headline = tr("Its save folder is on a read-only disk");
        problem.causeKey = QStringLiteral("readOnly");
        problem.cause = tr("read-only");
        problem.actions = {location, start};
    } else if (lower.contains(QLatin1String("no space left")) ||
               lower.contains(QLatin1String("disk quota exceeded"))) {
        problem.headline = tr("The disk is full");
        problem.causeKey = QStringLiteral("diskFull");
        problem.cause = tr("disk full");
        problem.actions = {location, start};
    } else if (lower.contains(QLatin1String("no such file or directory"))) {
        problem.headline = tr("A file or folder it needs is gone");
        problem.causeKey = QStringLiteral("missing");
        problem.cause = tr("missing");
        problem.actions = {location, recheck};
    } else if (lower.contains(QLatin1String("input/output error"))) {
        problem.headline =
            isRead(line.operation) ? tr("The disk failed a read") : tr("The disk failed a write");
        problem.causeKey = QStringLiteral("diskError");
        problem.cause = tr("disk error");
        problem.actions = {recheck, start};
    } else if (lower.contains(QLatin1String("file name too long"))) {
        problem.headline = tr("A file name is too long for this disk");
        problem.causeKey = QStringLiteral("nameTooLong");
        problem.cause = tr("name too long");
        problem.actions = {rename, start};
    } else if (lower.contains(QLatin1String("invalid argument")) &&
               line.operation == QLatin1String("file_open") &&
               !nameTheDiskRejects(line.file).isEmpty()) {
        problem.headline = tr("A file name isn't allowed on this disk");
        problem.causeKey = QStringLiteral("badName");
        problem.cause = tr("bad name");
        problem.actions = {rename, start};
    } else if (lower.contains(QLatin1String("invalid argument")) &&
               line.operation == QLatin1String("file_open")) {
        // #108: a name every common disk takes, refused all the same. Seen with damaged NTFS
        // records on a full volume: renaming can't help, and each retry writes to a disk that
        // may be failing, so the disk is checked first. Nothing here knows the filesystem, so
        // the advice names no tool.
        problem.headline = tr("The disk refused to open a file");
        problem.causeKey = QStringLiteral("openRefused");
        problem.cause = tr("can't open");
        problem.advice = tr("The file or the disk may be damaged.");
        problem.actions = {QStringLiteral("checkDisk"), start};
    } else {
        problem.headline = capitalised(message);
        problem.causeKey = QStringLiteral("other");
        problem.cause = tr("error");
        problem.actions = {start, recheck};
    }
    return problem;
}

TorrentProblem hybridRecheck()
{
    TorrentProblem problem;
    problem.headline = tr("libtorrent can't check this hybrid torrent");
    problem.causeKey = QStringLiteral("hybridRecheck");
    problem.cause = tr("hybrid error");
    problem.detail = tr("torrent_inconsistent_hashes: its v1 and v2 hashes don't describe the "
                        "same data once a downloaded file is missing");
    problem.advice = tr("qBittorrent logs nothing for it, and starting it or restarting "
                        "qBittorrent brings it back. Adding it again clears it, at the cost of "
                        "its ratio, seeding time and added date.");
    problem.actions = {QStringLiteral("addAgain")};
    return problem;
}

TorrentProblem notLogged(bool missingFiles)
{
    TorrentProblem problem;
    problem.missingFiles = missingFiles;
    problem.headline = tr("qBittorrent didn't log a reason");
    problem.causeKey = QStringLiteral("notLogged");
    problem.cause = missingFiles ? tr("missing files") : tr("error");
    problem.actions = missingFiles
                          ? QStringList{QStringLiteral("location"), QStringLiteral("recheck")}
                          : QStringList{QStringLiteral("start"), QStringLiteral("recheck")};
    return problem;
}

} // namespace ProblemMatch

} // namespace omaseed::core
