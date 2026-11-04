#include "core/EmptyFolders.hpp"

#include "core/SharedData.hpp"

#include <QDir>
#include <QFile>
#include <QSet>

#include <algorithm>
#include <unistd.h>

namespace omaseed::core {

namespace {

QString clean(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return slash == 0 && path.size() > 1 ? QStringLiteral("/") : QString();
    return path.left(slash);
}

} // namespace

int EmptyFolders::remove(const QString &rootIn,
                         const QStringList &emptied,
                         bool withRoot,
                         const QStringList &keepIn)
{
    const QString root = clean(rootIn);
    // "/" or a relative path is never a boundary to sweep up to.
    if (root.isEmpty() || root == QLatin1String("/") || !QDir::isAbsolutePath(root))
        return 0;
    QStringList keep;
    for (const QString &path : keepIn) {
        if (!path.isEmpty())
            keep << clean(path);
    }
    // A folder that is one of `keep`, or holds one, stays.
    const auto kept = [&keep](const QString &folder) {
        return std::any_of(keep.cbegin(), keep.cend(), [&folder](const QString &path) {
            return SharedData::isUnder(path, folder);
        });
    };

    QSet<QString> seen;
    QStringList folders;
    for (const QString &start : emptied) {
        QString at = clean(start);
        if (!QDir::isAbsolutePath(at))
            continue;
        while (!at.isEmpty() && SharedData::isUnder(at, root)) {
            if (at == root && !withRoot)
                break;
            if (!seen.contains(at)) {
                seen.insert(at);
                if (!kept(at))
                    folders << at;
            }
            if (at == root)
                break;
            at = parentOf(at);
        }
    }
    // Deepest first, so a parent is tried once what was below it has gone.
    std::stable_sort(folders.begin(), folders.end(), [](const QString &a, const QString &b) {
        return a.count(QLatin1Char('/')) > b.count(QLatin1Char('/'));
    });
    int removed = 0;
    for (const QString &folder : std::as_const(folders)) {
        // rmdir removes an empty folder only, and never follows a link.
        if (::rmdir(QFile::encodeName(folder).constData()) == 0)
            ++removed;
    }
    return removed;
}

} // namespace omaseed::core
