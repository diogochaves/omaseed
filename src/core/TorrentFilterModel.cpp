#include "core/TorrentFilterModel.hpp"

#include "core/LocalDay.hpp"
#include "core/RowWords.hpp"
#include "core/SharedData.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <limits>
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

    // C24 H3: the groups follow the rows. A poll's runs rebuild them once, in releaseSort().
    const auto regroup = [this]() {
        if (m_groupBy != NoGroups && !m_inRuns)
            rebuildGroups();
    };
    connect(this, &QAbstractItemModel::rowsInserted, this, regroup);
    connect(this, &QAbstractItemModel::rowsRemoved, this, regroup);
    connect(this, &QAbstractItemModel::modelReset, this, regroup);
    connect(this, &QAbstractItemModel::layoutChanged, this, regroup);
    // The roles the proxy answers itself are made of the source's: a change to any of a
    // row's roles names them too, so a delegate bound to `rowSentence` follows the speed.
    connect(this,
            &QAbstractItemModel::dataChanged,
            this,
            [this](const QModelIndex &topLeft,
                   const QModelIndex &bottomRight,
                   const QList<int> &roles) {
                if (roles.isEmpty() || roles.contains(RowFactRole))
                    return;
                Q_EMIT dataChanged(topLeft,
                                   bottomRight,
                                   {QuestionRole,
                                    GroupRole,
                                    RowFactRole,
                                    RowCaptionRole,
                                    RowSentenceRole,
                                    RowMetaRole,
                                    RowFixRole,
                                    RowFixActionRole,
                                    MonogramRole});
            });
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

TorrentFilterModel::Question
TorrentFilterModel::questionOf(const Torrent &torrent, bool logged, qint64 dayStartMSecs)
{
    // DECISIONS C24 H2, #167's assumed answers: disjoint, the first that holds.
    const StatusGroups groups = torrent.statusGroups();
    if (logged || groups.testFlag(StatusGroup::Errored))
        return NeedsYou;
    const bool complete = groups.testFlag(StatusGroup::Completed);
    if (complete && torrent.completionOn.isValid() &&
        torrent.completionOn.toMSecsSinceEpoch() >= dayStartMSecs)
        return LandedToday;
    if (groups.testFlag(StatusGroup::Stopped))
        return Resting;
    return complete ? GivingBack : Arriving;
}

namespace {

/// The StatusFilter rows' words, in the enum's order after All.
const QStringList &statusWords()
{
    static const QStringList words{QStringLiteral("downloading"),
                                   QStringLiteral("seeding"),
                                   QStringLiteral("completed"),
                                   QStringLiteral("stopped"),
                                   QStringLiteral("active"),
                                   QStringLiteral("stalled"),
                                   QStringLiteral("checking"),
                                   QStringLiteral("errored")};
    return words;
}

const QLatin1StringView kIsPrefix("is:");

} // namespace

QStringList TorrentFilterModel::isWords()
{
    return statusWords();
}

TorrentFilterModel::StatusFilter TorrentFilterModel::statusFilterForWord(QStringView word)
{
    if (word.isEmpty())
        return All;
    int found = -1;
    for (int i = 0; i < statusWords().size(); ++i) {
        const QString &name = statusWords().at(i);
        if (name.compare(word, Qt::CaseInsensitive) == 0)
            return StatusFilter(i + 1);
        if (name.startsWith(word, Qt::CaseInsensitive)) {
            if (found >= 0)
                return All; // a prefix two words share
            found = i;
        }
    }
    return found >= 0 ? StatusFilter(found + 1) : All;
}

void TorrentFilterModel::setQuestion(Question question)
{
    if (m_question == question)
        return;
    m_question = question;
    applyFilterChange();
    Q_EMIT questionChanged();
    Q_EMIT countChanged();
}

void TorrentFilterModel::setDay(LocalDay *day)
{
    if (m_day == day)
        return;
    if (m_day != nullptr)
        disconnect(m_day, nullptr, this, nullptr);
    m_day = day;
    if (m_day != nullptr) {
        connect(m_day, &LocalDay::dayChanged, this, [this]() {
            // A new day: what landed yesterday is giving back or resting now.
            regroupAll();
            if (m_question == AnyQuestion)
                return;
            refilter();
            Q_EMIT countChanged();
        });
    }
}

