#include "core/TorrentFilesModel.hpp"

#include "core/Format.hpp"

#include <QCollator>
#include <QHash>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <array>
#include <functional>

namespace omaseed::core {

namespace {

/// Natural, case-blind name order ("ep2" before "ep10"), the way a file manager sorts.
int compareNames(const QString &left, const QString &right)
{
    static const QCollator collator = [] {
        QCollator made;
        made.setNumericMode(true);
        made.setCaseSensitivity(Qt::CaseInsensitive);
        return made;
    }();
    return collator.compare(left, right);
}

template<typename T>
int compareValues(T left, T right)
{
    return left < right ? -1 : (right < left ? 1 : 0);
}

QList<int> toIntList(const QVariantList &values)
{
    QList<int> ids;
    ids.reserve(values.size());
    for (const QVariant &value : values) {
        bool ok = false;
        const int id = value.toInt(&ok);
        if (ok)
            ids.append(id);
    }
    return ids;
}

QVariantList toVariantList(const QList<int> &ids)
{
    QVariantList values;
    values.reserve(ids.size());
    for (int id : ids)
        values.append(id);
    return values;
}

} // namespace

QString filePriorityText(int priority)
{
    switch (priority) {
    case int(FilePriority::Mixed):
        return QStringLiteral("mixed");
    case int(FilePriority::Skip):
        return QStringLiteral("skip");
    case int(FilePriority::High):
        return QStringLiteral("high");
    case int(FilePriority::Maximal):
        return QStringLiteral("maximal");
    default:
        // qBittorrent only ever sends 0, 1, 6 and 7, but an unknown value is still a file
        // the user is downloading; call it normal rather than inventing a word.
        return QStringLiteral("normal");
    }
}

TorrentFilesModel::TorrentFilesModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int TorrentFilesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QHash<int, QByteArray> TorrentFilesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {SizeRole, "size"},
        {SizeTextRole, "sizeText"},
        {ProgressRole, "progress"},
        {ProgressTextRole, "progressText"},
        {PriorityRole, "priority"},
        {PriorityTextRole, "priorityText"},
        {IsFolderRole, "isFolder"},
        {DepthRole, "depth"},
        {ExpandedRole, "expanded"},
        {FileIndexesRole, "fileIndexes"},
        {AvailabilityRole, "availability"},
        {AvailabilityTextRole, "availabilityText"},
        {RemainingRole, "remaining"},
        {RemainingTextRole, "remainingText"},
        {PieceFirstRole, "pieceFirst"},
        {PieceLastRole, "pieceLast"},
        {PiecesTextRole, "piecesText"},
        {SelectedRole, "rowSelected"},
        {WantedRole, "wanted"},
    };
}

QVariant TorrentFilesModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= int(m_visible.size()))
        return {};
    const Node &node = m_nodes.at(m_visible.at(index.row()));

    switch (role) {
    case NameRole:
        return node.isFolder ? node.name + QLatin1Char('/') : node.name;
    case PathRole:
        return node.path;
    case SizeRole:
        return node.size;
    case SizeTextRole:
        return Format::size(node.size);
    case ProgressRole:
        return node.progress;
    case ProgressTextRole:
        return Format::percent(node.progress);
    case PriorityRole:
        return node.priority;
    case PriorityTextRole:
        return filePriorityText(node.priority);
    case IsFolderRole:
        return node.isFolder;
    case DepthRole:
        return node.depth;
    case ExpandedRole:
        return node.isFolder && (!m_filterText.isEmpty() || !m_folded.contains(node.path));
    case FileIndexesRole:
        return toVariantList(node.shownIds);
    case AvailabilityRole:
        return node.availability;
    case AvailabilityTextRole:
        return node.availability < 0 ? QStringLiteral("—") : Format::percent(node.availability);
    case RemainingRole:
        return node.remaining;
    case RemainingTextRole:
        return Format::size(node.remaining);
    case PieceFirstRole:
        return node.pieceFirst;
    case PieceLastRole:
        return node.pieceLast;
    case PiecesTextRole:
        if (node.pieceFirst < 0)
            return QStringLiteral("—");
        if (node.pieceFirst == node.pieceLast)
            return QString::number(node.pieceFirst);
        return QStringLiteral("%1–%2").arg(node.pieceFirst).arg(node.pieceLast);
    case SelectedRole:
        return m_selected.contains(node.path);
    case WantedRole:
        if (!node.isFolder)
            return node.priority == int(FilePriority::Skip) ? 0 : 2;
        return node.skippedFiles == 0 ? 2 : node.skippedFiles == node.files ? 0 : 1;
    default:
        return {};
    }
}

// --- building ----------------------------------------------------------------------------

