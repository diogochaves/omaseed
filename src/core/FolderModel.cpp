#include "core/FolderModel.hpp"

namespace omaseed::core {

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shown.size())
        return {};
    const FolderNode &node = m_nodes.at(m_shown.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return node.name;
    case PathRole:
        return node.path;
    case DepthRole:
        return node.depth;
    case HeadingRole:
        return node.isHeading();
    case HasChildrenRole:
        return node.hasChildren;
    case FoldedRole:
        return m_foldedSet.contains(node.path);
    case CountRole:
        return node.count;
    case CountHereRole:
        return node.countHere;
    case BytesRole:
        return node.bytes;
    case SavePathsRole:
        return node.savePaths;
    case MarkRole:
        return m_marks.value(node.path);
    default:
        return {};
    }
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {PathRole, "path"},
        {NameRole, "name"},
        {DepthRole, "depth"},
        {HeadingRole, "heading"},
        {HasChildrenRole, "hasChildren"},
        {FoldedRole, "folded"},
        {CountRole, "count"},
        {CountHereRole, "countHere"},
        {BytesRole, "bytes"},
        {SavePathsRole, "savePaths"},
        {MarkRole, "mark"},
    };
}

void FolderModel::setNodes(const QList<FolderNode> &nodes)
{
    if (nodes == m_nodes)
        return;
    // Only the figures moved (a poll's counts or bytes): the rows stay, their data changes.
    bool sameRows = nodes.size() == m_nodes.size();
    for (qsizetype i = 0; sameRows && i < nodes.size(); ++i)
        sameRows = nodes.at(i).path == m_nodes.at(i).path &&
                   nodes.at(i).name == m_nodes.at(i).name &&
                   nodes.at(i).depth == m_nodes.at(i).depth &&
                   nodes.at(i).isSavePath == m_nodes.at(i).isSavePath &&
                   nodes.at(i).hasChildren == m_nodes.at(i).hasChildren;
    if (sameRows) {
        m_nodes = nodes;
        if (!m_shown.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_shown.size()) - 1));
        ++m_revision;
        Q_EMIT revisionChanged();
        return;
    }
    beginResetModel();
    m_nodes = nodes;
    m_byPath.clear();
    for (qsizetype i = 0; i < m_nodes.size(); ++i)
        m_byPath.insert(m_nodes.at(i).path, int(i));
    rebuildShown();
    endResetModel();
    ++m_revision;
    Q_EMIT countChanged();
    Q_EMIT revisionChanged();
}

void FolderModel::setFolded(const QStringList &paths)
{
    if (paths == m_folded)
        return;
    m_folded = paths;
    m_foldedSet = QSet<QString>(paths.cbegin(), paths.cend());
    beginResetModel();
    rebuildShown();
    endResetModel();
    ++m_revision;
    Q_EMIT foldedChanged();
    Q_EMIT countChanged();
    Q_EMIT revisionChanged();
}

void FolderModel::toggleFold(const QString &path)
{
    QStringList next = m_folded;
    if (!next.removeAll(path))
        next.append(path);
    setFolded(next);
}

void FolderModel::setMarks(const QHash<QString, QString> &marks)
{
    if (marks == m_marks)
        return;
    m_marks = marks;
    if (!m_shown.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_shown.size()) - 1), {MarkRole});
    ++m_revision;
    Q_EMIT revisionChanged();
}

const FolderNode *FolderModel::node(const QString &path) const
{
    const auto at = m_byPath.constFind(path);
    return at == m_byPath.cend() ? nullptr : &m_nodes.at(*at);
}

QVariantMap FolderModel::figures(const QString &path) const
{
    const FolderNode *found = node(path);
    if (found == nullptr)
        return {};
    return {
        {QStringLiteral("path"), found->path},
        {QStringLiteral("name"), FolderTree::baseName(found->path)},
        {QStringLiteral("count"), found->count},
        {QStringLiteral("countHere"), found->countHere},
        {QStringLiteral("bytes"), found->bytes},
        {QStringLiteral("savePaths"), found->savePaths},
        {QStringLiteral("heading"), found->isHeading()},
    };
}

QString FolderModel::pathAt(int row) const
{
    return row < 0 || row >= m_shown.size() ? QString() : m_nodes.at(m_shown.at(row)).path;
}

void FolderModel::rebuildShown()
{
    m_shown.clear();
    for (qsizetype i = 0; i < m_nodes.size(); ++i) {
        if (!hiddenByFold(m_nodes.at(i)))
            m_shown.append(int(i));
    }
}

bool FolderModel::hiddenByFold(const FolderNode &node) const
{
    QString parent = node.parentPath;
    while (!parent.isEmpty()) {
        if (m_foldedSet.contains(parent))
            return true;
        const FolderNode *up = this->node(parent);
        if (up == nullptr)
            return false;
        parent = up->parentPath;
    }
    return false;
}

} // namespace omaseed::core
