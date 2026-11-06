#include "core/SearchModel.hpp"

#include "core/Format.hpp"
#include "core/SearchWords.hpp"

#include <QDateTime>
#include <QVariantMap>

#include <algorithm>

namespace omaseed::core {

namespace {

constexpr int freshDefaultMs = 4000; // search-plan assumed 10: new and +1 copy last 4 s

QString healthName(int seeds)
{
    switch (SearchResults::health(seeds)) {
    case SearchResults::Health::Good:
        return QStringLiteral("good");
    case SearchResults::Health::Few:
        return QStringLiteral("few");
    case SearchResults::Health::Unknown:
        return QStringLiteral("unknown");
    case SearchResults::Health::Dead:
        break;
    }
    return QStringLiteral("dead");
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

SearchModel::SearchModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_now([]() { return QDateTime::currentSecsSinceEpoch(); })
{
    m_freshTimer.setSingleShot(true);
    m_freshTimer.setInterval(freshDefaultMs);
    connect(&m_freshTimer, &QTimer::timeout, this, [this]() {
        const qint64 now = nowMs();
        qint64 next = -1;
        for (int i = 0; i < m_rows.size(); ++i) {
            Shown &shown = m_rows[i];
            if (shown.fresh.isEmpty())
                continue;
            const qint64 until = m_freshUntil.value(shown.row.key, 0);
            if (until <= now + 50) {
                shown.fresh.clear();
                m_freshUntil.remove(shown.row.key);
                m_freshKind.remove(shown.row.key);
                Q_EMIT dataChanged(index(i), index(i), {FreshRole});
            } else if (next < 0 || until < next) {
                next = until;
            }
        }
        if (next > 0)
            m_freshTimer.start(int(std::max<qint64>(50, next - now)));
    });
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int SearchModel::foldedCount() const
{
    int folded = 0;
    for (const SearchRow &row : m_all)
        folded += row.copies - 1;
    return folded;
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {NameRole, "name"},
        {MetaRole, "meta"},
        {SizeTextRole, "sizeText"},
        {AgeTextRole, "ageText"},
        {SeedsRole, "seeds"},
        {SeedsTextRole, "seedsText"},
        {SeedsCaptionRole, "seedsCaption"},
        {HealthRole, "health"},
        {SentenceRole, "sentence"},
        {SentenceShortRole, "sentenceShort"},
        {PluginsRole, "plugins"},
        {CopiesRole, "copies"},
        {TrackerCountRole, "trackerCount"},
        {LeechersRole, "leechers"},
        {IsLinkRole, "isLink"},
        {MarkRole, "mark"},
        {MarkTextRole, "markText"},
        {FreshRole, "fresh"},
        {AddWordRole, "addWord"},
        {LinkRole, "link"},
        {PageRole, "page"},
        {HashRole, "hash"},
        {LibraryHashRole, "libraryHash"},
        {PluginRole, "plugin"},
        {PrivateHostRole, "privateHost"},
    };
}

QString SearchModel::markOf(const Shown &shown) const
{
    if (m_added.contains(shown.row.key))
        return QStringLiteral("added");
    if (!shown.match.hash.isEmpty())
        return QStringLiteral("library");
    if (!shown.match.name.isEmpty())
        return QStringLiteral("disk");
    return QString();
}

QString SearchModel::pluginText(const QStringList &plugins) const
{
    return SearchResults::pluginWords(
        plugins, [this](const QString &name) { return m_pluginNames.value(name, name); });
}

QString SearchModel::sentence(const Shown &shown, bool narrow) const
{
    const SearchRow &row = shown.row;
    const QString who = pluginText(row.plugins);
    const QString mark = markOf(shown);
    const auto withWho = [&who](const QString &rest) {
        return who.isEmpty() ? rest : QStringLiteral("%1 · %2").arg(who, rest);
    };
    if (mark == QLatin1String("added")) {
        const QString when = m_added.value(row.key);
        const QString added =
            when.isEmpty() ? QStringLiteral("added") : QStringLiteral("added %1").arg(when);
        if (narrow)
            return added;
        return withWho(shown.match.words.isEmpty()
                           ? added
                           : QStringLiteral("%1 · %2").arg(added, shown.match.words));
    }
    if (mark == QLatin1String("library")) {
        const QString inLibrary =
            shown.match.words.isEmpty()
                ? QStringLiteral("in your library")
                : QStringLiteral("in your library, %1").arg(shown.match.words);
        return narrow ? inLibrary
                      : withWho(QStringLiteral("%1 · trackers… adds what it lacks").arg(inLibrary));
    }
    if (mark == QLatin1String("disk"))
        return narrow
                   ? QStringLiteral("same name and size as one you have")
                   : withWho(
                         QStringLiteral(
                             "same name and size as %1, on disk · the add sheet checks its files")
                             .arg(shown.match.name));
    if (!row.privateHost.isEmpty())
        return narrow ? QStringLiteral("a private tracker of yours")
                      : withWho(QStringLiteral(
                            "a tracker your library knows as private: kept as its own row"));
    if (row.isLink)
        return narrow ? QStringLiteral("a .torrent link · hash at add")
                      : withWho(QStringLiteral("a .torrent link: its info hash is known at add"));
    if (row.seeds < 0)
        return narrow ? QStringLiteral("seeds not reported")
                      : withWho(row.plugins.size() > 1
                                    ? QStringLiteral(
                                          "these plugins don't report seeds: unknown, not dead")
                                    : QStringLiteral(
                                          "this plugin doesn't report seeds: unknown, not dead"));
    const QString leechers =
        row.leechers < 0 ? QString()
                         : (row.leechers == 1 ? QStringLiteral("1 leecher")
                                              : QStringLiteral("%1 leechers").arg(row.leechers));
    if (row.seeds == 0) {
        if (narrow)
            return QStringLiteral("no seeds right now");
        QString rest = QStringLiteral("no seeds when the plugin looked");
        if (!leechers.isEmpty())
            rest += QStringLiteral(" · ") + leechers;
        return withWho(rest);
    }
    if (narrow)
        return row.copies > 1 ? QStringLiteral("%1 · %2 copies").arg(who).arg(row.copies) : who;
    QStringList parts = {who};
    if (row.copies > 1)
        parts.append(QStringLiteral("%1 copies folded, %2 trackers")
                         .arg(row.copies)
                         .arg(row.trackers.size()));
    if (!leechers.isEmpty())
        parts.append(leechers);
    return parts.join(QStringLiteral(" · "));
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Shown &shown = m_rows.at(index.row());
    const SearchRow &row = shown.row;
    switch (role) {
    case KeyRole:
        return row.key;
    case NameRole:
        return row.name;
    case MetaRole: {
        const QString size =
            row.size >= 0 ? Format::size(row.size) : QStringLiteral("size unknown");
        return QStringLiteral("%1 · %2").arg(size, SearchResults::ageText(row.published, m_now()));
    }
    case SizeTextRole:
        return row.size >= 0 ? Format::size(row.size) : QString();
    case AgeTextRole:
        return SearchResults::ageText(row.published, m_now());
    case SeedsRole:
        return row.seeds;
    case SeedsTextRole:
        return SearchResults::seedsText(row.seeds);
    case SeedsCaptionRole:
        return SearchResults::seedsCaption(row.seeds);
    case HealthRole:
        return healthName(row.seeds);
    case SentenceRole:
        return sentence(shown, false);
    case SentenceShortRole:
        return sentence(shown, true);
    case PluginsRole:
        return pluginText(row.plugins);
    case CopiesRole:
        return row.copies;
    case TrackerCountRole:
        return int(row.trackers.size());
    case LeechersRole:
        return row.leechers;
    case IsLinkRole:
        return row.isLink;
    case MarkRole:
        return markOf(shown);
    case MarkTextRole: {
        const QString mark = markOf(shown);
        if (mark == QLatin1String("library"))
            return QStringLiteral("in your library");
        if (mark == QLatin1String("disk"))
            return QStringLiteral("likely on disk");
        return mark;
    }
    case FreshRole:
        return shown.fresh;
    case AddWordRole:
        return !shown.match.hash.isEmpty() ? QStringLiteral("trackers…") : QStringLiteral("add…");
    case LinkRole:
        return row.link;
    case PageRole:
        return row.page;
    case HashRole:
        return row.hash();
    case LibraryHashRole:
        return shown.match.hash;
    case PluginRole:
        return row.plugin;
    case PrivateHostRole:
        return row.privateHost;
    default:
        break;
    }
    return {};
}

void SearchModel::setOrder(const QString &order)
{
    const SearchResults::Order next = SearchResults::orderFromName(order);
    if (next == m_order)
        return;
    m_order = next;
    rebuild(false);
    Q_EMIT orderChanged();
}

void SearchModel::setHideDead(bool hide)
{
    if (hide == m_hideDead)
        return;
    m_hideDead = hide;
    rebuild(false);
    Q_EMIT hideDeadChanged();
}

void SearchModel::setFilterText(const QString &text)
{
    if (text == m_filterText)
        return;
    m_filterText = text;
    rebuild(false);
    Q_EMIT filterTextChanged();
}

void SearchModel::setAnchorKey(const QString &key)
{
    if (key == m_anchorKey)
        return;
    m_anchorKey = key;
    Q_EMIT anchorKeyChanged();
}

void SearchModel::setHits(const QList<SearchHit> &hits)
{
    m_hits = hits;
    rebuild(true);
}

void SearchModel::startOver()
{
    m_copiesSeen.clear();
    m_freshUntil.clear();
    m_freshKind.clear();
    m_added.clear();
    clearLanded();
}

void SearchModel::setPrivateHostOf(std::function<QString(const QStringList &)> privateHostOf)
{
    m_privateHostOf = std::move(privateHostOf);
}

void SearchModel::setLibraryLookup(LibraryLookup lookup)
{
    m_lookup = std::move(lookup);
}

void SearchModel::setPluginNames(const QHash<QString, QString> &names)
{
    // Merged, never dropped: a plugin uninstalled since keeps its own name on the rows it found.
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        m_pluginNames.insert(it.key(), it.value());
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0),
                           index(int(m_rows.size()) - 1),
                           {SentenceRole, SentenceShortRole, PluginsRole});
}

void SearchModel::refreshMarks()
{
    for (int i = 0; i < m_rows.size(); ++i)
        m_rows[i].match = m_lookup ? m_lookup(m_rows.at(i).row) : LibraryMatch();
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
}

void SearchModel::markAdded(const QString &key)
{
    m_added.insert(key, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
    const int row = indexOfKey(key);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row));
}

