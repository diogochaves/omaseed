#include "core/ServerLog.hpp"

#include "core/TorrentModel.hpp"
#include "core/TorrentProblem.hpp"
#include "core/engines/EngineClient.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QLocale>

#include <algorithm>

namespace omaseed::core {

namespace {

/// 1, 2, 4, 8 -> 0, 1, 2, 3; anything else counts as normal.
int levelIndex(int level)
{
    switch (level) {
    case ServerLog::Info:
        return 1;
    case ServerLog::Warning:
        return 2;
    case ServerLog::Critical:
        return 3;
    default:
        return 0;
    }
}

int levelOf(int type)
{
    switch (type) {
    case ServerLog::Info:
    case ServerLog::Warning:
    case ServerLog::Critical:
        return type;
    default:
        return ServerLog::Normal;
    }
}

} // namespace

ServerLog::ServerLog(QObject *parent)
    : QAbstractListModel(parent)
{}

void ServerLog::setClient(EngineClient *client)
{
    if (m_client == client)
        return;
    m_client = client;
    startOver();
    if (m_active)
        read();
}

void ServerLog::setTorrents(TorrentModel *torrents)
{
    m_torrents = torrents;
}

void ServerLog::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    // On screen again: what happened meanwhile, now rather than at the next poll.
    if (m_active)
        read();
}

void ServerLog::setMoveLines(QList<Line> lines)
{
    std::stable_sort(
        lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.at < b.at; });
    beginResetModel();
    m_lines.removeIf([](const Line &line) { return !line.moveId.isEmpty(); });
    m_moveLines = std::move(lines);
    mergeMoveLines();
    endResetModel();
    Q_EMIT countsChanged();
}

void ServerLog::mergeMoveLines()
{
    // Each of omaseed's lines after qBittorrent's of the same second or before.
    QList<Line> merged;
    merged.reserve(m_lines.size() + m_moveLines.size());
    qsizetype i = 0;
    for (const Line &line : std::as_const(m_lines)) {
        while (i < m_moveLines.size() && m_moveLines.at(i).at < line.at)
            merged.append(m_moveLines.at(i++));
        merged.append(line);
    }
    while (i < m_moveLines.size())
        merged.append(m_moveLines.at(i++));
    m_lines = std::move(merged);
    m_counts = {};
    for (const Line &line : std::as_const(m_lines))
        ++m_counts[levelIndex(line.level)];
}

void ServerLog::setCapacity(int lines)
{
    m_capacity = qMax(1, lines);
    const qsizetype over = m_lines.size() - m_capacity;
    if (over <= 0)
        return;
    beginRemoveRows({}, 0, int(over) - 1);
    for (qsizetype i = 0; i < over; ++i)
        --m_counts[levelIndex(m_lines.at(i).level)];
    m_lines.remove(0, over);
    endRemoveRows();
    Q_EMIT countsChanged();
}

void ServerLog::onSynced(bool fullUpdate, bool mayRead)
{
    if (fullUpdate)
        startOver();
    if (mayRead && m_active)
        read();
}

void ServerLog::release()
{
    startOver();
}

void ServerLog::startOver()
{
    // A read still on its way asked for ids that may mean nothing now: it is dropped.
    ++m_generation;
    m_reading = false;
    const bool had = !m_lines.isEmpty();
    if (had || !m_moveLines.isEmpty()) {
        beginResetModel();
        m_lines.clear();
        m_counts = {};
        // omaseed's own lines don't come from the server: they stay.
        mergeMoveLines();
        endResetModel();
    }
    m_lastId = -1;
    if (m_loaded) {
        m_loaded = false;
        Q_EMIT loadedChanged();
    }
    if (had)
        Q_EMIT countsChanged();
    Q_EMIT restarted();
}

void ServerLog::read()
{
    if (m_reading || m_client.isNull())
        return;
    m_reading = true;
    const quint64 generation = m_generation;
    m_client->log(m_lastId, false)
        ->then(this, [this, generation](const Result<QList<LogEntry>> &result) {
            if (generation != m_generation)
                return;
            m_reading = false;
            if (result.isError())
                return;
            applyRead(result.value());
        });
}