QStringList TorrentFilesModel::shapeOf(const QJsonArray &files)
{
    QStringList shape;
    shape.reserve(files.size());
    for (const QJsonValue &value : files)
        shape.append(value.toObject().value(QStringLiteral("name")).toString());
    return shape;
}

void TorrentFilesModel::readFile(Node &node, const QJsonObject &file)
{
    node.fileIndex = file.value(QStringLiteral("index")).toInt(node.fileIndex);
    node.fileIndexes = {node.fileIndex};
    node.size = qint64(file.value(QStringLiteral("size")).toDouble());
    node.progress = file.value(QStringLiteral("progress")).toDouble();
    node.priority = file.value(QStringLiteral("priority")).toInt(node.priority);
    node.availability = file.value(QStringLiteral("availability")).toDouble(-1.0);
    const QJsonArray range = file.value(QStringLiteral("piece_range")).toArray();
    node.pieceFirst = range.size() == 2 ? range.at(0).toInt(-1) : -1;
    node.pieceLast = range.size() == 2 ? range.at(1).toInt(-1) : -1;
    // qBittorrent's own content view counts nothing left to fetch on a skipped file.
    node.remaining = node.priority == int(FilePriority::Skip)
                         ? 0
                         : qint64(double(node.size) * (1.0 - node.progress));
}

void TorrentFilesModel::applyFiles(const QJsonArray &files)
{
    if (files.isEmpty()) {
        clear();
        return;
    }

    if (shapeOf(files) == m_shape) {
        updateInPlace(files);
        updateSummary();
        return;
    }

    beginResetModel();
    rebuild(files);
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT selectionChanged();
    updateSummary();
}

void TorrentFilesModel::clear()
{
    if (m_nodes.isEmpty())
        return;
    beginResetModel();
    m_nodes.clear();
    m_roots.clear();
    m_visible.clear();
    m_shape.clear();
    m_files = {};
    m_folded.clear();
    m_selected.clear();
    m_fileCount = 0;
    m_matchCount = 0;
    // A filter belongs to the torrent it was typed for; the sort is a preference and stays.
    const bool hadFilter = !m_filterText.isEmpty();
    m_filterText.clear();
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT selectionChanged();
    if (hadFilter)
        Q_EMIT filterTextChanged();
    updateSummary();
}

void TorrentFilesModel::rebuild(const QJsonArray &files)
{
    m_nodes.clear();
    m_roots.clear();
    m_visible.clear();
    m_shape = shapeOf(files);
    m_files = files;
    m_fileCount = 0;

    // Folder path -> node index, so the second file of a folder finds the folder the first
    // one created instead of making another.
    QHash<QString, int> folders;

    const auto folderFor = [&](const QString &path, int depth) {
        const auto known = folders.constFind(path);
        if (known != folders.constEnd())
            return *known;
        Node node;
        node.path = path;
        node.name = path.section(QLatin1Char('/'), -1);
        node.depth = depth;
        node.isFolder = true;
        m_nodes.append(node);
        const int at = int(m_nodes.size()) - 1;
        folders.insert(path, at);
        return at;
    };

    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        const QString path = file.value(QStringLiteral("name")).toString();
        if (path.isEmpty())
            continue;

        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        int parent = -1;
        QString walked;
        for (int level = 0; level < parts.size() - 1; ++level) {
            walked =
                walked.isEmpty() ? parts.at(level) : walked + QLatin1Char('/') + parts.at(level);
            const int folder = folderFor(walked, level);
            if (parent == -1) {
                if (!m_roots.contains(folder))
                    m_roots.append(folder);
            } else if (!m_nodes[parent].children.contains(folder)) {
                m_nodes[parent].children.append(folder);
                m_nodes[folder].parent = parent;
            }
            parent = folder;
        }

        Node leaf;
        leaf.name = parts.isEmpty() ? path : parts.constLast();
        leaf.path = path;
        leaf.depth = int(parts.size()) - 1;
        leaf.isFolder = false;
        leaf.fileIndex = m_fileCount;
        leaf.order = m_fileCount;
        readFile(leaf, file);
        leaf.parent = parent;
        m_nodes.append(leaf);
        const int at = int(m_nodes.size()) - 1;
        ++m_fileCount;

        if (parent == -1)
            m_roots.append(at);
        else
            m_nodes[parent].children.append(at);
    }

    for (int root : std::as_const(m_roots))
        aggregate(root);

    // A selection survives a new shape only for the paths that still exist.
    QSet<QString> paths;
    for (const Node &node : std::as_const(m_nodes))
        paths.insert(node.path);
    m_selected.intersect(paths);

    applyFilter();
    rebuildVisible();
}