qint64 TorrentFilterModel::dayStartMSecs() const
{
    if (m_day != nullptr)
        return m_day->startMSecs();
    return QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
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
        // C24 H2: a problem or failure in the log makes a torrent need you, or stop needing
        // you, with no poll.
        connect(torrents,
                &TorrentModel::problemsChanged,
                this,
                [this]() {
                    if (m_question == AnyQuestion)
                        return;
                    refilter();
                    Q_EMIT countChanged();
                }),
        // C24 H3: after Qt's own handling (connected first, in the base class), a shown
        // torrent whose group moved without a re-sort asks for one.
        connect(torrents,
                &QAbstractItemModel::dataChanged,
                this,
                [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                    noteGroupChanges(topLeft.row(), bottomRight.row());
                }),
    };
}

void TorrentFilterModel::holdSortFor(const QList<int> &rows, const QList<int> &roles)
{
    m_inRuns = true;
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
    m_inRuns = false;
    const bool regroup = m_regroupPending;
    m_regroupPending = false;
    if (m_sortHeld) {
        m_sortHeld = false;
        sortNow();
        if (m_refilterAfterHold) {
            m_refilterAfterHold = false;
            refilter();
        }
    } else if (regroup) {
        sortNow();
    }
    // The runs moved speeds and counts the groups' lines say, if nothing else.
    if (m_groupBy != NoGroups)
        rebuildGroups();
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
        dropHeldOrder();
        // Mid-bracket (never from the table, which sets this between polls), the bracket's
        // own release sorts.
        if (!m_sortHeld)
            sortNow();
    }
    Q_EMIT holdOrderChanged();
}

void TorrentFilterModel::freezeOrder()
{
    dropHeldOrder();
    const int count = rowCount();
    m_heldPlaces.reserve(count);
    for (int row = 0; row < count; ++row)
        m_heldPlaces.insert(hashAt(row), row);
    if (const auto *source = qobject_cast<const TorrentModel *>(sourceModel())) {
        m_heldLibrary.reserve(source->rowCount());
        for (int row = 0; row < source->rowCount(); ++row)
            m_heldLibrary.insert(source->torrentAt(row).hash);
    }
}

void TorrentFilterModel::dropHeldOrder()
{
    m_heldPlaces.clear();
    m_heldLibrary.clear();
    m_heldSlots.clear();
}

double TorrentFilterModel::heldPlace(int sourceRow) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    const QString &hash = source->torrentAt(sourceRow).hash;
    if (const auto held = m_heldPlaces.constFind(hash); held != m_heldPlaces.cend())
        return *held;
    if (const auto slot = m_heldSlots.constFind(hash); slot != m_heldSlots.cend())
        return *slot;
    // #51 D9: a torrent already in the library that only now matches the filter waits below.
    if (m_heldLibrary.contains(hash))
        return std::numeric_limits<double>::max();
    // DECISIONS C20 (#124): an add takes its sorted place at once. Its slot is half a row
    // after the last held row the live order puts before it (or above the first), so the held
    // rows keep their order around it; two new ones in one slot follow the live order.
    const QModelIndex fresh = source->index(sourceRow, 0);
    const auto viewBefore = [&](const QModelIndex &a, const QModelIndex &b) {
        return sortOrder() == Qt::DescendingOrder ? liveLessThan(b, a) : liveLessThan(a, b);
    };
    double place = -0.5;
    for (auto held = m_heldPlaces.cbegin(); held != m_heldPlaces.cend(); ++held) {
        if (held.value() + 0.5 <= place)
            continue;
        const int row = source->indexOfHash(held.key());
        if (row >= 0 && viewBefore(source->index(row, 0), fresh))
            place = held.value() + 0.5;
    }
    m_heldSlots.insert(hash, place);
    return place;
}

