#include "core/ProblemLog.hpp"

#include "core/QbtClient.hpp"
#include "core/SharedData.hpp"
#include "core/TorrentModel.hpp"

#include <QCryptographicHash>
#include <QJsonObject>

#include <algorithm>
#include <optional>

namespace omaseed::core {

namespace {

/// Enough for every line about a torrent's files in a busy session: qBittorrent logs one per
/// torrent per error, not one per piece.
constexpr qsizetype kMaxLines = 2000;

bool isErroredState(TorrentState state)
{
    return state == TorrentState::Error || state == TorrentState::MissingFiles;
}

bool isCheckingState(TorrentState state)
{
    return state == TorrentState::CheckingDownload || state == TorrentState::CheckingUpload ||
           state == TorrentState::CheckingResumeData;
}

/// How soon after a check an error counts as the check's (C17 D3): a big torrent's check runs
/// long, and the polls may not see it at all.
constexpr qint64 kAfterCheckSeconds = 600;

/// The hash libtorrent prints the prefix of: v2 when there is one, otherwise v1.
QString printedHash(const Torrent &torrent)
{
    if (!torrent.infohashV2.isEmpty())
        return torrent.infohashV2;
    if (!torrent.infohashV1.isEmpty())
        return torrent.infohashV1;
    return torrent.hash;
}

bool samePath(const QString &a, const QString &b)
{
    return !a.isEmpty() && !b.isEmpty() && SharedData::normalize(a) == SharedData::normalize(b);
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

bool isStanding(ProblemLine::Kind kind)
{
    return kind == ProblemLine::Kind::MoveFailed || kind == ProblemLine::Kind::RenameFailed ||
           kind == ProblemLine::Kind::PartfileFailed;
}

} // namespace

ProblemLog::ProblemLog(QObject *parent)
    : QObject(parent)
{}

void ProblemLog::setClient(QbtClient *client)
{
    if (m_client == client)
        return;
    m_client = client;
    m_seenFirstSync = false;
    m_errored.clear();
    m_newlyErrored.clear();
    // Another server: nothing of this one's failures applies there. The caller hands the next
    // server's dismissals over with setDismissed(); no dismissedChanged(), which would write
    // this server's (empty) list under the next one's name.
    m_failures.clear();
    m_dismissed.clear();
    m_settled.clear();
    m_candidates.clear();
    m_renameStanding.clear();
    m_files.clear();
    m_filesAsked.clear();
    m_renamed.clear();
    m_watches.clear();
    m_jobHashes.clear();
    m_jobTold.clear();
    const bool hadLogLines = !m_webSeedLines.isEmpty() || !m_exportFailure.isEmpty();
    m_webSeedLines.clear();
    m_exportFailure.clear();
    reset();
    if (hadLogLines)
        Q_EMIT logLinesChanged();
}

void ProblemLog::setTorrents(TorrentModel *torrents)
{
    m_torrents = torrents;
}

void ProblemLog::reset()
{
    m_lines.clear();
    m_lastId = -1;
    m_readOnce = false;
    m_sinceRead.invalidate();
    // A read still on its way asked for ids that may mean nothing now: it is dropped, and
    // the next one starts from the top.
    ++m_generation;
    m_reading = false;
}

void ProblemLog::onSynced(bool fullUpdate, bool mayRead)
{
    if (m_torrents.isNull())
        return;
    if (fullUpdate) {
        // The model dropped every problem with its rows, and the server may have restarted:
        // its log, and its ids, start again. updateFailures() below, with no line left, takes
        // every failure away until the log is read again.
        m_problems.clear();
        reset();
    }
    updateErrored(fullUpdate);
    m_seenFirstSync = true;

    endWatches();
    if (fullUpdate || failureTouched())
        updateFailures();

    if (!mayRead)
        return;
    const auto since =
        m_sinceRead.isValid() ? m_sinceRead.durationElapsed() : std::chrono::nanoseconds::max();
    // D2: every poll while omaseed waits for an answer; C15 M15: and while a move job runs.
    bool due = !m_watches.isEmpty() || !m_jobHashes.isEmpty();
    if (!m_errored.isEmpty() && (!m_readOnce || since >= m_interval))
        due = true;
    if (m_visible && since >= m_idleInterval)
        due = true;
    if (due)
        read();
}

void ProblemLog::updateErrored(bool fullUpdate)
{
    const TorrentModel::Change &change = m_torrents->lastChange();
    QStringList look;
    if (fullUpdate || change.full) {
        look = m_torrents->hashes();
        look += QStringList(m_errored.cbegin(), m_errored.cend());
    } else {
        look = change.changed + change.removed;
    }

    bool changed = false;
    for (const QString &hash : std::as_const(look)) {
        const bool known = m_torrents->indexOfHash(hash) >= 0;
        const TorrentState state = known ? m_torrents->torrent(hash).state : TorrentState::Unknown;
        const bool errored = known && isErroredState(state);
        if (known && isCheckingState(state))
            m_checkedAt.insert(hash, QDateTime::currentDateTimeUtc());
        if (errored && !m_errored.contains(hash)) {
            m_errored.insert(hash);
            changed = true;
            const QDateTime checked = m_checkedAt.value(hash);
            if (checked.isValid() &&
                checked.secsTo(QDateTime::currentDateTimeUtc()) <= kAfterCheckSeconds)
                m_erroredAfterCheck.insert(hash);
            // A notice only for an error that happened while watching (D4 a), never for the
            // library as the first sync finds it, and not for missing files, which the start
            // of a server finds by the dozen.
            if (m_seenFirstSync && !fullUpdate && state == TorrentState::Error)
                m_newlyErrored.insert(hash);
        } else if (!errored && m_errored.remove(hash)) {
            changed = true;
            m_erroredAfterCheck.remove(hash);
            m_newlyErrored.remove(hash);
            m_problems.remove(hash);
            if (known)
                m_torrents->setProblem(hash, {});
        }
    }
    if (changed)
        m_readOnce = false;
}

void ProblemLog::read()
{
    if (m_reading || m_client.isNull())
        return;
    m_reading = true;
    const quint64 serial = ++m_issued;
    const quint64 generation = m_generation;
    const bool fromStart = m_lastId < 0;
    m_client->logMainProblems(m_lastId)->then(
        this, [this, serial, generation, fromStart](const QbtResult<QJsonArray> &result) {
            if (generation != m_generation)
                return;
            m_reading = false;
            m_sinceRead.start();
            if (result.isError())
                return;
            applyRead(result.value(), serial, fromStart);
            checkFiles();
        });
}

void ProblemLog::applyEntries(const QJsonArray &entries)
{
    applyRead(entries, ++m_issued, m_lastId < 0);
}

QString ProblemLog::lineKey(int id, qint64 timestamp, const QString &message)
{
    const QByteArray digest =
        QCryptographicHash::hash(message.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("%1:%2:%3").arg(id).arg(timestamp).arg(QString::fromLatin1(digest));
}

void ProblemLog::applyRead(const QJsonArray &entries, quint64 serial, bool fromStart)
{
    QSet<QString> failureKeys;
    bool added = false;
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const int id = entry.value(QStringLiteral("id")).toInt(-1);
        if (id > m_lastId)
            m_lastId = id;
        const QString message = entry.value(QStringLiteral("message")).toString();
        ProblemLine parsed = ProblemLine::parse(message);
        if (parsed.kind == ProblemLine::Kind::Unknown)
            continue;
        const qint64 seconds = qint64(entry.value(QStringLiteral("timestamp")).toDouble());
        Line line;
        line.id = id;
        line.at = QDateTime::fromSecsSinceEpoch(seconds);
        line.message = message;
        line.key = lineKey(id, seconds, message);
        line.read = serial;
        line.parsed = std::move(parsed);
        if (line.parsed.isFailure())
            failureKeys.insert(line.key);
        m_lines.append(std::move(line));
        added = true;
    }
    if (m_lines.size() > kMaxLines)
        m_lines.remove(0, m_lines.size() - kMaxLines);
    m_readOnce = true;

    if (fromStart) {
        // The whole log, from the top: a dismissed line that isn't in it any more is from
        // before qBittorrent restarted, or has left its 20 000 lines. Either way it is gone.
        QSet<QString> kept = m_dismissed;
        kept.intersect(failureKeys);
        if (kept.size() != m_dismissed.size()) {
            m_dismissed = kept;
            Q_EMIT dismissedChanged();
        }
    }

    rematch();
    if (added || fromStart) {
        updateFailures();
        updateLogLines();
    }
    tellWatches(serial);
    tellJob(serial);
}

void ProblemLog::rematch()
{
    if (m_torrents.isNull())
        return;

    QList<Torrent> candidates;
    for (const QString &hash : std::as_const(m_errored)) {
        if (m_torrents->indexOfHash(hash) >= 0)
            candidates.append(m_torrents->torrent(hash));
    }

    // Oldest first, so the newest line for a torrent is the one kept.
    QHash<QString, TorrentProblem> found;
    for (const Line &line : std::as_const(m_lines)) {
        // docs/32's failures leave a torrent working: never the reason for an error.
        if (line.parsed.isFailure())
            continue;
        bool ambiguous = false;
        const QStringList hashes = ProblemMatch::match(line.parsed, candidates, &ambiguous);
        for (const QString &hash : hashes) {
            TorrentProblem problem = ProblemMatch::describe(line.parsed);
            problem.loggedAt = line.at;
            problem.line = line.message;
            problem.ambiguous = ambiguous;
            const TorrentProblem previous = found.value(hash);
            problem.count =
                previous.found && previous.file == problem.file && previous.detail == problem.detail
                    ? previous.count + 1
                    : 1;
            found.insert(hash, problem);
        }
    }

    for (const Torrent &torrent : std::as_const(candidates)) {
        TorrentProblem problem = found.value(torrent.hash);
        if (!problem.found) {
            // Board 44 §2: libtorrent's hybrid error is never logged; a hybrid that errored
            // right after a check is that, and only adding it again clears it.
            problem = torrent.state == TorrentState::Error && torrent.isHybrid() &&
                              m_erroredAfterCheck.contains(torrent.hash)
                          ? ProblemMatch::hybridRecheck()
                          : ProblemMatch::notLogged(torrent.state == TorrentState::MissingFiles);
        }
        publish(torrent.hash, problem);
    }

    // The notices wait for a read, so they carry the reason (N1).
    const QSet<QString> pending = std::exchange(m_newlyErrored, {});
    for (const QString &hash : pending) {
        if (!m_errored.contains(hash) || m_torrents->indexOfHash(hash) < 0)
            continue;
        Q_EMIT torrentErrored(hash, m_torrents->torrent(hash).name, m_problems.value(hash));
    }
}

void ProblemLog::publish(const QString &hash, const TorrentProblem &problem)
{
    auto it = m_problems.find(hash);
    if (it != m_problems.end() && it.value() == problem)
        return;
    m_problems.insert(hash, problem);
    m_torrents->setProblem(hash, problem.toMap());
}

// ---- docs/32 (#55): failures ------------------------------------------------------------------

ProblemLog::Index ProblemLog::buildIndex() const
{
    Index index;
    for (int row = 0; row < m_torrents->count(); ++row) {
        const Torrent &torrent = m_torrents->torrentAt(row);
        index.byName[torrent.name].append(row);
        index.byPrefix[printedHash(torrent).left(6).toLower()].append(row);
    }
    return index;
}

QStringList ProblemLog::failureMatch(const Line &line, const Index &index, bool *ambiguous) const
{
    *ambiguous = false;
    QList<int> rows = index.byPrefix.value(line.parsed.hashPrefix);
    for (const QString &name : line.parsed.quoted)
        rows += index.byName.value(name);
    if (!line.parsed.libtorrentName.isEmpty())
        rows += index.byName.value(line.parsed.libtorrentName);
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    QList<Torrent> candidates;
    for (int row : std::as_const(rows)) {
        const Torrent &torrent = m_torrents->torrentAt(row);
        // A torrent added after the line was written isn't what it is about, even under the
        // same name (one deleted and added again).
        if (torrent.addedOn.isValid() && line.at.isValid() && torrent.addedOn > line.at)
            continue;
        candidates.append(torrent);
    }
    QStringList hashes = ProblemMatch::match(line.parsed, candidates, ambiguous);
    if (*ambiguous && line.parsed.kind == ProblemLine::Kind::RenameFailed) {
        // Two torrents of one name: the one omaseed had just asked to rename is the one that
        // failed. A line from before it asked stays the twins'.
        QStringList asked;
        for (const QString &hash : std::as_const(hashes)) {
            const auto since = m_renamed.constFind(hash);
            if (since != m_renamed.constEnd() && line.read >= since.value())
                asked << hash;
        }
        if (asked.size() == 1) {
            *ambiguous = false;
            hashes = asked;
        }
    }
    return hashes;
}

bool ProblemLog::failureTouched() const
{
    const TorrentModel::Change &change = m_torrents->lastChange();
    if (change.full)
        return true;
    for (const QStringList *list : {&change.changed, &change.removed}) {
        for (const QString &hash : *list) {
            if (m_candidates.contains(hash) || m_failures.contains(hash))
                return true;
        }
    }
    return false;
}

void ProblemLog::updateFailures()
{
    if (m_torrents.isNull())
        return;

    struct Group
    {
        QList<const Line *> moves, renames, partfiles;
        QHash<const Line *, bool> ambiguous;
    };
    QHash<QString, Group> groups;
    QSet<QString> candidates;
    QSet<QString> renameStanding;

    bool any = false;
    for (const Line &line : std::as_const(m_lines)) {
        if (isStanding(line.parsed.kind) && !m_dismissed.contains(line.key) &&
            !m_settled.contains(line.key)) {
            any = true;
            break;
        }
    }
    const Index index = any ? buildIndex() : Index();

    for (const Line &line : std::as_const(m_lines)) {
        const ProblemLine::Kind kind = line.parsed.kind;
        if (!any || !isStanding(kind) || m_dismissed.contains(line.key) ||
            m_settled.contains(line.key))
            continue;
        bool ambiguous = false;
        const QStringList hashes = failureMatch(line, index, &ambiguous);
        for (const QString &hash : hashes) {
            candidates.insert(hash);
            const Torrent &torrent = m_torrents->torrent(hash);
            bool fixed = false;
            if (kind == ProblemLine::Kind::MoveFailed) {
                // F1: the files are still where they were until the save path changes.
                fixed = !line.parsed.source.isEmpty() &&
                        !samePath(torrent.savePath, line.parsed.source) &&
                        !samePath(torrent.downloadPath, line.parsed.source);
            } else if (kind == ProblemLine::Kind::RenameFailed) {
                // F2: the file kept its old name until it is no longer among its files.
                const auto files = m_files.constFind(hash);
                fixed = files != m_files.constEnd() && !files->contains(line.parsed.torrentFile);
            }
            if (fixed) {
                // Seen fixed: it stays so, even if the torrent goes back where it was.
                if (hashes.size() == 1)
                    m_settled.insert(line.key);
                continue;
            }
            Group &group = groups[hash];
            group.ambiguous.insert(&line, ambiguous);
            if (kind == ProblemLine::Kind::MoveFailed) {
                group.moves << &line;
            } else if (kind == ProblemLine::Kind::RenameFailed) {
                group.renames << &line;
                renameStanding.insert(hash);
            } else {
                group.partfiles << &line;
            }
        }
    }

    QHash<QString, TorrentFailure> failures;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const Group &group = it.value();
        const Torrent &torrent = m_torrents->torrent(it.key());
        const QString where = torrent.savePath;
        // The newest of each kind, then the newest of those (docs/32 §5: two failures on one
        // torrent show the newest).
        QList<std::pair<int, TorrentFailure>> shown;
        if (!group.moves.isEmpty()) {
            const Line *newest = group.moves.last();
            TorrentFailure failure = ProblemMatch::describeMove(newest->parsed);
            for (const Line *line : group.moves) {
                failure.keys << line->key;
                if (line->parsed.destination == newest->parsed.destination &&
                    line->parsed.systemMessage == newest->parsed.systemMessage)
                    ++failure.count;
            }
            failure.loggedAt = newest->at;
            failure.line = newest->message;
            failure.ambiguous = group.ambiguous.value(newest);
            shown.append({newest->id, failure});
        }
        if (!group.renames.isEmpty()) {
            // One line per file, its newest; files in the order those lines came.
            QList<const Line *> perFile;
            for (const Line *line : group.renames) {
                perFile.removeIf([line](const Line *kept) {
                    return kept->parsed.torrentFile == line->parsed.torrentFile;
                });
                perFile << line;
            }
            QList<ProblemLine> lines;
            for (const Line *line : std::as_const(perFile))
                lines << line->parsed;
            const Line *newest = group.renames.last();
            TorrentFailure failure = ProblemMatch::describeRename(lines, where);
            for (const Line *line : group.renames)
                failure.keys << line->key;
            failure.count = 1;
            failure.loggedAt = newest->at;
            failure.line = newest->message;
            failure.ambiguous = group.ambiguous.value(newest);
            shown.append({newest->id, failure});
        }
        if (!group.partfiles.isEmpty()) {
            const Line *newest = group.partfiles.last();
            TorrentFailure failure = ProblemMatch::describePartfile(newest->parsed, where);
            for (const Line *line : group.partfiles)
                failure.keys << line->key;
            failure.count = 1;
            failure.loggedAt = newest->at;
            failure.line = newest->message;
            failure.ambiguous = group.ambiguous.value(newest);
            shown.append({newest->id, failure});
        }
        const auto newest = std::max_element(
            shown.cbegin(), shown.cend(), [](auto a, auto b) { return a.first < b.first; });
        failures.insert(it.key(), newest->second);
    }

