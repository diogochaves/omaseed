#include "core/TorrentFilterModel.hpp"

#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <algorithm>

namespace omaseed::core {

TorrentFilterModel::TorrentFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_collator.setNumericMode(true); // "part 2" before "part 10"

    m_sortRole = TorrentModel::NameRole;
    QSortFilterProxyModel::setSortRole(m_sortRole);
    setDynamicSortFilter(true);
    sort(0, m_sortOrder);

    const auto emitCount = [this]() { Q_EMIT countChanged(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, emitCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, emitCount);
    connect(this, &QAbstractItemModel::modelReset, this, emitCount);
    connect(this, &QAbstractItemModel::layoutChanged, this, emitCount);
}

StatusGroup TorrentFilterModel::groupFor(StatusFilter filter)
{
    switch (filter) {
    case Downloading:
        return StatusGroup::Downloading;
    case Seeding:
        return StatusGroup::Seeding;
    case Completed:
        return StatusGroup::Completed;
    case Stopped:
        return StatusGroup::Stopped;
    case Active:
        return StatusGroup::Active;
    case Stalled:
        return StatusGroup::Stalled;
    case Checking:
        return StatusGroup::Checking;
    case Errored:
        return StatusGroup::Errored;
    case All:
        break;
    }
    return StatusGroup::NoGroup;
}

void TorrentFilterModel::setStatusFilter(StatusFilter filter)
{
    if (m_statusFilter == filter)
        return;
    m_statusFilter = filter;
    refilter();
    Q_EMIT statusFilterChanged();
    Q_EMIT countChanged();
}

QVariant TorrentFilterModel::category() const
{
    return m_category ? QVariant(*m_category) : QVariant();
}

void TorrentFilterModel::setCategory(const QVariant &category)
{
    if (!category.isValid() || category.isNull()) {
        clearCategory();
        return;
    }
    const QString next = category.toString();
    if (m_category && *m_category == next)
        return;
    m_category = next;
    refilter();
    Q_EMIT categoryChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::clearCategory()
{
    if (!m_category)
        return;
    m_category.reset();
    refilter();
    Q_EMIT categoryChanged();
    Q_EMIT countChanged();
}

QVariant TorrentFilterModel::tag() const
{
    return m_tag ? QVariant(*m_tag) : QVariant();
}

void TorrentFilterModel::setTag(const QVariant &tag)
{
    if (!tag.isValid() || tag.isNull()) {
        clearTag();
        return;
    }
    const QString next = tag.toString();
    if (m_tag && *m_tag == next)
        return;
    m_tag = next;
    refilter();
    Q_EMIT tagChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::clearTag()
{
    if (!m_tag)
        return;
    m_tag.reset();
    refilter();
    Q_EMIT tagChanged();
    Q_EMIT countChanged();
}

QVariant TorrentFilterModel::tracker() const
{
    return m_tracker ? QVariant(*m_tracker) : QVariant();
}

void TorrentFilterModel::setTracker(const QVariant &tracker)
{
    if (!tracker.isValid() || tracker.isNull()) {
        clearTracker();
        return;
    }
    const QString next = tracker.toString();
    if (m_tracker && *m_tracker == next)
        return;
    m_tracker = next;
    reloadTrackerHashes();
    refilter();
    Q_EMIT trackerChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::clearTracker()
{
    if (!m_tracker)
        return;
    m_tracker.reset();
    m_trackerHashes.clear();
    refilter();
    Q_EMIT trackerChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::setTrackerModel(TrackerModel *model)
{
    if (m_trackerModel == model)
        return;
    if (m_trackerModel)
        disconnect(m_trackerModel, nullptr, this, nullptr);
    m_trackerModel = model;
    if (model != nullptr) {
        // A poll that moves a torrent onto or off a host re-filters, but only when the
        // filtered host's own membership moved.
        const auto follow = [this]() {
            if (reloadTrackerHashes())
                refilter();
        };
        connect(model, &QAbstractItemModel::dataChanged, this, follow);
        connect(model, &QAbstractItemModel::modelReset, this, follow);
        connect(model, &QAbstractItemModel::rowsInserted, this, follow);
        connect(model, &QAbstractItemModel::rowsRemoved, this, follow);
    }
    reloadTrackerHashes();
    if (m_tracker)
        refilter();
}

bool TorrentFilterModel::usesTrackerMembership() const
{
    return m_trackerModel && m_tracker && !m_tracker->isEmpty() && *m_tracker != trackerlessKey();
}

bool TorrentFilterModel::reloadTrackerHashes()
{
    QSet<QString> next;
    if (usesTrackerMembership()) {
        const QStringList hashes = m_trackerModel->hashesFor(*m_tracker);
        next = QSet<QString>(hashes.cbegin(), hashes.cend());
    }
    if (next == m_trackerHashes)
        return false;
    m_trackerHashes = std::move(next);
    return true;
}

void TorrentFilterModel::setText(const QString &text)
{
    const QString next = text.trimmed();
    if (m_text == next)
        return;
    m_text = next;
    refilter();
    Q_EMIT textChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::setSortRole(int role)
{
    if (m_sortRole == role)
        return;
    m_sortRole = role;
    // With a dynamic sort, the base class re-sorts on a new role by itself (lessThan reads
    // m_sortRole, set above); a second sort, or an invalidate(), would be two more passes.
    QSortFilterProxyModel::setSortRole(role);
    Q_EMIT sortRoleChanged();
}

QString TorrentFilterModel::sortRoleName() const
{
    if (sourceModel() == nullptr)
        return {};
    return QString::fromUtf8(sourceModel()->roleNames().value(m_sortRole));
}

void TorrentFilterModel::setSortByRoleName(const QString &roleName)
{
    if (sourceModel() == nullptr)
        return;
    const QHash<int, QByteArray> names = sourceModel()->roleNames();
    const QByteArray wanted = roleName.toUtf8();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (it.value() == wanted) {
            setSortRole(it.key());
            return;
        }
    }
}

void TorrentFilterModel::setSortOrder(Qt::SortOrder order)
{
    if (m_sortOrder == order)
        return;
    m_sortOrder = order;
    resort();
    Q_EMIT sortOrderChanged();
}

void TorrentFilterModel::refilter()
{
    beginFilterChange();
    endFilterChange(Direction::Rows);
}

void TorrentFilterModel::resort()
{
    // One pass: sort() on a new order re-sorts the mapping; no invalidate() on top.
    sort(0, m_sortOrder);
}

int TorrentFilterModel::rowOfHash(const QString &hash) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return -1;
    const int sourceRow = source->indexOfHash(hash);
    if (sourceRow < 0)
        return -1;
    const QModelIndex proxyIndex = mapFromSource(source->index(sourceRow));
    return proxyIndex.isValid() ? proxyIndex.row() : -1;
}

QString TorrentFilterModel::hashAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return data(index(row, 0), TorrentModel::HashRole).toString();
}

bool TorrentFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent)
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return false;
    const Torrent &t = source->torrentAt(sourceRow);
    if (t.hash.isEmpty())
        return false;

    if (m_statusFilter != All && !t.statusGroups().testFlag(groupFor(m_statusFilter)))
        return false;

    if (m_category && t.category != *m_category)
        return false;

    if (m_tag) {
        if (m_tag->isEmpty()) {
            if (!t.tags.isEmpty())
                return false;
        } else if (!t.tags.contains(*m_tag)) {
            return false;
        }
    }

    if (m_tracker) {
        // The trackerless row (and the three-valued "none"): no tracker at all, as
        // FilterCounts counts it (review Q11).
        if (m_tracker->isEmpty() || *m_tracker == trackerlessKey()) {
            if (!t.isTrackerless())
                return false;
        } else if (usesTrackerMembership()) {
            if (!m_trackerHashes.contains(t.hash))
                return false;
        } else if (t.trackerHostKey() != *m_tracker) {
            return false;
        }
    }

    if (!m_text.isEmpty()) {
        const Qt::CaseSensitivity ci = Qt::CaseInsensitive;
        const bool matches =
            t.name.contains(m_text, ci) || t.hash.startsWith(m_text, ci) ||
            t.category.contains(m_text, ci) ||
            std::any_of(t.tags.cbegin(), t.tags.cend(), [this, ci](const QString &tag) {
                return tag.contains(m_text, ci);
            });
        if (!matches)
            return false;
    }

    return true;
}

bool TorrentFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return false;

