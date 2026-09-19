#include "core/TorrentFilesModel.hpp"

#include "core/Format.hpp"

#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <functional>

namespace omaseed::core {

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
        return node.isFolder && !m_folded.contains(node.path);
    case FileIndexesRole: {
        QVariantList ids;
        ids.reserve(node.fileIndexes.size());
        for (int id : node.fileIndexes)
            ids.append(id);
        return ids;
    }
    case AvailabilityRole:
        return node.availability;
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

void TorrentFilesModel::applyFiles(const QJsonArray &files)
{
    if (files.isEmpty()) {
        clear();
        return;
    }

    if (shapeOf(files) == m_shape) {
        updateInPlace(files);
        return;
    }

    beginResetModel();
    rebuild(files);
    endResetModel();
    Q_EMIT countChanged();
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
    m_folded.clear();
    m_fileCount = 0;
    endResetModel();
    Q_EMIT countChanged();
}

void TorrentFilesModel::rebuild(const QJsonArray &files)
{
    m_nodes.clear();
    m_roots.clear();
    m_visible.clear();
    m_shape = shapeOf(files);
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
        leaf.fileIndex = file.value(QStringLiteral("index")).toInt(m_fileCount);
        leaf.size = qint64(file.value(QStringLiteral("size")).toDouble());
        leaf.progress = file.value(QStringLiteral("progress")).toDouble();
        leaf.priority = file.value(QStringLiteral("priority")).toInt(int(FilePriority::Normal));
        leaf.availability = file.value(QStringLiteral("availability")).toDouble();
        leaf.fileIndexes = {leaf.fileIndex};
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

    for (int at = 0; at < files.size(); ++at) {
        const QJsonObject file = files.at(at).toObject();
        Node &node = m_nodes[leaves.at(at)];
        node.fileIndex = file.value(QStringLiteral("index")).toInt(node.fileIndex);
        node.fileIndexes = {node.fileIndex};
        node.size = qint64(file.value(QStringLiteral("size")).toDouble());
        node.progress = file.value(QStringLiteral("progress")).toDouble();
        node.priority = file.value(QStringLiteral("priority")).toInt(node.priority);
        node.availability = file.value(QStringLiteral("availability")).toDouble();
    }
    for (int root : std::as_const(m_roots))
        aggregate(root);

    if (!m_visible.isEmpty()) {
        Q_EMIT dataChanged(index(0),
                           index(int(m_visible.size()) - 1),
                           {SizeRole,
                            SizeTextRole,
                            ProgressRole,
                            ProgressTextRole,
                            PriorityRole,
                            PriorityTextRole,
                            FileIndexesRole,
                            AvailabilityRole});
    }
}

void TorrentFilesModel::aggregate(int at)
{
    if (!m_nodes.at(at).isFolder)
        return;

    qint64 size = 0;
    double done = 0.0;
    int priority = int(FilePriority::Mixed);
    bool first = true;
    QList<int> ids;

    const QList<int> children = m_nodes.at(at).children;
    for (int child : children) {
        aggregate(child);
        const Node &kid = m_nodes.at(child);
        size += kid.size;
        done += double(kid.size) * kid.progress;
        ids.append(kid.fileIndexes);
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
    node.progress = size > 0 ? done / double(size) : 0.0;
    node.priority = first ? int(FilePriority::Normal) : priority;
    node.fileIndexes = ids;
}

void TorrentFilesModel::rebuildVisible()
{
    m_visible.clear();
    const std::function<void(int)> walk = [&](int at) {
        m_visible.append(at);
        const Node &node = m_nodes.at(at);
        if (!node.isFolder || m_folded.contains(node.path))
            return;
        for (int child : node.children)
            walk(child);
    };
    for (int root : std::as_const(m_roots))
        walk(root);
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
    return !m_folded.contains(m_nodes.at(m_visible.at(row)).path);
}

void TorrentFilesModel::setExpanded(int row, bool expanded)
{
    if (!isFolder(row) || isExpanded(row) == expanded)
        return;

    const QString path = m_nodes.at(m_visible.at(row)).path;
    if (expanded)
        m_folded.remove(path);
    else
        m_folded.insert(path);

    beginResetModel();
    rebuildVisible();
    endResetModel();
    Q_EMIT countChanged();
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
    beginResetModel();
    rebuildVisible();
    endResetModel();
    Q_EMIT countChanged();
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
    beginResetModel();
    rebuildVisible();
    endResetModel();
    Q_EMIT countChanged();
}

// --- priorities --------------------------------------------------------------------------

QVariantList TorrentFilesModel::fileIndexes(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return {};
    QVariantList ids;
    const Node &node = m_nodes.at(m_visible.at(row));
    ids.reserve(node.fileIndexes.size());
    for (int id : node.fileIndexes)
        ids.append(id);
    return ids;
}

int TorrentFilesModel::priority(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return int(FilePriority::Normal);
    return m_nodes.at(m_visible.at(row)).priority;
}

QString TorrentFilesModel::pathAt(int row) const
{
    if (row < 0 || row >= int(m_visible.size()))
        return {};
    return m_nodes.at(m_visible.at(row)).path;
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

} // namespace omaseed::core