void TorrentFilterModel::applyFilterChange()
{
    if (!m_holdOrder) {
        refilter();
        return;
    }
    // Sorted first, so the rows the new filter lets in are placed among sorted ones.
    dropHeldOrder();
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
    // C24 H2: `is:` words name states; the rest is the query.
    StatusGroups groups;
    QStringList rest;
    for (const QString &word : next.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.startsWith(kIsPrefix, Qt::CaseInsensitive)) {
            const QStringView named = QStringView(word).mid(kIsPrefix.size());
            const StatusFilter filter = statusFilterForWord(named);
            if (filter != All) {
                groups |= groupFor(filter);
                continue;
            }
            bool partOfOne = named.isEmpty();
            for (const QString &state : statusWords())
                partOfOne = partOfOne || state.startsWith(named, Qt::CaseInsensitive);
            if (partOfOne)
                continue; // still being typed: narrows nothing yet
        }
        rest.append(word);
    }
    m_isGroups = groups;
    m_queryText = rest.join(QLatin1Char(' '));
    m_query = SearchWords::parse(m_queryText);
    m_matchRanks.clear();
    applyFilterChange();
    // C22: the ranks came or went with the query, so every row sorts again (a held order
    // already did, in applyFilterChange()).
    if (!m_holdOrder)
        sortNow();
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
    dropHeldOrder();
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
    dropHeldOrder();
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

int TorrentFilterModel::questionOfHash(const QString &hash) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return AnyQuestion;
    const int sourceRow = source->indexOfHash(hash);
    if (sourceRow < 0)
        return AnyQuestion;
    const Torrent &t = source->torrentAt(sourceRow);
    return questionOf(t, source->hasLoggedTrouble(t.hash), dayStartMSecs());
}

bool TorrentFilterModel::hashInScope(const QString &hash) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return false;
    const int sourceRow = source->indexOfHash(hash);
    return sourceRow >= 0 && inScope(source->torrentAt(sourceRow), *source, dayStartMSecs());
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

    if (m_isGroups != StatusGroup::NoGroup && (t.statusGroups() & m_isGroups) != m_isGroups)
        return false;

    if (!inScope(t, *source, dayStartMSecs()))
        return false;

    if (!m_queryText.isEmpty() && !t.hash.startsWith(m_queryText, Qt::CaseInsensitive)) {
        // C22: every word somewhere in the name, the category or a tag. The parts are joined
        // with a character no fold keeps, so a word never spans two of them.
        const QChar apart(0x1f);
        QString haystack = SearchWords::fold(t.name) + apart + SearchWords::fold(t.category);
        for (const QString &tag : t.tags)
            haystack += apart + SearchWords::fold(tag);
        if (!SearchWords::matches(m_query, haystack))
            return false;
    }

    return true;
}

bool TorrentFilterModel::inScope(const Torrent &t,
                                 const TorrentModel &source,
                                 qint64 dayStart) const
{
    if (m_statusFilter != All && !t.statusGroups().testFlag(groupFor(m_statusFilter)))
        return false;

    if (m_question != AnyQuestion &&
        questionOf(t, source.hasLoggedTrouble(t.hash), dayStart) != m_question)
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

    return true;
}

ScopeFacts TorrentFilterModel::scopeFacts() const
{
    ScopeFacts facts;
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return facts;
    const qint64 dayStart = dayStartMSecs();
    for (int row = 0, count = source->rowCount(); row < count; ++row) {
        const Torrent &t = source->torrentAt(row);
        if (t.hash.isEmpty() || !inScope(t, *source, dayStart))
            continue;
        facts.add(t, questionOf(t, source->hasLoggedTrouble(t.hash), dayStart));
    }
    return facts;
}

bool TorrentFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr)
        return false;

    // C24 H3: the group first, in its own order whichever way the column sorts (the proxy
    // reverses lessThan() for a descending sort, so this is reversed first), then the rest.
    if (m_groupBy != NoGroups) {
        const int compared = compareGroups(left, right);
        if (compared != 0)
            return sortOrder() == Qt::DescendingOrder ? compared > 0 : compared < 0;
    }

    if (!m_heldPlaces.isEmpty()) {
        // The held order: the places the rows had, new torrents in their slots among them,
        // and below them, rows new to the view (heldPlace()). Places are the view's order,
        // which a descending sort reads from lessThan() the other way round. Two rows in one
        // slot follow the live order.
        const double l = heldPlace(left.row());
        const double r = heldPlace(right.row());
        if (l != r)
            return sortOrder() == Qt::DescendingOrder ? r < l : l < r;
    }
    return liveLessThan(left, right);
}

bool TorrentFilterModel::liveLessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    int compared = 0;
    // C22: while a query is typed, the better match comes first whichever way the column
    // sorts (the proxy reverses lessThan() for a descending sort, so this is reversed first).
    if (!m_query.isEmpty()) {
        compared = matchRank(left.row()) - matchRank(right.row());
        if (compared != 0)
            return sortOrder() == Qt::DescendingOrder ? compared > 0 : compared < 0;
    }
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

