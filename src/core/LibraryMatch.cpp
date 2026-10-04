#include "core/LibraryMatch.hpp"

#include <algorithm>

namespace omaseed::core::LibraryMatch {

namespace {

QString trimmed(const QString &path)
{
    QString result = path;
    while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

/// The files as (path, size) pairs in one order, so two lists compare as sets.
QList<TorrentFileEntry> sorted(QList<TorrentFileEntry> files)
{
    std::sort(files.begin(), files.end(), [](const TorrentFileEntry &a, const TorrentFileEntry &b) {
        return a.path < b.path || (a.path == b.path && a.size < b.size);
    });
    return files;
}

/// The folder every file of `files` sits in ("R" for "R/a", "R/b/c"), or "" when they do not
/// share one.
QString commonRoot(const QList<TorrentFileEntry> &files)
{
    QString root;
    for (const TorrentFileEntry &file : files) {
        const qsizetype slash = file.path.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return QString();
        const QString first = file.path.left(slash);
        if (root.isEmpty())
            root = first;
        else if (root != first)
            return QString();
    }
    return root;
}

} // namespace

Result match(const QString &name,
             bool multiFile,
             const QList<TorrentFileEntry> &files,
             const Candidate &candidate)
{
    Result result;
    if (files.isEmpty() || candidate.files.size() != files.size())
        return result;
    const QString savePath = trimmed(candidate.savePath);
    if (savePath.isEmpty())
        return result;

    if (!multiFile) {
        const TorrentFileEntry &mine = files.constFirst();
        const TorrentFileEntry &theirs = candidate.files.constFirst();
        if (theirs.path == name && theirs.size == mine.size) {
            result = {true, savePath, QStringLiteral("Original")};
        }
        return result;
    }

    const QList<TorrentFileEntry> wanted = sorted(files);

    // In a folder of its own: the content path is that folder.
    const QString root = commonRoot(candidate.files);
    if (!root.isEmpty() && trimmed(candidate.contentPath) == savePath + QLatin1Char('/') + root) {
        QList<TorrentFileEntry> inside;
        inside.reserve(candidate.files.size());
        for (const TorrentFileEntry &file : candidate.files)
            inside.append({file.path.mid(root.size() + 1), file.size});
        if (sorted(inside) == wanted) {
            if (root == name)
                result = {true, savePath, QStringLiteral("Original")};
            else
                result = {true, savePath + QLatin1Char('/') + root, QStringLiteral("NoSubfolder")};
            return result;
        }
    }

    // Straight in the save path ("No subfolder"), or a folder its content path does not name.
    if (sorted(candidate.files) == wanted)
        result = {true, savePath, QStringLiteral("NoSubfolder")};
    return result;
}

} // namespace omaseed::core::LibraryMatch
