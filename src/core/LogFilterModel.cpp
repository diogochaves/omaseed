#include "core/LogFilterModel.hpp"

#include "core/ServerLog.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace omaseed::core {

LogFilterModel::LogFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // Every way the shown lines change moves the count; the proxy keeps itself up to date as
    // lines arrive, so a read never filters the whole log again.
    connect(this, &QAbstractItemModel::rowsInserted, this, &LogFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &LogFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &LogFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &LogFilterModel::countChanged);
}

void LogFilterModel::setLog(ServerLog *log)
{
    if (m_log == log)
        return;
    if (m_log)
        disconnect(m_log, nullptr, this, nullptr);
    m_log = log;
    setSourceModel(log);
    if (log) {
        connect(log, &ServerLog::countsChanged, this, &LogFilterModel::countChanged);
        // The ids start again: a mark would land on another line.
        connect(log, &ServerLog::restarted, this, &LogFilterModel::clearMarks);
        // After the proxy's own handling of the same signal, which was connected first.
        connect(log,
                &QAbstractItemModel::rowsInserted,
                this,
                [this](const QModelIndex &, int first, int last) {
                    int shown = 0;
                    for (int row = first; row <= last; ++row) {
                        if (filterAcceptsRow(row, {}))
                            ++shown;
                    }
                    if (shown > 0)
                        Q_EMIT linesArrived(shown);
                });
    }
    Q_EMIT logChanged();
    Q_EMIT countChanged();
}

void LogFilterModel::setLevels(int levels)
{
    levels &= 15;
    if (m_levels == levels)
        return;
    m_levels = levels;
    refilter();
    Q_EMIT levelsChanged();
}

void LogFilterModel::toggleLevel(int level)
{
    setLevels(m_levels ^ level);
}

void LogFilterModel::setText(const QString &text)
{
    if (m_text == text)
        return;
    const bool wasNarrowed = isNarrowed();
    m_text = text;
    const QStringList next = words(text);
    Q_EMIT textChanged();
    if (next == m_words)
        return;
    m_words = next;
    refilter();
    if (wasNarrowed != isNarrowed())
        Q_EMIT narrowedChanged();
}

QStringList LogFilterModel::words(const QString &text)
{
    static const QRegularExpression space(QStringLiteral("\\s+"));
    return text.toLower().split(space, Qt::SkipEmptyParts);
}

bool LogFilterModel::matchesWords(const QString &message, const QStringList &words)
{
    return std::all_of(words.cbegin(), words.cend(), [&message](const QString &word) {
        return message.contains(word, Qt::CaseInsensitive);
    });
}

bool LogFilterModel::setTorrent(const QString &hash)
{
    if (m_log.isNull())
        return false;
    const QVariantMap key = m_log->torrentKey(hash);
    if (key.isEmpty())
        return false;
    const bool wasNarrowed = isNarrowed();
    m_torrentHash = hash;
    m_torrentName = key.value(QStringLiteral("name")).toString();
    m_torrentPrefix = key.value(QStringLiteral("prefix")).toString();
    refilter();
    Q_EMIT torrentChanged();
    if (!wasNarrowed)
        Q_EMIT narrowedChanged();
    return true;
}

void LogFilterModel::clearTorrent()
{
    if (m_torrentHash.isEmpty())
        return;
    m_torrentHash.clear();
    m_torrentName.clear();
    m_torrentPrefix.clear();
    refilter();
    Q_EMIT torrentChanged();
    if (!isNarrowed())
        Q_EMIT narrowedChanged();
}

QString LogFilterModel::torrentAt(int row) const
{
    if (m_log.isNull() || row < 0 || row >= rowCount())
        return {};
    return m_log->torrentAt(mapToSource(index(row, 0)).row());
}

int LogFilterModel::levelTotal() const
{
    return m_log ? m_log->countAt(m_levels) : 0;
}

bool LogFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (m_log.isNull())
        return false;
    const ServerLog::Line &line = m_log->lineAt(sourceRow);
    if (!(line.level & m_levels))
        return false;
    if (!matchesWords(line.message, m_words))
        return false;
    if (!m_torrentHash.isEmpty() && !ServerLog::names(line.message, m_torrentName, m_torrentPrefix))
        return false;
    return true;
}