    m_candidates = candidates;
    m_renameStanding = renameStanding;
    const QSet<QString> before(m_failures.keyBegin(), m_failures.keyEnd());
    for (const QString &hash : before) {
        if (!failures.contains(hash)) {
            m_failures.remove(hash);
            m_torrents->setFailure(hash, {});
        }
    }
    for (auto it = failures.cbegin(); it != failures.cend(); ++it) {
        auto known = m_failures.find(it.key());
        if (known != m_failures.end() && known.value() == it.value())
            continue;
        m_failures.insert(it.key(), it.value());
        m_torrents->setFailure(it.key(), it.value().toMap());
    }
}

void ProblemLog::updateLogLines()
{
    if (m_torrents.isNull())
        return;
    QHash<QString, QList<WebSeedLogLine>> webSeeds;
    QVariantMap exported;
    std::optional<Index> index;
    for (const Line &line : std::as_const(m_lines)) {
        if (line.parsed.kind == ProblemLine::Kind::UrlSeedFailed) {
            if (!index)
                index = buildIndex();
            bool ambiguous = false;
            const WebSeedLogLine entry{
                line.parsed.url, ProblemMatch::webSeedStatusText(line.parsed), line.at};
            for (const QString &hash : failureMatch(line, *index, &ambiguous))
                webSeeds[hash].append(entry);
        } else if (line.parsed.kind == ProblemLine::Kind::ExportFailed) {
            // E1: the newest only; it is about the folder, not the torrent.
            exported = {
                {QStringLiteral("folder"), folderOf(line.parsed.file)},
                {QStringLiteral("text"),
                 tr("The last copy failed: %1, writing %2")
                     .arg(line.parsed.systemMessage, baseName(line.parsed.file))},
                {QStringLiteral("at"), line.at},
                {QStringLiteral("line"), line.message},
            };
        }
    }
    if (webSeeds == m_webSeedLines && exported == m_exportFailure)
        return;
    m_webSeedLines = webSeeds;
    m_exportFailure = exported;
    Q_EMIT logLinesChanged();
}

void ProblemLog::noteRechecked(const QStringList &hashes)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString &hash : hashes)
        m_checkedAt.insert(hash, now);
}