void TorrentFilesModel::updateInPlace(const QJsonArray &files)
{
    // The shape matched, so the n-th file of the array is the n-th leaf of the tree, in the
    // order rebuild() appended them.
    QList<int> leaves;
    leaves.reserve(m_fileCount);
    for (int at = 0; at < int(m_nodes.size()); ++at) {
        if (!m_nodes.at(at).isFolder)
            leaves.append(at);
    }
    if (leaves.size() != files.size()) {
        beginResetModel();
        rebuild(files);
        endResetModel();
        Q_EMIT countChanged();
        return;
    }

    m_files = files;
    const int matchesBefore = m_matchCount;
    for (int at = 0; at < files.size(); ++at)
        readFile(m_nodes[leaves.at(at)], files.at(at).toObject());
    for (int root : std::as_const(m_roots))
        aggregate(root);
    applyFilter();

    // Sorted by a number that moved, the rows may have to change places: they move, so the
    // view keeps its delegates and scroll, and the cursor follows its file (reordered()).
    // Hiding the skipped files, a priority changed on the server shows or hides rows.
    if (m_hideSkipped || (!m_sortKey.isEmpty() && m_sortKey != QLatin1String("name")))
        relayout();
    if (m_matchCount != matchesBefore)
        Q_EMIT countChanged();

    emitAllChanged({SizeRole,
                    SizeTextRole,
                    ProgressRole,
                    ProgressTextRole,
                    PriorityRole,
                    PriorityTextRole,
                    FileIndexesRole,
                    AvailabilityRole,
                    AvailabilityTextRole,
                    RemainingRole,
                    RemainingTextRole,
                    PieceFirstRole,
                    PieceLastRole,
                    PiecesTextRole,
                    WantedRole});
}

void TorrentFilesModel::aggregate(int at)
{
    if (!m_nodes.at(at).isFolder)
        return;

    qint64 size = 0;
    qint64 remaining = 0;
    double done = 0.0;
    qint64 wantedSize = 0;
    double wantedDone = 0.0;
    double available = 0.0;
    qint64 availableOver = 0;
    int priority = int(FilePriority::Mixed);
    int pieceFirst = -1;
    int pieceLast = -1;
    int order = -1;
    bool first = true;
    QList<int> ids;
    int files = 0;
    int skippedFiles = 0;

    const QList<int> children = m_nodes.at(at).children;
    for (int child : children) {
        aggregate(child);
        const Node &kid = m_nodes.at(child);
        size += kid.size;
        remaining += kid.remaining;
        done += double(kid.size) * kid.progress;
        // Progress counts the wanted files only, as qBittorrent's content view does: a
        // folder whose wanted files are all done reads 100 %, whatever it skips.
        if (kid.priority != int(FilePriority::Skip)) {
            wantedSize += kid.size;
            wantedDone += double(kid.size) * kid.progress;
        }
        ids.append(kid.fileIndexes);
        if (kid.isFolder) {
            files += kid.files;
            skippedFiles += kid.skippedFiles;
        } else {
            ++files;
            if (kid.priority == int(FilePriority::Skip))
                ++skippedFiles;
        }
        // Availability is weighted by size over the files that are wanted, as qBittorrent's
        // content view does: a skipped file's pieces say nothing about this download.
        if (kid.priority != int(FilePriority::Skip) && kid.availability >= 0.0) {
            available += double(kid.size) * kid.availability;
            availableOver += kid.size;
        }
        if (kid.pieceFirst >= 0)
            pieceFirst = pieceFirst < 0 ? kid.pieceFirst : std::min(pieceFirst, kid.pieceFirst);
        pieceLast = std::max(pieceLast, kid.pieceLast);
        order = order < 0 ? kid.order : std::min(order, kid.order);
        if (first) {
            priority = kid.priority;
            first = false;
        } else if (priority != kid.priority) {
            priority = int(FilePriority::Mixed);
        }
    }

    std::sort(ids.begin(), ids.end());
    Node &node = m_nodes[at];
    node.size = size;
    node.remaining = remaining;
    // A folder that skips everything still shows what is on disk, over its full size.
    node.progress = wantedSize > 0 ? wantedDone / double(wantedSize)
                    : size > 0     ? done / double(size)
                                   : 0.0;
    node.availability = availableOver > 0 ? available / double(availableOver) : -1.0;
    node.priority = first ? int(FilePriority::Normal) : priority;
    node.fileIndexes = ids;
    node.pieceFirst = pieceFirst;
    node.pieceLast = pieceLast;
    node.order = std::max(order, 0);
    node.files = files;
    node.skippedFiles = skippedFiles;
}

// --- filter and sort ---------------------------------------------------------------------