void LogFilterModel::refilter()
{
    beginFilterChange();
    endFilterChange(Direction::Rows);
}

int LogFilterModel::nextProblem(int from, int step) const
{
    if (m_log.isNull() || step == 0)
        return -1;
    const int rows = rowCount();
    for (int row = from + (step > 0 ? 1 : -1); row >= 0 && row < rows; row += step > 0 ? 1 : -1) {
        const int level = m_log->lineAt(mapToSource(index(row, 0)).row()).level;
        if (level == ServerLog::Warning || level == ServerLog::Critical)
            return row;
    }
    return -1;
}

int LogFilterModel::idAt(int row) const
{
    if (m_log.isNull() || row < 0 || row >= rowCount())
        return -1;
    return m_log->lineAt(mapToSource(index(row, 0)).row()).id;
}

QString LogFilterModel::messageAt(int row) const
{
    if (m_log.isNull() || row < 0 || row >= rowCount())
        return {};
    return m_log->lineAt(mapToSource(index(row, 0)).row()).message;
}

int LogFilterModel::rowOfId(int id) const
{
    if (m_log.isNull() || id < 0)
        return -1;
    // Ids grow down the log: a binary search over the source, then its place here.
    int low = 0;
    int high = m_log->rowCount() - 1;
    while (low <= high) {
        const int mid = (low + high) / 2;
        const int at = m_log->lineAt(mid).id;
        if (at == id)
            return mapFromSource(m_log->index(mid, 0)).row();
        if (at < id)
            low = mid + 1;
        else
            high = mid - 1;
    }
    return -1;
}

bool LogFilterModel::isMarked(int row) const
{
    return m_marked.contains(idAt(row));
}

void LogFilterModel::toggleMark(int row)
{
    const int id = idAt(row);
    if (id < 0)
        return;
    if (!m_marked.remove(id))
        m_marked.insert(id);
    const QModelIndex at = index(row, 0);
    Q_EMIT dataChanged(at, at, {MarkedRole});
    Q_EMIT marksChanged();
}

void LogFilterModel::markRange(int from, int to, bool add)
{
    const int rows = rowCount();
    if (rows == 0)
        return;
    if (!add)
        m_marked.clear();
    const int low = std::clamp(std::min(from, to), 0, rows - 1);
    const int high = std::clamp(std::max(from, to), 0, rows - 1);
    for (int row = low; row <= high; ++row)
        m_marked.insert(idAt(row));
    Q_EMIT dataChanged(index(0, 0), index(rows - 1, 0), {MarkedRole});
    Q_EMIT marksChanged();
}

void LogFilterModel::markAll()
{
    markRange(0, rowCount() - 1);
}

void LogFilterModel::clearMarks()
{
    if (m_marked.isEmpty())
        return;
    m_marked.clear();
    if (rowCount() > 0)
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {MarkedRole});
    Q_EMIT marksChanged();
}

QList<int> LogFilterModel::markedRows() const
{
    QList<int> rows;
    if (m_marked.isEmpty())
        return rows;
    for (int row = 0; row < rowCount(); ++row) {
        if (m_marked.contains(idAt(row)))
            rows.append(row);
    }
    return rows;
}

QString LogFilterModel::copyText(int cursor) const
{
    if (m_log.isNull())
        return {};
    QList<int> rows = markedRows();
    if (rows.isEmpty() && cursor >= 0 && cursor < rowCount())
        rows = {cursor};
    QStringList lines;
    lines.reserve(rows.size());
    for (const int row : std::as_const(rows))
        lines << m_log->copyLine(mapToSource(index(row, 0)).row());
    return lines.join(QLatin1Char('\n'));
}

int LogFilterModel::copyCount(int cursor) const
{
    const qsizetype marked = markedRows().size();
    if (marked > 0)
        return int(marked);
    return cursor >= 0 && cursor < rowCount() ? 1 : 0;
}

QVariant LogFilterModel::data(const QModelIndex &index, int role) const
{
    if (role == MarkedRole)
        return m_marked.contains(idAt(index.row()));
    return QSortFilterProxyModel::data(index, role);
}

QHash<int, QByteArray> LogFilterModel::roleNames() const
{
    QHash<int, QByteArray> names = QSortFilterProxyModel::roleNames();
    names.insert(MarkedRole, "marked");
    return names;
}

} // namespace omaseed::core