void ProblemLog::dismiss(const QString &hash)
{
    const TorrentFailure failure = m_failures.value(hash);
    if (failure.isEmpty())
        return;
    for (const QString &key : failure.keys)
        m_dismissed.insert(key);
    Q_EMIT dismissedChanged();
    updateFailures();
}

QStringList ProblemLog::dismissed() const
{
    QStringList keys(m_dismissed.cbegin(), m_dismissed.cend());
    keys.sort();
    return keys;
}

void ProblemLog::setDismissed(const QStringList &keys)
{
    m_dismissed = QSet<QString>(keys.cbegin(), keys.cend());
    updateFailures();
}

void ProblemLog::applyFiles(const QString &hash, const QStringList &paths)
{
    m_files.insert(hash, paths);
    updateFailures();
}

void ProblemLog::checkFiles()
{
    if (m_client.isNull())
        return;
    for (const QString &hash : std::as_const(m_renameStanding)) {
        if (m_filesAsked.contains(hash))
            continue;
        m_filesAsked.insert(hash);
        const quint64 generation = m_generation;
        m_client->torrentsFiles(hash)->then(
            this, [this, hash, generation](const QbtResult<QJsonArray> &result) {
                m_filesAsked.remove(hash);
                if (generation != m_generation || result.isError())
                    return;
                QStringList paths;
                for (const QJsonValue &value : result.value())
                    paths << value.toObject().value(QStringLiteral("name")).toString();
                applyFiles(hash, paths);
            });
    }
}