TorrentFilesModel::Filter TorrentFilesModel::parseFilter(const QString &text)
{
    Filter filter;
    const QStringList alternatives = text.toLower().split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString &alternative : alternatives) {
        QList<FilterWord> words;
        const QStringList parts = alternative.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            FilterWord word;
            word.plain = part;
            word.wildcard = part.contains(QLatin1Char('*'));
            if (word.wildcard) {
                // `*` is any run of characters; everything else is literal, and the pattern
                // may sit anywhere in the path, as a plain word does.
                QStringList pieces = part.split(QLatin1Char('*'));
                for (QString &piece : pieces)
                    piece = QRegularExpression::escape(piece);
                word.pattern = QRegularExpression(pieces.join(QStringLiteral(".*")),
                                                  QRegularExpression::DotMatchesEverythingOption);
            }
            words.append(word);
        }
        // "mkv| " while typing: an alternative of blanks is no alternative.
        if (!words.isEmpty())
            filter.append(words);
    }
    return filter;
}

bool TorrentFilesModel::matches(const Filter &filter, const QString &path)
{
    if (filter.isEmpty())
        return true;
    const QString lower = path.toLower();
    for (const QList<FilterWord> &words : filter) {
        const bool all = std::all_of(words.cbegin(), words.cend(), [&](const FilterWord &word) {
            return word.wildcard ? word.pattern.match(lower).hasMatch()
                                 : lower.contains(word.plain);
        });
        if (all)
            return true;
    }
    return false;
}

bool TorrentFilesModel::filterMatches(const QString &filter, const QString &path)
{
    return matches(parseFilter(filter), path);
}

void TorrentFilesModel::applyFilter()
{
    const Filter filter = parseFilter(m_filterText);
    m_matchCount = 0;
    for (int root : std::as_const(m_roots))
        matchFilter(root, filter);
}

bool TorrentFilesModel::matchFilter(int at, const Filter &filter)
{
    Node &node = m_nodes[at];
    if (!node.isFolder) {
        const bool matches = TorrentFilesModel::matches(filter, node.path) &&
                             !(m_hideSkipped && node.priority == int(FilePriority::Skip));
        node.matches = matches;
        node.shownIds = matches ? node.fileIndexes : QList<int>();
        if (matches)
            ++m_matchCount;
        return matches;
    }

    bool any = false;
    QList<int> shown;
    const QList<int> children = node.children;
    for (int child : children) {
        if (matchFilter(child, filter)) {
            any = true;
            shown.append(m_nodes.at(child).shownIds);
        }
    }
    std::sort(shown.begin(), shown.end());
    m_nodes[at].matches = any;
    m_nodes[at].shownIds = shown;
    return any;
}

void TorrentFilesModel::setFilterText(const QString &text)
{
    if (text == m_filterText)
        return;
    m_filterText = text;
    applyFilter();
    beginResetModel();
    rebuildVisible();
    endResetModel();
    updateHeadWanted();
    Q_EMIT filterTextChanged();
    Q_EMIT countChanged();
}

void TorrentFilesModel::setHideSkipped(bool hide)
{
    if (hide == m_hideSkipped)
        return;
    m_hideSkipped = hide;
    applyFilter();
    // Same rows in another order cannot happen here: the set of rows changes, or nothing.
    relayout();
    Q_EMIT hideSkippedChanged();
    Q_EMIT countChanged();
}

void TorrentFilesModel::setSortKey(const QString &key)
{
    if (key == m_sortKey)
        return;
    m_sortKey = key;
    relayout();
    Q_EMIT sortChanged();
}

void TorrentFilesModel::setSortDescending(bool descending)
{
    if (descending == m_sortDescending)
        return;
    m_sortDescending = descending;
    relayout();
    Q_EMIT sortChanged();
}

void TorrentFilesModel::sortBy(const QString &key)
{
    if (key == m_sortKey) {
        setSortDescending(!m_sortDescending);
        return;
    }
    m_sortKey = key;
    m_sortDescending = startsDescending(key);
    relayout();
    Q_EMIT sortChanged();
}

void TorrentFilesModel::cycleSort(const QString &key)
{
    if (key.isEmpty() || key != m_sortKey)
        sortBy(key);
    else if (m_sortDescending == startsDescending(key))
        setSortDescending(!m_sortDescending);
    else
        sortBy(QString());
}

bool TorrentFilesModel::startsDescending(const QString &key)
{
    return !(key.isEmpty() || key == QLatin1String("name") || key == QLatin1String("pieces"));
}

bool TorrentFilesModel::lessThan(int left, int right) const
{
    const Node &a = m_nodes.at(left);
    const Node &b = m_nodes.at(right);

    int order = 0;
    if (m_sortKey == QLatin1String("name")) {
        // Folders first whichever way the names run, as in qBittorrent's content view.
        if (a.isFolder != b.isFolder)
            return a.isFolder;
        order = compareNames(a.name, b.name);
    } else if (m_sortKey == QLatin1String("size")) {
        order = compareValues(a.size, b.size);
    } else if (m_sortKey == QLatin1String("progress")) {
        order = compareValues(a.progress, b.progress);
    } else if (m_sortKey == QLatin1String("remaining")) {
        order = compareValues(a.remaining, b.remaining);
    } else if (m_sortKey == QLatin1String("priority")) {
        order = compareValues(a.priority, b.priority);
    } else if (m_sortKey == QLatin1String("availability")) {
        order = compareValues(a.availability, b.availability);
    } else if (m_sortKey == QLatin1String("pieces")) {
        order = compareValues(a.pieceFirst, b.pieceFirst);
    }
    if (m_sortDescending)
        order = -order;
    if (order != 0)
        return order < 0;
    // Equal on the key: the torrent's own order, so equal rows never swap between polls.
    return a.order < b.order;
}

