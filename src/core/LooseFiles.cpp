#include "core/LooseFiles.hpp"

#include "core/FolderTree.hpp"
#include "core/SharedData.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTimeZone>

#include <algorithm>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("LooseFiles", text);
}

QString clean(const QString &path)
{
    return QDir::cleanPath(path);
}

struct Walk
{
    QString root;
    QSet<QString> owned;
    QDateTime now;
    std::chrono::seconds minAge;
    const std::function<bool()> *stop = nullptr;
    LooseFiles::Result result;
    int sinceAsked = 0;

    bool stopped()
    {
        if (stop == nullptr || !*stop || ++sinceAsked < 64)
            return false;
        sinceAsked = 0;
        if ((*stop)()) {
            result.complete = false;
            return true;
        }
        return false;
    }

    QString relative(const QString &path) const
    {
        return path.mid(root.size() + (root.endsWith(QLatin1Char('/')) ? 0 : 1));
    }

    void add(const QFileInfo &info, LooseFile::Verdict verdict, const QString &why)
    {
        LooseFile file;
        file.path = clean(info.absoluteFilePath());
        file.relative = relative(file.path);
        file.isFolder = info.isDir() && !info.isSymLink();
        file.size = verdict == LooseFile::Verdict::Unknown || file.isFolder ? 0 : info.size();
        file.modified = info.lastModified(QTimeZone::UTC);
        file.verdict = verdict;
        file.why = why;
        switch (verdict) {
        case LooseFile::Verdict::Loose:
            ++result.loose;
            result.looseBytes += file.size;
            break;
        case LooseFile::Verdict::NeverCounts:
            ++result.neverCounts;
            break;
        case LooseFile::Verdict::Recent:
            ++result.recent;
            break;
        case LooseFile::Verdict::Unknown:
            ++result.unknown;
            break;
        }
        result.files << file;
    }

    void folder(const QString &path)
    {
        const QDir dir(path);
        const QFileInfoList entries =
            dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                              QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &info : entries) {
            if (stopped())
                return;
            const QString at = clean(info.absoluteFilePath());
            if (owned.contains(at))
                continue;
            if (info.isSymLink()) {
                add(info, LooseFile::Verdict::Unknown, tr("a link"));
                continue;
            }
            if (info.isDir()) {
                if (!info.isReadable() || !info.isExecutable()) {
                    add(info, LooseFile::Verdict::Unknown, tr("can't read"));
                    continue;
                }
                folder(at);
                if (!result.complete)
                    return;
                continue;
            }
            if (!info.exists() || !info.isFile()) {
                add(info, LooseFile::Verdict::Unknown, tr("not a plain file"));
                continue;
            }
            if (!info.isReadable()) {
                add(info, LooseFile::Verdict::Unknown, tr("can't read"));
                continue;
            }
            const QString name = info.fileName();
            if (LooseFiles::neverCounts(name)) {
                const bool qbt =
                    name.endsWith(QLatin1String(".parts")) || name.endsWith(QLatin1String(".!qB"));
                add(info,
                    LooseFile::Verdict::NeverCounts,
                    qbt ? tr("qBittorrent's own") : tr("a system file"));
                continue;
            }
            const QDateTime modified = info.lastModified(QTimeZone::UTC);
            const qint64 age = modified.isValid() ? modified.secsTo(now) : 0;
            if (!modified.isValid() || age < minAge.count()) {
                add(info,
                    LooseFile::Verdict::Recent,
                    tr("changed %1 min ago").arg(std::max<qint64>(0, age / 60)));
                continue;
            }
            add(info, LooseFile::Verdict::Loose, QString());
        }
    }
};

} // namespace

QStringList LooseFiles::neverCountPatterns()
{
    return {QStringLiteral("*.parts"),
            QStringLiteral("*.!qB"),
            QStringLiteral("Thumbs.db"),
            QStringLiteral("desktop.ini"),
            QStringLiteral(".DS_Store")};
}

bool LooseFiles::neverCounts(const QString &fileName)
{
    if (fileName.endsWith(QLatin1String(".parts")) || fileName.endsWith(QLatin1String(".!qB")))
        return true;
    // Windows writes these two in any case.
    return fileName.compare(QLatin1String("Thumbs.db"), Qt::CaseInsensitive) == 0 ||
           fileName.compare(QLatin1String("desktop.ini"), Qt::CaseInsensitive) == 0 ||
           fileName == QLatin1String(".DS_Store");
}

LooseFiles::Result LooseFiles::scan(const QString &folder,
                                    const QStringList &owned,
                                    const QDateTime &now,
                                    std::chrono::seconds minAge,
                                    const std::function<bool()> &stop)
{
    Walk walk;
    walk.root = clean(folder);
    walk.now = now;
    walk.minAge = minAge;
    walk.stop = &stop;
    for (const QString &path : owned)
        walk.owned.insert(clean(path));

    // A folder inside a torrent's content is all the torrent's.
    for (QString up = walk.root; !up.isEmpty(); up = QFileInfo(up).path()) {
        if (walk.owned.contains(up))
            return walk.result;
        if (up == QLatin1String("/") || up == QLatin1String("."))
            break;
    }

    const QFileInfo root(walk.root);
    if (!root.isDir() || !root.isReadable() || !root.isExecutable()) {
        walk.result.complete = false;
        return walk.result;
    }
    walk.folder(walk.root);
    return walk.result;
}

LooseFiles::OnServer LooseFiles::onServer(const QString &rootIn,
                                          const QStringList &foldersIn,
                                          const QHash<QString, ServerFolder> &listed,
                                          const QStringList &ownedIn)
{
    OnServer out;
    const QString root = SharedData::normalize(rootIn);
    QStringList folders;
    for (const QString &folder : foldersIn)
        folders << SharedData::normalize(folder);
    folders.removeDuplicates();
    folders.sort();
    QStringList owned;
    for (const QString &path : ownedIn)
        owned << SharedData::normalize(path);

    for (const QString &folder : std::as_const(folders)) {
        const auto found = listed.constFind(folder);
        if (found == listed.cend()) {
            out.needListings << folder;
            out.complete = false;
            continue;
        }
        if (found->state == ServerFolder::State::Missing)
            continue;
        if (found->state != ServerFolder::State::Listed) {
            out.complete = false;
            continue;
        }
        QStringList names(found->names.cbegin(), found->names.cend());
        names.sort();
        for (const QString &name : std::as_const(names)) {
            const QString full = FolderTree::join(folder, name);
            // A save path, or a folder on the way to one: listed on its own.
            const bool holdsAFolder =
                std::any_of(folders.cbegin(), folders.cend(), [&full](const QString &other) {
                    return SharedData::isUnder(other, full);
                });
            if (holdsAFolder)
                continue;
            // A torrent's content (a folder covers what is below it), or a file of one added
            // without a subfolder, or a folder holding some of those.
            const bool isOwned =
                std::any_of(owned.cbegin(), owned.cend(), [&full](const QString &path) {
                    return SharedData::isUnder(full, path) || SharedData::isUnder(path, full);
                });
            if (isOwned)
                continue;
            if (neverCounts(name)) {
                ++out.neverCounts;
                continue;
            }
            out.names << (root == QLatin1String("/") ? full.mid(1) : full.mid(root.size() + 1));
        }
    }
    return out;
}

} // namespace omaseed::core
