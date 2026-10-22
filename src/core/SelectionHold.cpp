#include "core/SelectionHold.hpp"

#include <QTimer>

#include <algorithm>

namespace omaseed::core {

SelectionHold::SelectionHold(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &SelectionHold::due);
}

bool SelectionHold::mustWait(const QHash<int, int> &current,
                             const QHash<int, qint64> &sizes,
                             const QList<int> &ids,
                             int priority,
                             qint64 roomBytes)
{
    if (priority == 0)
        return false; // a skip never costs space (D5)
    qint64 added = 0;
    int widened = 0;
    for (const int id : ids) {
        if (current.value(id, 1) != 0)
            continue;
        added += sizes.value(id, 0);
        ++widened;
    }
    if (widened == 0)
        return false;
    if (added > kThresholdBytes)
        return true;
    // C28 (#159): a tick that doesn't fit on ntfs3, exfat or vfat waits, so it can be undone
    // before libtorrent writes anything (#116).
    if (roomBytes >= 0 && added > roomBytes)
        return true;
    // D6: or the whole torrent: after the change no file is skipped.
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (it.value() == 0 && !ids.contains(it.key()))
            return false;
    }
    return true;
}

qint64 SelectionHold::widening(const QHash<int, int> &current,
                               const QHash<int, qint64> &sizes,
                               const QList<int> &ids,
                               int priority)
{
    if (priority == 0)
        return 0;
    qint64 added = 0;
    for (const int id : ids) {
        if (current.value(id, 1) == 0)
            added += sizes.value(id, 0);
    }
    return added;
}

void SelectionHold::add(const QString &hash,
                        const QList<int> &ids,
                        int priority,
                        const QHash<int, int> &current,
                        const QHash<int, qint64> &sizes)
{
    if (m_hash != hash) {
        m_original.clear();
        m_target.clear();
        m_sizes.clear();
        m_hash = hash;
    }
    for (const int id : ids) {
        if (!m_original.contains(id))
            m_original.insert(id, current.value(id, 1));
        m_target.insert(id, priority);
        m_sizes.insert(id, sizes.value(id, 0));
    }
    // A file back where the server has it is no change.
    for (auto it = m_target.begin(); it != m_target.end();) {
        if (m_original.value(it.key()) == it.value()) {
            m_original.remove(it.key());
            m_sizes.remove(it.key());
            it = m_target.erase(it);
        } else {
            ++it;
        }
    }
    if (m_target.isEmpty()) {
        clear();
        return;
    }
    m_deadline = QDateTime::currentDateTimeUtc().addMSecs(m_windowMs);
    m_timer->start(m_windowMs);
    Q_EMIT changed();
}

QMap<int, QList<int>> SelectionHold::net() const
{
    QMap<int, QList<int>> groups;
    for (auto it = m_target.cbegin(); it != m_target.cend(); ++it)
        groups[it.value()].append(it.key());
    for (QList<int> &ids : groups)
        std::sort(ids.begin(), ids.end());
    return groups;
}

QMap<int, QList<int>> SelectionHold::originals() const
{
    QMap<int, QList<int>> groups;
    for (auto it = m_original.cbegin(); it != m_original.cend(); ++it)
        groups[it.value()].append(it.key());
    for (QList<int> &ids : groups)
        std::sort(ids.begin(), ids.end());
    return groups;
}

int SelectionHold::addedFiles() const
{
    int count = 0;
    for (auto it = m_target.cbegin(); it != m_target.cend(); ++it) {
        if (it.value() != 0 && m_original.value(it.key()) == 0)
            ++count;
    }
    return count;
}

qint64 SelectionHold::addedBytes() const
{
    qint64 bytes = 0;
    for (auto it = m_target.cbegin(); it != m_target.cend(); ++it) {
        if (it.value() != 0 && m_original.value(it.key()) == 0)
            bytes += m_sizes.value(it.key());
    }
    return bytes;
}

void SelectionHold::setWindowMs(int ms)
{
    m_windowMs = std::max(1, ms);
}

QMap<int, QList<int>> SelectionHold::take()
{
    const QMap<int, QList<int>> groups = net();
    clear();
    return groups;
}

void SelectionHold::clear()
{
    m_timer->stop();
    const bool was = waiting() || !m_hash.isEmpty();
    m_original.clear();
    m_target.clear();
    m_sizes.clear();
    m_hash.clear();
    m_deadline = QDateTime();
    if (was)
        Q_EMIT changed();
}

} // namespace omaseed::core