QList<int> TorrentFilesModel::sortedChildren(const QList<int> &children) const
{
    QList<int> sorted = children;
    if (m_sortKey.isEmpty()) {
        std::stable_sort(sorted.begin(), sorted.end(), [this](int left, int right) {
            return m_nodes.at(left).order < m_nodes.at(right).order;
        });
    } else {
        std::stable_sort(sorted.begin(), sorted.end(), [this](int left, int right) {
            return lessThan(left, right);
        });
    }
    return sorted;
}

QList<int> TorrentFilesModel::computeVisible() const
{
    QList<int> visible;
    const bool filtering = !m_filterText.isEmpty();
    const std::function<void(int)> walk = [&](int at) {
        const Node &node = m_nodes.at(at);
        if (!node.matches)
            return;
        visible.append(at);
        if (!node.isFolder || (!filtering && m_folded.contains(node.path)))
            return;
        for (int child : sortedChildren(node.children))
            walk(child);
    };
    for (int root : sortedChildren(m_roots))
        walk(root);
    return visible;
}

void TorrentFilesModel::rebuildVisible()
{
    m_visible = computeVisible();
}

bool TorrentFilesModel::relayout()
{
    const QList<int> next = computeVisible();
    if (next == m_visible)
        return false;

    QList<int> before = m_visible;
    QList<int> after = next;
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    if (before == after) {
        reorderVisible(next);
        return true;
    }

    beginResetModel();
    m_visible = next;
    endResetModel();
    Q_EMIT countChanged();
    return true;
}

void TorrentFilesModel::reorderVisible(const QList<int> &next)
{
    // Moves keep every delegate and the scroll position in a QML view (a layoutChanged
    // rebuilds them all). Each move is a linear search, so a very long list takes the one
    // layoutChanged instead, with the persistent indexes carried over by node.
    constexpr int kMoveLimit = 2000;
    if (int(next.size()) <= kMoveLimit) {
        for (int target = 0; target < int(next.size()); ++target) {
            if (m_visible.at(target) == next.at(target))
                continue;
            const int from = int(m_visible.indexOf(next.at(target), target + 1));
            if (from < 0)
                continue; // cannot happen: the two lists hold the same nodes
            beginMoveRows(QModelIndex(), from, from, QModelIndex(), target);
            m_visible.move(from, target);
            endMoveRows();
        }
    } else {
        Q_EMIT layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        const QModelIndexList persistent = persistentIndexList();
        QList<int> nodes;
        nodes.reserve(persistent.size());
        for (const QModelIndex &index : persistent)
            nodes.append(m_visible.value(index.row(), -1));
        m_visible = next;
        QHash<int, int> rowOfNode;
        rowOfNode.reserve(m_visible.size());
        for (int row = 0; row < int(m_visible.size()); ++row)
            rowOfNode.insert(m_visible.at(row), row);
        QModelIndexList moved;
        moved.reserve(persistent.size());
        for (int node : std::as_const(nodes)) {
            const int row = rowOfNode.value(node, -1);
            moved.append(row < 0 ? QModelIndex() : index(row));
        }
        changePersistentIndexList(persistent, moved);
        Q_EMIT layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    }
    Q_EMIT reordered();
}

void TorrentFilesModel::updateHeadWanted()
{
    int files = 0;
    int skipped = 0;
    for (const Node &node : std::as_const(m_nodes)) {
        if (node.isFolder || (!m_filterText.isEmpty() && !filterMatches(m_filterText, node.path)))
            continue;
        ++files;
        if (node.priority == int(FilePriority::Skip))
            ++skipped;
    }
    const int wanted = skipped == 0 ? 2 : skipped == files ? 0 : 1;
    if (wanted == m_headWanted && files == m_headFileCount)
        return;
    m_headWanted = wanted;
    m_headFileCount = files;
    Q_EMIT headWantedChanged();
}

QVariantList TorrentFilesModel::headFileIndexes() const
{
    QList<int> ids;
    for (const Node &node : m_nodes) {
        if (!node.isFolder && (m_filterText.isEmpty() || filterMatches(m_filterText, node.path)))
            ids.append(node.fileIndex);
    }
    std::sort(ids.begin(), ids.end());
    return toVariantList(ids);
}