// ---- docs/32 D2 (N1): what omaseed just asked for ---------------------------------------------

void ProblemLog::watchJob(const QStringList &hashes, const QDateTime &since)
{
    m_jobHashes = QSet<QString>(hashes.cbegin(), hashes.cend());
    if (m_jobHashes.isEmpty()) {
        m_jobTold.clear();
        return;
    }
    m_jobSince = since;
}

void ProblemLog::tellJob(quint64 serial)
{
    if (m_jobHashes.isEmpty() || m_torrents.isNull())
        return;
    QList<Torrent> torrents;
    for (const QString &hash : std::as_const(m_jobHashes)) {
        if (m_torrents->indexOfHash(hash) >= 0)
            torrents << m_torrents->torrent(hash);
    }
    // qBittorrent's log counts whole seconds: a line from the second the job began counts.
    const QDateTime since = m_jobSince.isValid() ? m_jobSince.addSecs(-1) : QDateTime();
    for (const Line &line : std::as_const(m_lines)) {
        if (line.read != serial || line.parsed.kind != ProblemLine::Kind::MoveFailed ||
            m_jobTold.contains(line.key) || (since.isValid() && line.at < since))
            continue;
        const QStringList hashes = ProblemMatch::match(line.parsed, torrents, nullptr);
        if (hashes.isEmpty())
            continue;
        m_jobTold.insert(line.key);
        const TorrentProblem words = ProblemMatch::describe(line.parsed);
        for (const QString &hash : hashes)
            Q_EMIT jobMoveFailed(
                hash, line.at, line.parsed.systemMessage, words.causeKey, line.message);
    }
}

