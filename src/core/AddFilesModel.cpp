#include "core/AddFilesModel.hpp"

#include "core/Format.hpp"
#include "core/TorrentFilesModel.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace omaseed::core {

namespace {

constexpr int kSkip = int(FilePriority::Skip);
constexpr int kNormal = int(FilePriority::Normal);

} // namespace

AddFilesModel::AddFilesModel(QObject *parent)
    : QIdentityProxyModel(parent)
    , m_tree(new TorrentFilesModel(this))
{
    setSourceModel(m_tree);
    connect(m_tree, &TorrentFilesModel::countChanged, this, &AddFilesModel::countChanged);
}

QHash<int, QByteArray> AddFilesModel::roleNames() const
{
    QHash<int, QByteArray> names = m_tree->roleNames();
    names.insert(WantedRole, "wanted");
    return names;
}

QVariant AddFilesModel::data(const QModelIndex &index, int role) const
{
    if (role != WantedRole)
        return QIdentityProxyModel::data(index, role);

    const QList<int> ids = idsAt(index.row());
    int skipped = 0;
    for (int id : ids) {
        if (m_priorities.value(id, kNormal) == kSkip)
            ++skipped;
    }
    if (skipped == 0)
        return 2;
    return skipped == ids.size() ? 0 : 1;
}

int AddFilesModel::wantedCount() const
{
    int wanted = 0;
    for (int priority : m_priorities) {
        if (priority != kSkip)
            ++wanted;
    }
    return wanted;
}

qint64 AddFilesModel::wantedSize() const
{
    qint64 bytes = 0;
    for (int id = 0; id < int(m_files.size()); ++id) {
        if (m_priorities.value(id, kNormal) != kSkip)
            bytes += m_files.at(id).size;
    }
    return bytes;
}

QString AddFilesModel::summaryText() const
{
    if (m_files.isEmpty())
        return QString();
    const int wanted = wantedCount();
    const QString size = Format::size(wantedSize());
    const int total = int(m_files.size());
    // No translations ship yet, so "%n file(s)" would print as written: pick the word here.
    const QString files = total == 1 ? tr("file") : tr("files");
    if (wanted == total)
        return tr("%1 %2 · %3").arg(total).arg(files, size);
    return tr("%1 of %2 %3 · %4").arg(wanted).arg(total).arg(files, size);
}

bool AddFilesModel::isCustomised() const
{
    for (int priority : m_priorities) {
        if (priority != kNormal)
            return true;
    }
    return false;
}

void AddFilesModel::load(const QList<TorrentFileEntry> &files)
{
    m_files = files;
    m_priorities = QList<int>(files.size(), kNormal);
    // A different torrent: forget the folds of the last one.
    m_tree->clear();
    applyToTree();
    Q_EMIT choicesChanged();
}

void AddFilesModel::clear()
{
    if (m_files.isEmpty())
        return;
    m_files.clear();
    m_priorities.clear();
    m_tree->clear();
    Q_EMIT choicesChanged();
}

void AddFilesModel::applyToTree()
{
    QJsonArray rows;
    for (int id = 0; id < int(m_files.size()); ++id) {
        QJsonObject row;
        row.insert(QStringLiteral("name"), m_files.at(id).path);
        row.insert(QStringLiteral("size"), double(m_files.at(id).size));
        row.insert(QStringLiteral("progress"), 0.0);
        row.insert(QStringLiteral("priority"), m_priorities.at(id));
        row.insert(QStringLiteral("index"), id);
        row.insert(QStringLiteral("availability"), -1.0);
        rows.append(row);
    }
    m_tree->applyFiles(rows);
    // The tree reports the roles it changed; the check mark is ours, so it goes out too.
    if (rowCount() > 0)
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {WantedRole});
}

QList<int> AddFilesModel::idsAt(int row) const
{
    QList<int> ids;
    for (const QVariant &id : m_tree->fileIndexes(row))
        ids.append(id.toInt());
    return ids;
}

bool AddFilesModel::isFolder(int row) const
{
    return m_tree->isFolder(row);
}

bool AddFilesModel::isExpanded(int row) const
{
    return m_tree->isExpanded(row);
}

void AddFilesModel::setExpanded(int row, bool expanded)
{
    m_tree->setExpanded(row, expanded);
}

int AddFilesModel::depthAt(int row) const
{
    return data(index(row, 0), TorrentFilesModel::DepthRole).toInt();
}

int AddFilesModel::priority(int row) const
{
    return m_tree->priority(row);
}

void AddFilesModel::setPriority(int row, int priority)
{
    const QList<int> ids = idsAt(row);
    bool changed = false;
    for (int id : ids) {
        if (id >= 0 && id < int(m_priorities.size()) && m_priorities.at(id) != priority) {
            m_priorities[id] = priority;
            changed = true;
        }
    }
    if (!changed)
        return;
    applyToTree();
    Q_EMIT choicesChanged();
}

void AddFilesModel::toggleWanted(int row)
{
    const int wanted = data(index(row, 0), WantedRole).toInt();
    setPriority(row, wanted == 0 ? kNormal : kSkip);
}

void AddFilesModel::raise(int row)
{
    setPriority(row, TorrentFilesModel::raisedPriority(priority(row)));
}

void AddFilesModel::lower(int row)
{
    setPriority(row, TorrentFilesModel::loweredPriority(priority(row)));
}

QVariantList AddFilesModel::priorityList() const
{
    QVariantList list;
    list.reserve(m_priorities.size());
    for (int priority : m_priorities)
        list.append(priority);
    return list;
}

QMap<int, QList<int>> AddFilesModel::priorityGroups(const QVariantList &priorities)
{
    QMap<int, QList<int>> groups;
    for (int id = 0; id < int(priorities.size()); ++id) {
        bool ok = false;
        const int priority = priorities.at(id).toInt(&ok);
        if (ok && priority != kNormal)
            groups[priority].append(id);
    }
    return groups;
}

} // namespace omaseed::core
