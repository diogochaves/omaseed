#include "core/FolderTree.hpp"

#include "core/SharedData.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace omaseed::core {

namespace {

/// The heading a top save path hangs from (see the header), or "" for none.
QString headingOf(const QString &path, const FolderTree::MountRoot &mountRoot)
{
    if (mountRoot) {
        const QString mount = SharedData::normalize(mountRoot(path));
        if (!mount.isEmpty() && mount == path)
            return {}; // the drive's top is the save path itself
        if (!mount.isEmpty() && mount != QLatin1String("/") && SharedData::isUnder(path, mount))
            return mount;
    }
    const QString parent = FolderTree::parentOf(path);
    if (parent.isEmpty() || parent == QLatin1String("/"))
        return {};
    return parent;
}

} // namespace

QString FolderTree::baseName(const QString &path)
{
    const QString p = SharedData::normalize(path);
    return p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
}

QString FolderTree::parentOf(const QString &path)
{
    const QString p = SharedData::normalize(path);
    const qsizetype slash = p.lastIndexOf(QLatin1Char('/'));
    if (slash < 0 || p == QLatin1String("/"))
        return {};
    if (slash == 0)
        return QStringLiteral("/");
    return p.left(slash);
}

QString FolderTree::join(const QString &parent, const QString &child)
{
    const QString p = SharedData::normalize(parent);
    QString c = child;
    while (c.startsWith(QLatin1Char('/')))
        c.remove(0, 1);
    if (c.isEmpty())
        return p;
    if (p.isEmpty())
        return c;
    return p.endsWith(QLatin1Char('/')) ? p + c : p + QLatin1Char('/') + c;
}

QString FolderTree::targetFor(const QString &savePath, const QString &root, const QString &newPath)
{
    const QString path = SharedData::normalize(savePath);
    const QString from = SharedData::normalize(root);
    if (path.isEmpty() || from.isEmpty() || !SharedData::isUnder(path, from))
        return {};
    const QString to = SharedData::normalize(newPath);
    if (path == from)
        return to;
    const qsizetype cut = from == QLatin1String("/") ? 1 : from.size() + 1;
    return join(to, path.mid(cut));
}

const FolderNode *FolderTree::find(const QList<FolderNode> &nodes, const QString &path)
{
    const QString p = SharedData::normalize(path);
    for (const FolderNode &node : nodes) {
        if (node.path == p)
            return &node;
    }
    return nullptr;
}

QList<FolderNode> FolderTree::build(const QList<Torrent> &torrents, const MountRoot &mountRoot)
{
    QHash<QString, FolderNode> nodes;
    for (const Torrent &torrent : torrents) {
        const QString path = SharedData::normalize(torrent.savePath);
        if (path.isEmpty())
            continue;
        FolderNode &node = nodes[path];
        node.path = path;
        node.isSavePath = true;
        ++node.countHere;
        node.bytesHere += std::max<qint64>(0, torrent.completed);
    }
    const QStringList savePaths = nodes.keys();

    // The nearest save path above each one; the top ones get their heading.
    const auto nearestAbove = [&nodes](const QString &path) {
        for (QString up = parentOf(path); !up.isEmpty(); up = parentOf(up)) {
            if (nodes.contains(up))
                return up;
        }
        return QString();
    };
    for (const QString &path : savePaths) {
        bool top = true;
        for (QString up = parentOf(path); !up.isEmpty() && top; up = parentOf(up))
            top = !(nodes.contains(up) && nodes.value(up).isSavePath);
        if (!top)
            continue;
        const QString heading = headingOf(path, mountRoot);
        if (heading.isEmpty() || nodes.contains(heading))
            continue;
        FolderNode &node = nodes[heading];
        node.path = heading;
    }

    for (auto it = nodes.begin(); it != nodes.end(); ++it)
        it->parentPath = nearestAbove(it.key());

    // Figures up the chain: each save path counts in itself and in every row above it.
    for (const QString &path : savePaths) {
        const FolderNode here = nodes.value(path);
        for (QString at = path; !at.isEmpty(); at = nodes.value(at).parentPath) {
            FolderNode &node = nodes[at];
            node.count += here.countHere;
            node.bytes += here.bytesHere;
            ++node.savePaths;
        }
    }

    QHash<QString, QStringList> children;
    QStringList tops;
    for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
        if (it->parentPath.isEmpty())
            tops << it.key();
        else
            children[it->parentPath] << it.key();
    }
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        it->hasChildren = children.contains(it.key());
        if (it->parentPath.isEmpty()) {
            it->name = it.key();
        } else {
            const qsizetype cut =
                it->parentPath == QLatin1String("/") ? 1 : it->parentPath.size() + 1;
            it->name = it.key().mid(cut);
        }
    }
    const auto byName = [&nodes](const QString &a, const QString &b) {
        const int folded =
            QString::compare(nodes.value(a).name, nodes.value(b).name, Qt::CaseInsensitive);
        return folded != 0 ? folded < 0 : a < b;
    };

    QList<FolderNode> out;
    out.reserve(nodes.size());
    const std::function<void(const QStringList &, int)> walk = [&](QStringList level, int depth) {
        std::sort(level.begin(), level.end(), byName);
        for (const QString &path : std::as_const(level)) {
            FolderNode node = nodes.value(path);
            node.depth = depth;
            out << node;
            const auto kids = children.constFind(path);
            if (kids != children.cend())
                walk(*kids, depth + 1);
        }
    };
    walk(tops, 0);
    return out;
}

} // namespace omaseed::core