void ProblemLog::watchRename(const QString &hash, int files)
{
    if (hash.isEmpty() || m_torrents.isNull())
        return;
    if (!m_renamed.contains(hash))
        m_renamed.insert(hash, m_issued + 1);
    Watch watch;
    watch.action = Action::Rename;
    watch.hashes = {hash};
    watch.names = {m_torrents->torrent(hash).name};
    watch.total = std::max(1, files);
    watch.fromRead = m_issued + 1;
    watch.age.start();
    m_watches.append(watch);
}

void ProblemLog::watchDelete(const QStringList &hashes)
{
    if (hashes.isEmpty() || m_torrents.isNull())
        return;
    Watch watch;
    watch.action = Action::Delete;
    watch.hashes = hashes;
    for (const QString &hash : hashes)
        watch.names << m_torrents->torrent(hash).name;
    watch.total = int(hashes.size());
    watch.fromRead = m_issued + 1;
    watch.age.start();
    m_watches.append(watch);
}

void ProblemLog::endWatches()
{
    m_watches.removeIf(
        [this](const Watch &watch) { return watch.age.durationElapsed() >= m_watchDuration; });
}

void ProblemLog::tellWatches(quint64 serial)
{
    if (m_watches.isEmpty() || m_torrents.isNull())
        return;
    for (Watch &watch : m_watches) {
        if (serial < watch.fromRead)
            continue;
        QList<Torrent> torrents;
        for (const QString &hash : std::as_const(watch.hashes)) {
            if (m_torrents->indexOfHash(hash) >= 0)
                torrents << m_torrents->torrent(hash);
        }
        QString reason;
        QString file;
        bool told = false;
        for (const Line &line : std::as_const(m_lines)) {
            if (line.read != serial || watch.counted.contains(line.key))
                continue;
            const ProblemLine::Kind kind = line.parsed.kind;
            QStringList failed;
            if (watch.action == Action::Rename && kind == ProblemLine::Kind::RenameFailed) {
                if (!ProblemMatch::match(line.parsed, torrents, nullptr).isEmpty()) {
                    failed = {line.parsed.torrentFile};
                    file = line.parsed.torrentFile;
                }
            } else if (watch.action == Action::Delete &&
                       kind == ProblemLine::Kind::ContentRemovalFailed &&
                       watch.names.contains(line.parsed.quoted.value(0))) {
                failed = {line.parsed.quoted.value(0)};
            }
            if (failed.isEmpty())
                continue;
            watch.counted.insert(line.key);
            for (const QString &one : std::as_const(failed))
                watch.failed.insert(one);
            reason = line.parsed.systemMessage;
            told = true;
        }
        if (!told)
            continue;

        const int count = int(watch.failed.size());
        QString text;
        QStringList stillHere;
        switch (watch.action) {
        case Action::Rename:
            text = count == 1 ? tr("%1 kept its old name: %2").arg(baseName(file), reason)
                              : tr("%1 files kept their old names: %2").arg(count).arg(reason);
            if (m_torrents->indexOfHash(watch.hashes.value(0)) >= 0)
                stillHere = watch.hashes;
            break;
        case Action::Delete:
            text =
                watch.total == 1
                    ? tr("%1 was removed, its files weren't: %2").arg(watch.names.value(0), reason)
                    : tr("%1 of %2 were removed, their files weren't: %3")
                          .arg(count)
                          .arg(watch.total)
                          .arg(reason);
            break;
        }
        stillHere.sort();
        Q_EMIT watchedFailure(text, stillHere);
    }
    // A delete whose every torrent failed has nothing more to say.
    m_watches.removeIf([](const Watch &watch) {
        return watch.action != Action::Rename && watch.failed.size() >= watch.total;
    });
}

} // namespace omaseed::core