void SearchModel::clearLanded()
{
    if (m_landedAbove == 0 && m_foldedAbove == 0)
        return;
    m_landedAbove = 0;
    m_foldedAbove = 0;
    Q_EMIT landedChanged();
}

int SearchModel::indexOfKey(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).row.key == key)
            return i;
    return -1;
}

QVariantMap SearchModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return map;
}

const SearchRow *SearchModel::rowFor(const QString &key) const
{
    for (const SearchRow &row : m_all)
        if (row.key == key)
            return &row;
    return nullptr;
}

QVariantMap SearchModel::details(const QString &key) const
{
    const int at = indexOfKey(key);
    QVariantMap map = get(at);
    const SearchRow *row = rowFor(key);
    if (row == nullptr)
        return map;
    QVariantList copies;
    for (int i = 0; i < row->hits.size(); ++i) {
        const SearchHit &hit = row->hits.at(i);
        QStringList fresh;
        if (i > 0)
            for (const QString &url : hit.trackers)
                if (!row->hits.constFirst().trackers.contains(url))
                    fresh.append(url);
        copies.append(QVariantMap{
            {QStringLiteral("plugin"), m_pluginNames.value(hit.plugin, hit.plugin)},
            {QStringLiteral("name"), hit.name},
            {QStringLiteral("seeds"), hit.seeds},
            {QStringLiteral("seedsText"),
             hit.seeds < 0 ? QStringLiteral("seeds not reported")
                           : QStringLiteral("%1 seeds").arg(hit.seeds)},
            {QStringLiteral("trackers"), int(hit.trackers.size())},
            {QStringLiteral("newTrackers"), int(fresh.size())},
            {QStringLiteral("naming"), i == 0},
        });
    }
    // Where each tracker came from, when not the naming copy.
    QVariantList trackers;
    for (const QString &url : row->trackers) {
        QString from;
        if (!row->hits.constFirst().trackers.contains(url)) {
            for (const SearchHit &hit : row->hits)
                if (hit.trackers.contains(url)) {
                    from = m_pluginNames.value(hit.plugin, hit.plugin);
                    break;
                }
        }
        trackers.append(QVariantMap{{QStringLiteral("url"), url}, {QStringLiteral("from"), from}});
    }
    map.insert(QStringLiteral("copyList"), copies);
    // The facts are the naming copy's (board 48: "as plugin a says").
    map.insert(QStringLiteral("namingPlugin"), m_pluginNames.value(row->plugin, row->plugin));
    map.insert(QStringLiteral("trackers"), trackers);
    map.insert(QStringLiteral("hashV1"), row->hashV1);
    map.insert(QStringLiteral("hashV2"), row->hashV2);
    map.insert(
        QStringLiteral("published"),
        row->published > 0
            ? QDateTime::fromSecsSinceEpoch(row->published).toString(QStringLiteral("d MMM yyyy"))
            : QString());
    return map;
}