int TorrentFilterModel::matchRank(int sourceRow) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr || sourceRow < 0 || sourceRow >= source->rowCount())
        return SearchWords::NoMatch;
    if (m_matchRanks.size() != source->rowCount())
        m_matchRanks = QList<qint8>(source->rowCount(), qint8(-1));
    qint8 &cached = m_matchRanks[sourceRow];
    if (cached < 0) {
        const Torrent &t = source->torrentAt(sourceRow);
        // A hash prefix ranks with the phrases; one found only in the category or a tag
        // ranks last (its name holds none of the words, or not all of them).
        const SearchWords::Rank rank = t.hash.startsWith(m_text, Qt::CaseInsensitive)
                                           ? SearchWords::Phrase
                                           : SearchWords::rank(m_query, SearchWords::fold(t.name));
        cached = qint8(rank);
    }
    return cached;
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

namespace omaseed::core {

// --- DECISIONS C24 H3: groups, and the rows display's words -------------------------------

namespace {

QString trGroups(const char *text)
{
    return QCoreApplication::translate("TorrentFilterModel", text);
}

} // namespace

QString TorrentFilterModel::questionKey(int question)
{
    switch (question) {
    case NeedsYou:
        return QStringLiteral("needsYou");
    case Arriving:
        return QStringLiteral("arriving");
    case LandedToday:
        return QStringLiteral("landedToday");
    case GivingBack:
        return QStringLiteral("givingBack");
    case Resting:
        return QStringLiteral("resting");
    default:
        return {};
    }
}

void TorrentFilterModel::setGroupBy(GroupBy groupBy)
{
    if (m_groupBy == groupBy)
        return;
    m_groupBy = groupBy;
    // As a new sort role: a held order lets go for it, and holds again from the new order.
    dropHeldOrder();
    sortNow();
    if (m_holdOrder)
        freezeOrder();
    rebuildGroups();
    Q_EMIT groupByChanged();
}

QString TorrentFilterModel::groupKeyOf(const Torrent &torrent, int question) const
{
    switch (m_groupBy) {
    case ByQuestion:
        return questionKey(question);
    case ByCategory:
        return torrent.category;
    case NoGroups:
        break;
    }
    return {};
}

int TorrentFilterModel::compareGroups(const QModelIndex &left, const QModelIndex &right) const
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    const Torrent &l = source->torrentAt(left.row());
    const Torrent &r = source->torrentAt(right.row());
    if (m_groupBy == ByQuestion) {
        const qint64 dayStart = dayStartMSecs();
        return int(questionOf(l, source->hasLoggedTrouble(l.hash), dayStart)) -
               int(questionOf(r, source->hasLoggedTrouble(r.hash), dayStart));
    }
    // By category: by name, the torrents with none last.
    if (l.category == r.category)
        return 0;
    if (l.category.isEmpty())
        return 1;
    if (r.category.isEmpty())
        return -1;
    const int compared = m_collator.compare(l.category, r.category);
    return compared != 0 ? compared : QString::compare(l.category, r.category);
}

void TorrentFilterModel::rebuildGroups()
{
    QVariantList next;
    QHash<QString, QString> keys;
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (m_groupBy != NoGroups && source != nullptr) {
        const qint64 dayStart = dayStartMSecs();
        const int count = rowCount();
        keys.reserve(count);
        QString current;
        int first = 0;
        int question = AnyQuestion;
        ScopeFacts facts;
        const auto close = [&](int end) {
            const QString title = m_groupBy == ByQuestion ? ScopeSentence::questionWord(question)
                                  : current.isEmpty()     ? trGroups("no category")
                                                          : current;
            const QString line = m_groupBy == ByQuestion ? ScopeSentence::groupLine(question, facts)
                                                         : ScopeSentence::shelfLine(facts);
            next.append(QVariantMap{
                {QStringLiteral("key"), current},
                {QStringLiteral("title"), title},
                {QStringLiteral("first"), first},
                {QStringLiteral("count"), end - first},
                {QStringLiteral("line"), line},
                {QStringLiteral("question"), m_groupBy == ByQuestion ? question : int(AnyQuestion)},
            });
        };
        for (int row = 0; row < count; ++row) {
            const Torrent &t = source->torrentAt(mapToSource(index(row, 0)).row());
            const int q = questionOf(t, source->hasLoggedTrouble(t.hash), dayStart);
            const QString key = groupKeyOf(t, q);
            keys.insert(t.hash, key);
            if (row == 0 || key != current) {
                if (row > 0)
                    close(row);
                current = key;
                first = row;
                question = q;
                facts = ScopeFacts();
            }
            facts.add(t, q);
        }
        if (count > 0)
            close(count);
    }
    m_groupOf = std::move(keys);
    if (next != m_groups) {
        m_groups = std::move(next);
        Q_EMIT groupsChanged();
    }
}

