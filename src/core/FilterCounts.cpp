#include "core/FilterCounts.hpp"

#include "core/TorrentModel.hpp"

#include <algorithm>

namespace omaseed::core {

namespace {

/// The flag's position in m_groupCounts; -1 for NoGroup.
int slotFor(StatusGroup group)
{
    switch (group) {
    case StatusGroup::Downloading:
        return 0;
    case StatusGroup::Seeding:
        return 1;
    case StatusGroup::Completed:
        return 2;
    case StatusGroup::Stopped:
        return 3;
    case StatusGroup::Active:
        return 4;
    case StatusGroup::Stalled:
        return 5;
    case StatusGroup::Checking:
        return 6;
    case StatusGroup::Errored:
        return 7;
    case StatusGroup::NoGroup:
        break;
    }
    return -1;
}

constexpr StatusGroup kAllGroups[] = {
    StatusGroup::Downloading,
    StatusGroup::Seeding,
    StatusGroup::Completed,
    StatusGroup::Stopped,
    StatusGroup::Active,
    StatusGroup::Stalled,
    StatusGroup::Checking,
    StatusGroup::Errored,
};

} // namespace

FilterCounts::FilterCounts(QObject *parent)
    : QObject(parent)
{}

void FilterCounts::setModel(TorrentModel *model)
{
    if (m_model == model)
        return;
    if (m_model != nullptr)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model != nullptr)
        connect(m_model, &TorrentModel::updated, this, &FilterCounts::recompute);
    recompute();
}

int FilterCounts::count(StatusGroup group) const
{
    const int slot = slotFor(group);
    return slot < 0 ? m_total : m_groupCounts[slot];
}

int FilterCounts::statusCount(int group) const
{
    return count(static_cast<StatusGroup>(group));
}

int FilterCounts::categoryCount(const QString &name) const
{
    return m_categoryCounts.value(name);
}

int FilterCounts::tagCount(const QString &tag) const
{
    return tag.isEmpty() ? m_untagged : m_tagCounts.value(tag);
}

int FilterCounts::trackerCount(const QString &host) const
{
    return m_trackerCounts.value(host);
}

void FilterCounts::recompute()
{
    const int oldTotal = m_total;
    const int oldInProgress = m_inProgress;
    const QHash<QString, int> oldCategories = m_categoryCounts;
    const QHash<QString, int> oldTags = m_tagCounts;
    const QHash<QString, int> oldTrackers = m_trackerCounts;
    int oldGroups[8];
    std::copy(std::begin(m_groupCounts), std::end(m_groupCounts), std::begin(oldGroups));

    m_total = 0;
    m_untagged = 0;
    m_inProgress = 0;
    for (int &slot : m_groupCounts)
        slot = 0;
    m_categoryCounts.clear();
    m_tagCounts.clear();
    m_trackerCounts.clear();
    m_trackerlessHashes.clear();

    const int rows = m_model != nullptr ? m_model->count() : 0;
    m_total = rows;
    m_trackerlessHashes.reserve(rows / 4 + 1);

    for (int row = 0; row < rows; ++row) {
        const Torrent &t = m_model->torrentAt(row);

        const StatusGroups groups = t.statusGroups();
        for (const StatusGroup group : kAllGroups) {
            if (groups.testFlag(group))
                ++m_groupCounts[slotFor(group)];
        }
        if (groups.testFlag(StatusGroup::Downloading) && !groups.testFlag(StatusGroup::Stopped))
            ++m_inProgress;

        ++m_categoryCounts[t.category];

        if (t.tags.isEmpty())
            ++m_untagged;
        for (const QString &tag : t.tags)
            ++m_tagCounts[tag];

        // QUrl parsing dominates this loop otherwise: one announce URL serves many torrents.
        auto host = m_hostCache.constFind(t.tracker);
        if (host == m_hostCache.constEnd())
            host = m_hostCache.insert(t.tracker, trackerHost(t.tracker));
        ++m_trackerCounts[*host];
        if (*host == trackerlessKey())
            m_trackerlessHashes.append(t.hash);
    }

    const bool sameGroups =
        std::equal(std::begin(m_groupCounts), std::end(m_groupCounts), std::begin(oldGroups));
    if (oldTotal == m_total && oldInProgress == m_inProgress && sameGroups && oldCategories == m_categoryCounts &&
        oldTags == m_tagCounts && oldTrackers == m_trackerCounts)
        return;

    Q_EMIT changed();
}

} // namespace omaseed::core