void SearchModel::rebuild(bool countLanded)
{
    Q_EMIT aboutToRefresh();
    const QList<SearchRow> folded = SearchResults::fold(m_hits, m_privateHostOf);
    const qint64 now = nowMs();
    const bool had = !m_copiesSeen.isEmpty();
    QHash<QString, int> copiesBefore = m_copiesSeen;
    m_copiesSeen.clear();
    for (const SearchRow &row : folded) {
        m_copiesSeen.insert(row.key, row.copies);
        if (!countLanded || !had)
            continue;
        const auto before = copiesBefore.constFind(row.key);
        if (before == copiesBefore.cend()) {
            m_freshUntil.insert(row.key, now + m_freshTimer.interval());
            m_freshKind.insert(row.key, QStringLiteral("new"));
        } else if (row.copies > *before) {
            m_freshUntil.insert(row.key, now + m_freshTimer.interval());
            m_freshKind.insert(row.key, QStringLiteral("copy"));
        }
    }
    m_all = folded;

    // What the view lists: hide dead, `/`, the order.
    const SearchWords::Query query = SearchWords::parse(m_filterText);
    QList<Shown> target;
    int hidden = 0;
    for (const SearchRow &row : std::as_const(m_all)) {
        if (!query.isEmpty() && !SearchWords::matches(query, SearchWords::fold(row.name)))
            continue;
        // SR4: hide dead hides a reported 0 only.
        if (m_hideDead && row.seeds == 0) {
            ++hidden;
            continue;
        }
        Shown shown;
        shown.row = row;
        shown.match = m_lookup ? m_lookup(row) : LibraryMatch();
        shown.fresh = m_freshUntil.value(row.key, 0) > now ? m_freshKind.value(row.key) : QString();
        target.append(shown);
    }
    std::stable_sort(target.begin(), target.end(), [this](const Shown &a, const Shown &b) {
        return SearchResults::before(a.row, b.row, m_order);
    });

    // SR5: what lands above the cursor's row.
    if (countLanded && !m_anchorKey.isEmpty()) {
        QSet<QString> shownBefore;
        for (const Shown &shown : std::as_const(m_rows))
            shownBefore.insert(shown.row.key);
        int landed = 0;
        int foldedIn = 0;
        for (const Shown &shown : std::as_const(target)) {
            if (shown.row.key == m_anchorKey)
                break;
            if (!shownBefore.contains(shown.row.key))
                ++landed;
            else if (shown.row.copies > copiesBefore.value(shown.row.key, shown.row.copies))
                ++foldedIn;
        }
        if (landed > 0 || foldedIn > 0) {
            m_landedAbove += landed;
            m_foldedAbove += foldedIn;
            Q_EMIT landedChanged();
        }
    }

    // The rows move into place one change at a time, so the view keeps its items.
    const int before = int(m_rows.size());
    QSet<QString> keep;
    for (const Shown &shown : std::as_const(target))
        keep.insert(shown.row.key);
    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (keep.contains(m_rows.at(i).row.key))
            continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }
    for (int i = 0; i < target.size(); ++i) {
        const QString &key = target.at(i).row.key;
        if (i < m_rows.size() && m_rows.at(i).row.key == key) {
            m_rows[i] = target.at(i);
            continue;
        }
        int from = -1;
        for (int j = i + 1; j < m_rows.size(); ++j)
            if (m_rows.at(j).row.key == key) {
                from = j;
                break;
            }
        if (from >= 0) {
            beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
            m_rows.move(from, i);
            endMoveRows();
            m_rows[i] = target.at(i);
        } else {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, target.at(i));
            endInsertRows();
        }
    }
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
    if (!m_freshUntil.isEmpty() && !m_freshTimer.isActive())
        m_freshTimer.start();
    Q_UNUSED(before)
    m_hiddenDead = hidden;
    Q_EMIT countChanged();
    Q_EMIT refreshed();
}

} // namespace omaseed::core