void ServerLog::applyEntries(const QJsonArray &entries)
{
    applyRead(QbtTranslate::log(entries));
}

void ServerLog::applyRead(const QList<LogEntry> &entries)
{
    QList<Line> fresh;
    fresh.reserve(entries.size());
    for (const LogEntry &entry : entries) {
        Line line;
        line.id = entry.id;
        if (line.id < 0)
            continue;
        // qBittorrent only sends lines after last_known_id: one at or below it is from a log
        // that started again, so this one is read again from the top.
        if (line.id <= m_lastId) {
            startOver();
            if (m_active)
                read();
            return;
        }
        m_lastId = line.id;
        line.at = entry.timestamp;
        line.level = levelOf(entry.type);
        line.message = entry.message;
        fresh.append(std::move(line));
    }

    const qsizetype room = m_capacity;
    // A read longer than the room keeps only its newest lines.
    if (fresh.size() > room)
        fresh.remove(0, fresh.size() - room);
    const qsizetype over = m_lines.size() + fresh.size() - room;
    if (over > 0) {
        beginRemoveRows({}, 0, int(over) - 1);
        for (qsizetype i = 0; i < over; ++i)
            --m_counts[levelIndex(m_lines.at(i).level)];
        m_lines.remove(0, over);
        endRemoveRows();
    }
    if (!fresh.isEmpty()) {
        // #137: in time order, among omaseed's lines (a move's line is written when it ends,
        // qBittorrent's about it may come at the next read; after a start over, the whole log
        // comes in one read behind omaseed's). Every row after `at` is one of omaseed's, none
        // older than the first fresh line; each fresh line goes before those newer than it, as
        // mergeMoveLines() puts them (omaseed's after qBittorrent's of the same second), in
        // runs, one insert per run.
        qsizetype at = m_lines.size();
        while (at > 0 && !m_lines.at(at - 1).moveId.isEmpty() &&
               m_lines.at(at - 1).at >= fresh.constFirst().at)
            --at;
        qsizetype next = 0;
        while (next < fresh.size()) {
            while (at < m_lines.size() && m_lines.at(at).at < fresh.at(next).at)
                ++at;
            qsizetype end = next + 1;
            if (at == m_lines.size())
                end = fresh.size();
            else
                while (end < fresh.size() && fresh.at(end).at <= m_lines.at(at).at)
                    ++end;
            beginInsertRows({}, int(at), int(at + end - next) - 1);
            for (; next < end; ++next) {
                ++m_counts[levelIndex(fresh.at(next).level)];
                m_lines.insert(at++, std::move(fresh[next]));
            }
            endInsertRows();
        }
    }
    if (over > 0 || !fresh.isEmpty())
        Q_EMIT countsChanged();
    if (!m_loaded) {
        m_loaded = true;
        Q_EMIT loadedChanged();
    }
}

QList<int> ServerLog::levelCounts() const
{
    return {m_counts[0], m_counts[1], m_counts[2], m_counts[3]};
}

int ServerLog::countAt(int mask) const
{
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        if (mask & (1 << i))
            sum += m_counts[i];
    }
    return sum;
}

QString ServerLog::firstAtText() const
{
    if (m_lines.isEmpty())
        return {};
    const QDateTime at = QDateTime::fromSecsSinceEpoch(m_lines.constFirst().at);
    const QDate today = QDate::currentDate();
    if (at.date() == today)
        return QLocale::c().toString(at, QStringLiteral("HH:mm"));
    if (at.date().year() == today.year())
        return QLocale::c().toString(at, QStringLiteral("d MMM HH:mm"));
    return QLocale::c().toString(at, QStringLiteral("d MMM yyyy HH:mm"));
}

