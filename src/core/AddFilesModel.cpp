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
    // The tree has a "wanted" of its own (the Files tab's tick); ours, from the choices
    // made here, is the one the sheet reads.
    names.remove(TorrentFilesModel::WantedRole);
    names.insert(WantedRole, "wanted");
    names.insert(RenamedRole, "renamed");
    names.insert(OriginalNameRole, "originalName");
    return names;
}

QVariant AddFilesModel::data(const QModelIndex &index, int role) const
{
    if (role == RenamedRole || role == OriginalNameRole) {
        const QString original = originalPathAt(index.row()).section(QLatin1Char('/'), -1);
        if (role == OriginalNameRole)
            return original;
        return !original.isEmpty() && original != pathAt(index.row()).section(QLatin1Char('/'), -1);
    }
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
    m_beforeSkip = QList<int>(files.size(), kNormal);
    m_paths.clear();
    for (const TorrentFileEntry &file : files)
        m_paths.append(file.path);
    m_renames.clear();
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
    m_beforeSkip.clear();
    m_paths.clear();
    m_renames.clear();
    m_tree->clear();
    Q_EMIT choicesChanged();
}

void AddFilesModel::applyToTree()
{
    QJsonArray rows;
    for (int id = 0; id < int(m_files.size()); ++id) {
        QJsonObject row;
        row.insert(QStringLiteral("name"), m_paths.value(id, m_files.at(id).path));
        row.insert(QStringLiteral("size"), double(m_files.at(id).size));
        row.insert(QStringLiteral("progress"), 0.0);
        row.insert(QStringLiteral("priority"), m_priorities.at(id));
        row.insert(QStringLiteral("index"), id);
        row.insert(QStringLiteral("availability"), -1.0);
        rows.append(row);
    }
    m_tree->applyFiles(rows);
    // The tree reports the roles it changed; the check mark and the rename marks are ours,
    // so they go out too.
    if (rowCount() > 0)
        Q_EMIT dataChanged(
            index(0, 0), index(rowCount() - 1, 0), {WantedRole, RenamedRole, OriginalNameRole});
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

bool AddFilesModel::setIds(const QList<int> &ids, int priority)
{
    bool changed = false;
    for (int id : ids) {
        if (id >= 0 && id < int(m_priorities.size()) && m_priorities.at(id) != priority) {
            if (priority == kSkip && id < int(m_beforeSkip.size()))
                m_beforeSkip[id] = m_priorities.at(id);
            m_priorities[id] = priority;
            changed = true;
        }
    }
    return changed;
}

void AddFilesModel::setPriority(int row, int priority)
{
    if (!setIds(idsAt(row), priority))
        return;
    applyToTree();
    Q_EMIT choicesChanged();
}

bool AddFilesModel::restoreIds(const QList<int> &ids)
{
    bool changed = false;
    for (int id : ids) {
        if (id < 0 || id >= int(m_priorities.size()) || m_priorities.at(id) != kSkip)
            continue;
        const int before = m_beforeSkip.value(id, kNormal);
        m_priorities[id] = before == kSkip ? kNormal : before;
        changed = true;
    }
    return changed;
}

void AddFilesModel::toggleWanted(int row)
{
    const QList<int> ids = idsAt(row);
    const bool changed =
        data(index(row, 0), WantedRole).toInt() == 2 ? setIds(ids, kSkip) : restoreIds(ids);
    if (!changed)
        return;
    applyToTree();
    Q_EMIT choicesChanged();
}

void AddFilesModel::raise(int row)
{
    setPriority(row, TorrentFilesModel::raisedPriority(priority(row)));
}

void AddFilesModel::lower(int row)
{
    setPriority(row, TorrentFilesModel::loweredPriority(priority(row)));
}

void AddFilesModel::cyclePriority(int row)
{
    QList<int> wanted;
    int common = -1;
    bool agree = true;
    for (int id : idsAt(row)) {
        const int priority = m_priorities.value(id, kNormal);
        if (priority == kSkip)
            continue;
        if (wanted.isEmpty())
            common = priority;
        else if (priority != common)
            agree = false;
        wanted.append(id);
    }
    if (wanted.isEmpty()) {
        setPriority(row, kNormal);
        return;
    }
    int next = kNormal;
    if (agree && common == kNormal)
        next = int(FilePriority::High);
    else if (agree && common == int(FilePriority::High))
        next = int(FilePriority::Maximal);
    if (!setIds(wanted, next))
        return;
    applyToTree();
    Q_EMIT choicesChanged();
}

void AddFilesModel::setAllWanted(bool wanted)
{
    QList<int> ids;
    for (int id = 0; id < int(m_priorities.size()); ++id)
        ids.append(id);
    if (!(wanted ? restoreIds(ids) : setIds(ids, kSkip)))
        return;
    applyToTree();
    Q_EMIT choicesChanged();
}

void AddFilesModel::toggleAllWanted()
{
    setAllWanted(wantedCount() < fileCount());
}

QString AddFilesModel::pathAt(int row) const
{
    return data(index(row, 0), TorrentFilesModel::PathRole).toString();
}

QString AddFilesModel::originalPathAt(int row) const
{
    const QList<int> ids = idsAt(row);
    if (ids.isEmpty() || ids.first() < 0 || ids.first() >= int(m_files.size()))
        return QString();
    const QString original = m_files.at(ids.first()).path;
    if (!isFolder(row))
        return original;
    // A rename keeps the number of segments, so the folder's depth finds it in the original.
    return original.section(QLatin1Char('/'), 0, depthAt(row));
}

int AddFilesModel::rowOfPath(const QString &path) const
{
    for (int row = 0; row < rowCount(); ++row) {
        if (pathAt(row) == path)
            return row;
    }
    return -1;
}

QString AddFilesModel::plainName(int row) const
{
    return pathAt(row).section(QLatin1Char('/'), -1);
}

QString AddFilesModel::renameError(int row, const QString &name) const
{
    if (row < 0 || row >= rowCount())
        return tr("nothing to rename");
    if (name.trimmed().isEmpty())
        return tr("a name is needed");
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return tr("a name cannot hold / or \\");
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return tr("that is not a name");

    const QString oldPath = pathAt(row);
    const QString parent = oldPath.section(QLatin1Char('/'), 0, -2);
    const QString newPath = parent.isEmpty() ? name : parent + QLatin1Char('/') + name;
    if (newPath == oldPath)
        return QString();
    // A file or a folder of that name beside it would merge the two on the server.
    const QString asFolder = newPath + QLatin1Char('/');
    for (const QString &path : m_paths) {
        if (path == newPath || path.startsWith(asFolder))
            return tr("%1 is already there").arg(name);
    }
    return QString();
}

bool AddFilesModel::rename(int row, const QString &name)
{
    if (!renameError(row, name).isEmpty())
        return false;
    const QString oldPath = pathAt(row);
    const QString parent = oldPath.section(QLatin1Char('/'), 0, -2);
    const QString newPath = parent.isEmpty() ? name : parent + QLatin1Char('/') + name;
    if (newPath == oldPath)
        return true;

    const bool folder = isFolder(row);
    if (folder) {
        const QString under = oldPath + QLatin1Char('/');
        for (QString &path : m_paths) {
            if (path.startsWith(under))
                path = newPath + QLatin1Char('/') + path.mid(under.size());
        }
    } else {
        for (int id : idsAt(row)) {
            if (id >= 0 && id < int(m_paths.size()))
                m_paths[id] = newPath;
        }
    }
    m_renames.append(QVariantMap{
        {QStringLiteral("kind"), folder ? QStringLiteral("folder") : QStringLiteral("file")},
        {QStringLiteral("from"), oldPath},
        {QStringLiteral("to"), newPath},
    });
    applyToTree();
    Q_EMIT choicesChanged();
    return true;
}

QVariantMap AddFilesModel::renameAnchor() const
{
    if (m_files.isEmpty())
        return {};
    return QVariantMap{{QStringLiteral("index"), 0},
                       {QStringLiteral("path"), m_files.first().path}};
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