int TorrentFilesModel::filePriority(int fileIndex) const
{
    for (const Node &node : m_nodes) {
        if (!node.isFolder && node.fileIndex == fileIndex)
            return node.priority;
    }
    return -1;
}

QHash<int, int> TorrentFilesModel::priorityByFileIndex() const
{
    QHash<int, int> priorities;
    for (const Node &node : m_nodes) {
        if (!node.isFolder)
            priorities.insert(node.fileIndex, node.priority);
    }
    return priorities;
}

void TorrentFilesModel::updateSummary()
{
    updateHeadWanted();
    int folders = 0;
    int skipped = 0;
    qint64 size = 0;
    for (const Node &node : std::as_const(m_nodes)) {
        if (node.isFolder) {
            ++folders;
            continue;
        }
        size += node.size;
        if (node.priority == int(FilePriority::Skip))
            ++skipped;
    }
    if (folders == m_folderCount && skipped == m_skippedCount && size == m_totalSize)
        return;
    m_folderCount = folders;
    m_skippedCount = skipped;
    m_totalSize = size;
    Q_EMIT summaryChanged();
}

QString TorrentFilesModel::totalSizeText() const
{
    return Format::size(m_totalSize);
}

void TorrentFilesModel::emitAllChanged(const QList<int> &roles)
{
    if (!m_visible.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_visible.size()) - 1), roles);
}

// --- folding -----------------------------------------------------------------------------

bool TorrentFilesModel::isFolder(int row) const
{
    return row >= 0 && row < int(m_visible.size()) && m_nodes.at(m_visible.at(row)).isFolder;
}

bool TorrentFilesModel::isExpanded(int row) const
{
    if (!isFolder(row))
        return false;
    return !m_filterText.isEmpty() || !m_folded.contains(m_nodes.at(m_visible.at(row)).path);
}

void TorrentFilesModel::setExpanded(int row, bool expanded)
{
    if (!m_filterText.isEmpty() || !isFolder(row) || isExpanded(row) == expanded)
        return;

    const QString path = m_nodes.at(m_visible.at(row)).path;
    if (expanded)
        m_folded.remove(path);
    else
        m_folded.insert(path);
    relayout();
}

void TorrentFilesModel::toggle(int row)
{
    setExpanded(row, !isExpanded(row));
}

void TorrentFilesModel::expandAll()
{
    if (m_folded.isEmpty())
        return;
    m_folded.clear();
    relayout();
}

void TorrentFilesModel::collapseAll()
{
    QSet<QString> folded;
    for (const Node &node : std::as_const(m_nodes)) {
        if (node.isFolder)
            folded.insert(node.path);
    }
    if (folded == m_folded)
        return;
    m_folded = folded;
    relayout();
}

int TorrentFilesModel::parentRow(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return -1;
    const int parent = m_nodes.at(m_visible.at(row)).parent;
    return parent < 0 ? -1 : int(m_visible.indexOf(parent));
}

int TorrentFilesModel::rowOfPath(const QString &path) const
{
    for (int row = 0; row < int(m_visible.size()); ++row) {
        if (m_nodes.at(m_visible.at(row)).path == path)
            return row;
    }
    return -1;
}

// --- selection ---------------------------------------------------------------------------

bool TorrentFilesModel::isSelected(int row) const
{
    return row >= 0 && row < int(m_visible.size()) &&
           m_selected.contains(m_nodes.at(m_visible.at(row)).path);
}

void TorrentFilesModel::setSelected(int row, bool selected)
{
    if (row < 0 || row >= int(m_visible.size()) || isSelected(row) == selected)
        return;
    const QString &path = m_nodes.at(m_visible.at(row)).path;
    if (selected)
        m_selected.insert(path);
    else
        m_selected.remove(path);
    Q_EMIT dataChanged(index(row), index(row), {SelectedRole});
    Q_EMIT selectionChanged();
}

void TorrentFilesModel::toggleSelected(int row)
{
    setSelected(row, !isSelected(row));
}

void TorrentFilesModel::selectRange(int from, int to)
{
    const int first = std::max(0, std::min(from, to));
    const int last = std::min(int(m_visible.size()) - 1, std::max(from, to));
    if (first > last)
        return;
    for (int row = first; row <= last; ++row)
        m_selected.insert(m_nodes.at(m_visible.at(row)).path);
    Q_EMIT dataChanged(index(first), index(last), {SelectedRole});
    Q_EMIT selectionChanged();
}

void TorrentFilesModel::selectAll()
{
    selectRange(0, int(m_visible.size()) - 1);
}