void TorrentFilterModel::noteGroupChanges(int first, int last)
{
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (m_groupBy == NoGroups || source == nullptr || m_groupOf.isEmpty())
        return;
    const qint64 dayStart = dayStartMSecs();
    for (int row = std::max(0, first); row <= last && row < source->rowCount(); ++row) {
        const Torrent &t = source->torrentAt(row);
        const auto known = m_groupOf.constFind(t.hash);
        if (known == m_groupOf.cend())
            continue;
        if (groupKeyOf(t, questionOf(t, source->hasLoggedTrouble(t.hash), dayStart)) != *known) {
            m_regroupPending = true;
            break;
        }
    }
    if (m_regroupPending && !m_inRuns) {
        m_regroupPending = false;
        sortNow();
    }
}

void TorrentFilterModel::regroupAll()
{
    if (rowCount() > 0)
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {});
    if (m_groupBy == ByQuestion && !m_inRuns)
        sortNow();
}

QVariant TorrentFilterModel::data(const QModelIndex &index, int role) const
{
    if (role < QuestionRole || role > MonogramRole)
        return QSortFilterProxyModel::data(index, role);
    const auto *source = qobject_cast<const TorrentModel *>(sourceModel());
    if (source == nullptr || !index.isValid())
        return {};
    const QModelIndex at = mapToSource(index);
    const Torrent &t = source->torrentAt(at.row());
    if (role == MonogramRole)
        return RowWords::monogram(t.name);
    const int question = questionOf(t, source->hasLoggedTrouble(t.hash), dayStartMSecs());
    if (role == QuestionRole)
        return question;
    if (role == GroupRole)
        return groupKeyOf(t, question);

    RowTrouble trouble;
    if (question == NeedsYou) {
        const QVariantMap problem = source->data(at, TorrentModel::ProblemRole).toMap();
        const QVariantMap failure = source->data(at, TorrentModel::FailureRole).toMap();
        trouble.cause = source->data(at, TorrentModel::StatusCauseRole).toString();
        trouble.headline = problem.value(QStringLiteral("headline")).toString();
        trouble.actions = problem.value(QStringLiteral("actions")).toStringList();
        trouble.missingFiles = problem.value(QStringLiteral("missingFiles")).toBool();
        trouble.stuckMove = problem.value(QStringLiteral("stuckMove")).toBool();
        trouble.failureKind = failure.value(QStringLiteral("kind")).toString();
        trouble.failureHeadline = failure.value(QStringLiteral("headline")).toString();
        trouble.failureActions = failure.value(QStringLiteral("actions")).toStringList();
    }
    const RowWords words = RowWords::of(t, question, trouble);
    switch (role) {
    case RowFactRole:
        return words.fact;
    case RowCaptionRole:
        return words.caption;
    case RowSentenceRole:
        return words.sentence;
    case RowMetaRole:
        return words.meta;
    case RowFixRole:
        return words.fix;
    case RowFixActionRole:
        return words.fixAction;
    default:
        return {};
    }
}

QHash<int, QByteArray> TorrentFilterModel::roleNames() const
{
    QHash<int, QByteArray> names = QSortFilterProxyModel::roleNames();
    names.insert(QuestionRole, "question");
    names.insert(GroupRole, "group");
    names.insert(RowFactRole, "rowFact");
    names.insert(RowCaptionRole, "rowCaption");
    names.insert(RowSentenceRole, "rowSentence");
    names.insert(RowMetaRole, "rowMeta");
    names.insert(RowFixRole, "rowFix");
    names.insert(RowFixActionRole, "rowFixAction");
    names.insert(MonogramRole, "monogram");
    return names;
}

} // namespace omaseed::core
