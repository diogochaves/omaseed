#include "core/ProblemLog.hpp"

#include "core/QbtClient.hpp"
#include "core/TorrentModel.hpp"

#include <QJsonObject>

namespace omaseed::core {

namespace {

/// Enough for every line about a torrent's files in a busy session: qBittorrent logs one per
/// torrent per error, not one per piece.
constexpr qsizetype kMaxLines = 2000;

bool isErroredState(TorrentState state)
{
    return state == TorrentState::Error || state == TorrentState::MissingFiles;
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
    reset();
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
}

void ProblemLog::onSynced(bool fullUpdate, bool mayRead)
{
    if (m_torrents.isNull())
        return;
    if (fullUpdate) {
        // The model dropped every problem with its rows, and the server may have restarted:
        // its log, and its ids, start again.
        m_problems.clear();
        reset();
    }
    updateErrored(fullUpdate);
    m_seenFirstSync = true;

    if (m_errored.isEmpty() || !mayRead)
        return;
    const bool due =
        !m_readOnce || !m_sinceRead.isValid() || m_sinceRead.durationElapsed() >= m_interval;
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
        if (errored && !m_errored.contains(hash)) {
            m_errored.insert(hash);
            changed = true;
            // A notice only for an error that happened while watching (D4 a), never for the
            // library as the first sync finds it, and not for missing files, which the start
            // of a server finds by the dozen.
            if (m_seenFirstSync && !fullUpdate && state == TorrentState::Error)
                m_newlyErrored.insert(hash);
        } else if (!errored && m_errored.remove(hash)) {
            changed = true;
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
    m_client->logMainProblems(m_lastId)->then(this, [this](const QbtResult<QJsonArray> &result) {
        m_reading = false;
        m_sinceRead.start();
        if (result.isError())
            return;
        m_readOnce = true;
        applyEntries(result.value());
    });
}

void ProblemLog::applyEntries(const QJsonArray &entries)
{
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const int id = entry.value(QStringLiteral("id")).toInt(-1);
        if (id > m_lastId)
            m_lastId = id;
        const QString message = entry.value(QStringLiteral("message")).toString();
        ProblemLine parsed = ProblemLine::parse(message);
        if (parsed.kind == ProblemLine::Kind::Unknown)
            continue;
        Line line;
        line.id = id;
        line.at = QDateTime::fromSecsSinceEpoch(
            qint64(entry.value(QStringLiteral("timestamp")).toDouble()));
        line.message = message;
        line.parsed = std::move(parsed);
        m_lines.append(std::move(line));
    }
    if (m_lines.size() > kMaxLines)
        m_lines.remove(0, m_lines.size() - kMaxLines);
    m_readOnce = true;
    rematch();
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
        if (!problem.found)
            problem = ProblemMatch::notLogged(torrent.state == TorrentState::MissingFiles);
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

} // namespace omaseed::core