    const Torrent &a = source->torrentAt(left.row());
    const Torrent &b = source->torrentAt(right.row());

    const QVariant leftValue = source->data(left, m_sortRole);
    const QVariant rightValue = source->data(right, m_sortRole);

    int compared = 0;
    if (leftValue.typeId() == QMetaType::QString && rightValue.typeId() == QMetaType::QString)
        compared = m_collator.compare(leftValue.toString(), rightValue.toString());
    else if (leftValue.typeId() == QMetaType::QDateTime)
        compared = leftValue.toDateTime() == rightValue.toDateTime()
                       ? 0
                       : (leftValue.toDateTime() < rightValue.toDateTime() ? -1 : 1);
    else if (leftValue.canConvert<double>() && rightValue.canConvert<double>()) {
        const double l = leftValue.toDouble();
        const double r = rightValue.toDouble();
        compared = l == r ? 0 : (l < r ? -1 : 1);
    }

    if (compared != 0)
        return compared < 0;

    // A total order, so a speed tick never shuffles rows sorted by something else. Ties read
    // a to z whichever way the column sorts: the proxy reverses lessThan() for a descending
    // sort, so the tie-break is reversed first (a library idle under "↓" lists by name).
    compared = m_collator.compare(a.name, b.name);
    if (compared == 0)
        compared = a.hash.compare(b.hash);
    return sortOrder() == Qt::DescendingOrder ? compared > 0 : compared < 0;
}

} // namespace omaseed::core