bool ServerLog::names(const QString &message, const QString &name, const QString &prefix)
{
    if (name.isEmpty() && prefix.isEmpty())
        return false;
    // Cheap first: most lines name nothing of the kind.
    if ((name.isEmpty() || !message.contains(name)) &&
        (prefix.isEmpty() || !message.contains(prefix)))
        return false;
    const QStringList quoted = ProblemMatch::quotedSegments(message);
    for (const QString &part : quoted) {
        if (!name.isEmpty() && part == name)
            return true;
        for (const QString &head : {prefix, name}) {
            if (!head.isEmpty() && part.size() > head.size() && part.startsWith(head) &&
                part.at(head.size()) == QLatin1Char(' '))
                return true;
        }
    }
    return false;
}

QString ServerLog::torrentAt(int row) const
{
    if (row < 0 || row >= m_lines.size() || m_torrents.isNull())
        return {};
    const QString &message = m_lines.at(row).message;
    const QStringList quoted = ProblemMatch::quotedSegments(message);
    if (quoted.isEmpty())
        return {};
    // The first quoted part that is a torrent's name, or starts with one's hash prefix.
    QString byPrefix;
    for (const QString &part : quoted) {
        for (int i = 0; i < m_torrents->count(); ++i) {
            const Torrent &torrent = m_torrents->torrentAt(i);
            if (part == torrent.name)
                return torrent.hash;
            if (byPrefix.isEmpty() && part.size() > 7 && part.at(6) == QLatin1Char(' ') &&
                part.startsWith(ProblemMatch::hashPrefix(torrent)))
                byPrefix = torrent.hash;
        }
    }
    return byPrefix;
}

QVariantMap ServerLog::torrentKey(const QString &hash) const
{
    if (m_torrents.isNull() || m_torrents->indexOfHash(hash) < 0)
        return {};
    const Torrent torrent = m_torrents->torrent(hash);
    return {{QStringLiteral("name"), torrent.name},
            {QStringLiteral("prefix"), ProblemMatch::hashPrefix(torrent)}};
}

QString ServerLog::levelName(int level)
{
    switch (level) {
    case Info:
        return QStringLiteral("info");
    case Warning:
        return QStringLiteral("warning");
    case Critical:
        return QStringLiteral("critical");
    default:
        return QStringLiteral("normal");
    }
}

QString ServerLog::timeText(qint64 seconds, const QDateTime &now)
{
    const QDateTime at = QDateTime::fromSecsSinceEpoch(seconds);
    if (at.date() == now.date())
        return QLocale::c().toString(at, QStringLiteral("HH:mm:ss"));
    if (at.date().year() == now.date().year())
        return QLocale::c().toString(at, QStringLiteral("d MMM HH:mm:ss"));
    return QLocale::c().toString(at, QStringLiteral("d MMM yyyy HH:mm:ss"));
}

QString ServerLog::copyLine(int row) const
{
    if (row < 0 || row >= m_lines.size())
        return {};
    const Line &line = m_lines.at(row);
    return QStringLiteral("%1 %2 %3")
        .arg(QLocale::c().toString(QDateTime::fromSecsSinceEpoch(line.at),
                                   QStringLiteral("yyyy-MM-dd HH:mm:ss")),
             levelName(line.level),
             line.message);
}

int ServerLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_lines.size());
}

QVariant ServerLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const Line &line = m_lines.at(index.row());
    switch (role) {
    case IdRole:
        return line.id;
    case LevelRole:
        return line.level;
    case LevelNameRole:
        return levelName(line.level);
    case TimestampRole:
        return line.at;
    case TimeTextRole:
        return timeText(line.at);
    case Qt::DisplayRole:
    case MessageRole:
        return line.message;
    case MoveIdRole:
        return line.moveId;
    default:
        return {};
    }
}

QHash<int, QByteArray> ServerLog::roleNames() const
{
    return {{IdRole, "lineId"},
            {LevelRole, "level"},
            {LevelNameRole, "levelName"},
            {TimestampRole, "timestamp"},
            {TimeTextRole, "timeText"},
            {MessageRole, "message"},
            {MoveIdRole, "moveId"}};
}

} // namespace omaseed::core
