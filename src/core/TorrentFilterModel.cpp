#include "core/TorrentFilterModel.hpp"

#include "core/SharedData.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <algorithm>
#include <numeric>

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
    applyFilterChange();
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
    applyFilterChange();
    Q_EMIT categoryChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::clearCategory()
{
    if (!m_category)
        return;
    m_category.reset();
    applyFilterChange();
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
    applyFilterChange();
    Q_EMIT tagChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::setFolder(const QString &folder)
{
    const QString next = folder.isEmpty() ? QString() : SharedData::normalize(folder);
    if (next == m_folder)
        return;
    m_folder = next;
    applyFilterChange();
    Q_EMIT folderChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::setOnlyHashes(const QStringList &hashes)
{
    if (hashes == m_onlyHashes)
        return;
    m_onlyHashes = hashes;
    m_onlyHashSet = QSet<QString>(hashes.cbegin(), hashes.cend());
    applyFilterChange();
    Q_EMIT onlyHashesChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::clearTag()
{
    if (!m_tag)
        return;
    m_tag.reset();
    applyFilterChange();
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
    applyFilterChange();
    Q_EMIT trackerChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::clearTracker()
{
    if (!m_tracker)
        return;
    m_tracker.reset();
    m_trackerHashes.clear();
    applyFilterChange();
    Q_EMIT trackerChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::setSourceModel(QAbstractItemModel *model)
{
    for (const QMetaObject::Connection &connection : std::as_const(m_sourceConnections))
        disconnect(connection);
    m_sourceConnections.clear();
    dropNameRanks();
    QSortFilterProxyModel::setSourceModel(model);
    auto *torrents = qobject_cast<TorrentModel *>(model);
    if (torrents == nullptr)
        return;
    // The ranks go stale on the "about to" signals, before Qt's own handlers compare rows:
    // inserted rows are placed with lessThan().
    const auto drop = [this]() { dropNameRanks(); };
    m_sourceConnections = {
        connect(torrents, &QAbstractItemModel::rowsAboutToBeInserted, this, drop),
        connect(torrents, &QAbstractItemModel::rowsAboutToBeRemoved, this, drop),
        connect(torrents, &QAbstractItemModel::rowsAboutToBeMoved, this, drop),
        connect(torrents, &QAbstractItemModel::modelAboutToBeReset, this, drop),
        connect(torrents, &QAbstractItemModel::layoutAboutToBeChanged, this, drop),
        connect(torrents,
                &TorrentModel::runsAboutToBeAnnounced,
                this,
                &TorrentFilterModel::holdSortFor),
        connect(torrents, &TorrentModel::runsAnnounced, this, &TorrentFilterModel::releaseSort),
    };
}

void TorrentFilterModel::holdSortFor(const QList<int> &rows, const QList<int> &roles)
{
    if (roles.contains(TorrentModel::NameRole))
        dropNameRanks();
    if (!dynamicSortFilter() || !roles.contains(m_sortRole))
        return;
    // One run is one re-sort at most: Qt's own partial re-sort is the cheapest then.
    bool severalRuns = false;
    for (int i = 1; i < rows.size() && !severalRuns; ++i)
        severalRuns = rows.at(i) != rows.at(i - 1) + 1;
    if (!severalRuns)
        return;
    // The rows hold their new values and the mapping is still the old order: if every touched
    // row still sits between its neighbours, the runs reorder nothing and Qt's per-run check
    // finds that cheaply. Otherwise one full sort (about 1 ms at 3,500 rows) beats a
    // layoutChanged per run, which the table pays for again.
    if (std::none_of(rows.cbegin(), rows.cend(), [this](int row) { return outOfOrder(row); }))
        return;

    // Held, Qt passes the runs on as plain changes, filter included: note whether a row
    // entered or left the filter, to re-filter after.
    const QAbstractItemModel *source = sourceModel();
    m_refilterAfterHold = std::any_of(rows.cbegin(), rows.cend(), [&](int row) {
        return mapFromSource(source->index(row, 0)).isValid() != filterAcceptsRow(row, {});
    });
    m_sortHeld = true;
    setDynamicSortFilter(false);
}

void TorrentFilterModel::releaseSort()
{
    if (!m_sortHeld)
        return;
    m_sortHeld = false;
    sortNow();
    if (m_refilterAfterHold) {
        m_refilterAfterHold = false;
        refilter();
    }
}

void TorrentFilterModel::sortNow()
{
    // The sort compares each row about 2 log n times: read a numeric role once per row instead.
    // A held order reads the held places, not the role.
    const QAbstractItemModel *source = sourceModel();
    const int count = source != nullptr && m_heldPlaces.isEmpty() ? source->rowCount() : 0;
    m_heldKeys.resize(count);
    for (int row = 0; row < count; ++row) {
        const QVariant value = source->data(source->index(row, 0), m_sortRole);
        if (value.typeId() == QMetaType::QString || value.typeId() == QMetaType::QDateTime ||
            !value.canConvert<double>()) {
            m_heldKeys.clear(); // not a number: lessThan() reads the role as usual
            break;
        }
        m_heldKeys[row] = value.toDouble();
    }
    // Already dynamic, this still sorts every row once: one layoutChanged. (sort() with the
    // same column and order would return without sorting.)
    setDynamicSortFilter(true);
    m_heldKeys.clear();
}

void TorrentFilterModel::setHoldOrder(bool hold)
{
    if (m_holdOrder == hold)
        return;
    m_holdOrder = hold;
    if (hold) {
        freezeOrder();
    } else {
        m_heldPlaces.clear();
        // Mid-bracket (never from the table, which sets this between polls), the bracket's
        // own release sorts.
        if (!m_sortHeld)
            sortNow();
    }
    Q_EMIT holdOrderChanged();
}

void TorrentFilterModel::freezeOrder()
{
    m_heldPlaces.clear();
    const int count = rowCount();
    m_heldPlaces.reserve(count);
    for (int row = 0; row < count; ++row)
        m_heldPlaces.insert(hashAt(row), row);
}

void TorrentFilterModel::applyFilterChange()
{
    if (!m_holdOrder) {
        refilter();
        return;
    }
    // Sorted first, so the rows the new filter lets in are placed among sorted ones.
    m_heldPlaces.clear();
    sortNow();
    refilter();
    freezeOrder();
}

bool TorrentFilterModel::outOfOrder(int sourceRow) const
{
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0);
    const QModelIndex proxyIndex = mapFromSource(sourceIndex);
    if (!proxyIndex.isValid())
        return false;
    // lessThan() is the ascending order; a descending sort reads it the other way round.
    const auto before = [this](const QModelIndex &a, const QModelIndex &b) {
        return sortOrder() == Qt::AscendingOrder ? lessThan(a, b) : lessThan(b, a);
    };
    const int row = proxyIndex.row();
    if (row > 0 && before(sourceIndex, mapToSource(index(row - 1, 0))))
        return true;
    return row + 1 < rowCount() && before(mapToSource(index(row + 1, 0)), sourceIndex);
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
    applyFilterChange();
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
    // A held order lets go for it, and holds again from the new order.
    m_heldPlaces.clear();
    QSortFilterProxyModel::setSortRole(role);
    if (m_holdOrder)
        freezeOrder();
    Q_EMIT sortRoleChanged();
}

QString TorrentFilterModel::sortRoleName() const
{
    if (sourceModel() == nullptr)
        return {};
    return QString::fromUtf8(sourceModel()->roleNames().value(m_sortRole));
}

void TorrentFilterModel::selectRows(QItemSelectionModel *selection,
                                    int from,
                                    int to,
                                    bool replace) const
{
    const int count = rowCount();
    if (selection == nullptr || selection->model() != this || count == 0)
        return;
    const int first = std::clamp(std::min(from, to), 0, count - 1);
    const int last = std::clamp(std::max(from, to), 0, count - 1);
    selection->select(QItemSelection(index(first, 0), index(last, 0)),
                      replace ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select);
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
    m_heldPlaces.clear();
    resort();
    if (m_holdOrder)
        freezeOrder();
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

    if (!m_onlyHashSet.isEmpty() && !m_onlyHashSet.contains(t.hash))
        return false;

    if (!m_folder.isEmpty()) {
        const QString save = SharedData::normalize(t.savePath);
        if (!SharedData::isUnder(save, m_folder))
            return false;
    }

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

    if (!m_heldPlaces.isEmpty()) {
        // The held order: the places the rows had, and below them, rows new to the view in
        // the live order. `before` is the view's order, which a descending sort reads from
        // lessThan() the other way round.
        const auto l = m_heldPlaces.constFind(source->torrentAt(left.row()).hash);
        const auto r = m_heldPlaces.constFind(source->torrentAt(right.row()).hash);
        const bool leftHeld = l != m_heldPlaces.cend();
        const bool rightHeld = r != m_heldPlaces.cend();
        if (leftHeld || rightHeld) {
            const auto before = [&](bool aHeld, auto a, bool bHeld, auto b) {
                return aHeld && bHeld ? *a < *b : aHeld;
            };
            return sortOrder() == Qt::DescendingOrder ? before(rightHeld, r, leftHeld, l)
                                                      : before(leftHeld, l, rightHeld, r);
        }
    }

    int compared = 0;
    if (!m_heldKeys.isEmpty()) {
        const double l = m_heldKeys.at(left.row());
        const double r = m_heldKeys.at(right.row());
        compared = l == r ? 0 : (l < r ? -1 : 1);
    } else {
        compared = compareValues(source->data(left, m_sortRole), source->data(right, m_sortRole));
    }
    if (compared != 0)
        return compared < 0;

    // A total order, so a speed tick never shuffles rows sorted by something else. Ties read
    // a to z whichever way the column sorts: the proxy reverses lessThan() for a descending
    // sort, so the tie-break is reversed first (a library idle under "↓" lists by name).
    compared = nameRank(left.row()) - nameRank(right.row());
    return sortOrder() == Qt::DescendingOrder ? compared > 0 : compared < 0;
}

int TorrentFilterModel::compareValues(const QVariant &left, const QVariant &right) const
{
    if (left.typeId() == QMetaType::QString && right.typeId() == QMetaType::QString)
        return m_collator.compare(left.toString(), right.toString());
    if (left.typeId() == QMetaType::QDateTime)
        return left.toDateTime() == right.toDateTime()
                   ? 0
                   : (left.toDateTime() < right.toDateTime() ? -1 : 1);
    if (left.canConvert<double>() && right.canConvert<double>()) {
        const double l = left.toDouble();
        const double r = right.toDouble();
        return l == r ? 0 : (l < r ? -1 : 1);
    }
    return 0;
}

int TorrentFilterModel::nameRank(int sourceRow) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return 0;
    const int count = source->rowCount();
    if (m_nameRanks.size() != count) {
        QList<int> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [this, source](int l, int r) {
            const Torrent &a = source->torrentAt(l);
            const Torrent &b = source->torrentAt(r);
            const int byName = m_collator.compare(a.name, b.name);
            return byName != 0 ? byName < 0 : a.hash < b.hash;
        });
        m_nameRanks.resize(count);
        for (int rank = 0; rank < count; ++rank)
            m_nameRanks[order.at(rank)] = rank;
    }
    return sourceRow >= 0 && sourceRow < count ? m_nameRanks.at(sourceRow) : 0;
}

} // namespace omaseed::core