void TorrentFilesModel::selectAllFiles()
{
    const qsizetype before = m_selected.size();
    for (const Node &node : std::as_const(m_nodes)) {
        if (!node.isFolder && node.matches)
            m_selected.insert(node.path);
    }
    if (m_selected.size() == before)
        return;
    emitAllChanged({SelectedRole});
    Q_EMIT selectionChanged();
}

void TorrentFilesModel::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emitAllChanged({SelectedRole});
    Q_EMIT selectionChanged();
}

// --- priorities --------------------------------------------------------------------------

QVariantList TorrentFilesModel::fileIndexes(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return {};
    return toVariantList(m_nodes.at(m_visible.at(row)).shownIds);
}

void TorrentFilesModel::collectLeaves(int at, QList<int> &out) const
{
    const Node &node = m_nodes.at(at);
    if (!node.matches)
        return;
    if (!node.isFolder) {
        out.append(at);
        return;
    }
    for (int child : sortedChildren(node.children))
        collectLeaves(child, out);
}

QList<int> TorrentFilesModel::targetLeaves(int cursorRow) const
{
    QSet<int> targets;
    if (!m_selected.isEmpty()) {
        for (int at = 0; at < int(m_nodes.size()); ++at) {
            if (m_selected.contains(m_nodes.at(at).path))
                targets.insert(at);
        }
    } else if (cursorRow >= 0 && cursorRow < int(m_visible.size())) {
        targets.insert(m_visible.at(cursorRow));
    }

    // Walk the tree in the order it is drawn, so "by shown file order" means what it says,
    // and take every file that is a target or sits under one.
    QList<int> leaves;
    const std::function<void(int)> walk = [&](int at) {
        if (!m_nodes.at(at).matches)
            return;
        if (targets.contains(at)) {
            collectLeaves(at, leaves);
            return;
        }
        for (int child : sortedChildren(m_nodes.at(at).children))
            walk(child);
    };
    for (int root : sortedChildren(m_roots))
        walk(root);
    return leaves;
}

QVariantList TorrentFilesModel::targetFileIndexes(int cursorRow) const
{
    QList<int> ids;
    for (int leaf : targetLeaves(cursorRow))
        ids.append(m_nodes.at(leaf).fileIndex);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return toVariantList(ids);
}

int TorrentFilesModel::priority(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return int(FilePriority::Normal);
    return m_nodes.at(m_visible.at(row)).priority;
}

int TorrentFilesModel::targetPriority(int cursorRow) const
{
    const QList<int> leaves = targetLeaves(cursorRow);
    if (leaves.isEmpty())
        return int(FilePriority::Normal);
    const int first = m_nodes.at(leaves.constFirst()).priority;
    for (int leaf : leaves) {
        if (m_nodes.at(leaf).priority != first)
            return int(FilePriority::Mixed);
    }
    return first;
}

QVariantList TorrentFilesModel::shownOrderGroups(int cursorRow) const
{
    const QList<int> leaves = targetLeaves(cursorRow);
    if (leaves.isEmpty())
        return {};
    const int groupSize = std::max(1, int(leaves.size()) / 3);
    const std::array<int, 3> ladder{
        int(FilePriority::Maximal), int(FilePriority::High), int(FilePriority::Normal)};
    std::array<QList<int>, 3> groups;
    for (int at = 0; at < int(leaves.size()); ++at)
        groups[std::min(2, at / groupSize)].append(m_nodes.at(leaves.at(at)).fileIndex);

    QVariantList result;
    for (size_t group = 0; group < groups.size(); ++group) {
        if (groups[group].isEmpty())
            continue;
        std::sort(groups[group].begin(), groups[group].end());
        result.append(QVariantMap{{QStringLiteral("priority"), ladder[group]},
                                  {QStringLiteral("fileIndexes"), toVariantList(groups[group])}});
    }
    return result;
}

QHash<int, int> TorrentFilesModel::leavesByFileIndex() const
{
    QHash<int, int> leaves;
    leaves.reserve(m_fileCount);
    for (int at = 0; at < int(m_nodes.size()); ++at) {
        if (!m_nodes.at(at).isFolder)
            leaves.insert(m_nodes.at(at).fileIndex, at);
    }
    return leaves;
}

void TorrentFilesModel::setLocalPriority(const QVariantList &fileIndexes, int priority)
{
    // One pass to index the leaves, then a lookup per id: select-all on a 20 000-file
    // torrent was a scan of the tree per id.
    const QHash<int, int> leaves = leavesByFileIndex();
    bool changed = false;
    for (int id : toIntList(fileIndexes)) {
        const int leaf = leaves.value(id, -1);
        if (leaf < 0 || m_nodes.at(leaf).priority == priority)
            continue;
        Node &node = m_nodes[leaf];
        node.priority = priority;
        node.remaining = priority == int(FilePriority::Skip)
                             ? 0
                             : qint64(double(node.size) * (1.0 - node.progress));
        changed = true;
    }
    if (!changed)
        return;
    for (int root : std::as_const(m_roots))
        aggregate(root);
    updateSummary();
    const int matchesBefore = m_matchCount;
    if (m_hideSkipped)
        applyFilter(); // a file just skipped goes, one raised from skip comes back

    relayout();
    if (m_matchCount != matchesBefore)
        Q_EMIT countChanged();
    emitAllChanged({PriorityRole,
                    PriorityTextRole,
                    RemainingRole,
                    RemainingTextRole,
                    AvailabilityRole,
                    AvailabilityTextRole,
                    WantedRole});
}

QString TorrentFilesModel::pathAt(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return {};
    return m_nodes.at(m_visible.at(row)).path;
}

QString TorrentFilesModel::nameAt(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return {};
    return m_nodes.at(m_visible.at(row)).name;
}

QStringList TorrentFilesModel::filePaths() const
{
    QStringList paths;
    paths.reserve(m_files.size());
    for (const QJsonValue &value : std::as_const(m_files))
        paths.append(value.toObject().value(QStringLiteral("name")).toString());
    return paths;
}

QStringList TorrentFilesModel::renameTargets() const
{
    // In the tab's order (its sort), folded folders included: a fold only hides rows.
    QStringList selected;
    QStringList shown;
    const std::function<void(int)> walk = [&](int at) {
        const Node &node = m_nodes.at(at);
        if (!node.matches)
            return;
        if (!node.isFolder) {
            shown.append(node.path);
            if (m_selected.contains(node.path))
                selected.append(node.path);
            return;
        }
        for (int child : sortedChildren(node.children))
            walk(child);
    };
    for (int root : sortedChildren(m_roots))
        walk(root);
    return selected.isEmpty() ? shown : selected;
}

int TorrentFilesModel::selectedFileCount() const
{
    int count = 0;
    for (const Node &node : std::as_const(m_nodes)) {
        if (!node.isFolder && node.matches && m_selected.contains(node.path))
            ++count;
    }
    return count;
}

void TorrentFilesModel::renameLocal(const QString &oldPath, const QString &newPath)
{
    if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath)
        return;

    const QString oldPrefix = oldPath + QLatin1Char('/');
    const auto moved = [&](const QString &path) -> QString {
        if (path == oldPath)
            return newPath;
        if (path.startsWith(oldPrefix))
            return newPath + path.mid(oldPath.size());
        return path;
    };

    QJsonArray files;
    for (const QJsonValue &value : std::as_const(m_files)) {
        QJsonObject file = value.toObject();
        file.insert(QStringLiteral("name"), moved(file.value(QStringLiteral("name")).toString()));
        files.append(file);
    }
    QSet<QString> folded;
    for (const QString &path : std::as_const(m_folded))
        folded.insert(moved(path));
    m_folded = folded;
    QSet<QString> selected;
    for (const QString &path : std::as_const(m_selected))
        selected.insert(moved(path));
    m_selected = selected;

    applyFiles(files);
}

int TorrentFilesModel::raisedPriority(int priority)
{
    switch (priority) {
    case int(FilePriority::Skip):
        return int(FilePriority::Normal);
    case int(FilePriority::Normal):
        return int(FilePriority::High);
    case int(FilePriority::Mixed):
        return int(FilePriority::High);
    default:
        return int(FilePriority::Maximal);
    }
}

int TorrentFilesModel::loweredPriority(int priority)
{
    switch (priority) {
    case int(FilePriority::Maximal):
        return int(FilePriority::High);
    case int(FilePriority::High):
        return int(FilePriority::Normal);
    case int(FilePriority::Mixed):
        return int(FilePriority::Normal);
    default:
        return int(FilePriority::Skip);
    }
}

QString TorrentFilesModel::serverPathOf(const QString &contentPath,
                                        const QString &savePath,
                                        const QString &relativePath)
{
    if (relativePath.isEmpty())
        return {};
    const auto trimmed = [](QString path) {
        while (path.size() > 1 &&
               (path.endsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('\\'))))
            path.chop(1);
        return path;
    };

    const QString top = relativePath.section(QLatin1Char('/'), 0, 0);
    const QString content = trimmed(contentPath);
    QString base;
    if (content.endsWith(QLatin1Char('/') + top) || content.endsWith(QLatin1Char('\\') + top))
        base = content.left(content.size() - top.size() - 1);
    else
        base = trimmed(savePath);
    if (base.isEmpty())
        return {};
    const QChar separator = base.contains(QLatin1Char('\\')) && !base.contains(QLatin1Char('/'))
                                ? QLatin1Char('\\')
                                : QLatin1Char('/');
    QString relative = relativePath;
    if (separator == QLatin1Char('\\'))
        relative.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return base.endsWith(separator) ? base + relative : base + separator + relative;
}

} // namespace omaseed::core
