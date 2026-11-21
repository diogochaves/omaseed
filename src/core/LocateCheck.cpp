#include "core/LocateCheck.hpp"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace omaseed::core::LocateCheck {

namespace {

/// The one walk both share: `any` counts a file there at any size, skipped ones included.
Result walk(const QList<ContentFile> &files, const QString &localFolder, const Stat &stat, bool any)
{
    Result result;
    QString folder = localFolder;
    while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    for (const ContentFile &file : files) {
        if (!any && file.priority && *file.priority == 0)
            continue; // Q9: a skipped file isn't required
        ++result.files;
        const std::optional<qint64> size =
            stat ? stat(folder + QLatin1Char('/') + file.path) : std::nullopt;
        const bool complete = file.progress >= 1.0;
        const bool fits =
            size.has_value() && (any || (complete ? *size == file.size : *size <= file.size));
        if (fits) {
            ++result.found;
            result.bytes += *size;
        } else if (result.firstMissing.isEmpty()) {
            result.firstMissing = file.path;
        }
    }
    return result;
}

} // namespace

Result check(const QList<ContentFile> &files, const QString &localFolder, const Stat &stat)
{
    return walk(files, localFolder, stat, false);
}

Result present(const QList<ContentFile> &files, const QString &localFolder, const Stat &stat)
{
    return walk(files, localFolder, stat, true);
}

namespace {

/// "\\040" and the other octal escapes mountinfo and fstab write in a path.
QString unescape(const QString &field)
{
    static const QRegularExpression octal(QStringLiteral("\\\\([0-7]{3})"));
    QString out;
    qsizetype last = 0;
    auto it = octal.globalMatch(field);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += field.mid(last, m.capturedStart() - last);
        out += QChar(m.captured(1).toInt(nullptr, 8));
        last = m.capturedEnd();
    }
    return out + field.mid(last);
}

QStringList fieldOf(const QString &file, int index, bool comments)
{
    QStringList out;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    static const QRegularExpression space(QStringLiteral("\\s+"));
    const QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || (comments && line.startsWith(QLatin1Char('#'))))
            continue;
        const QStringList fields = line.split(space, Qt::SkipEmptyParts);
        if (fields.size() > index)
            out << unescape(fields.at(index));
    }
    return out;
}

QString trimmed(QString path)
{
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

bool atOrUnder(const QString &path, const QString &folder)
{
    return path == folder || folder == QLatin1String("/") ||
           path.startsWith(folder + QLatin1Char('/'));
}

} // namespace

Mounts readMounts(const QString &mountinfo, const QString &fstab)
{
    Mounts mounts;
    for (const QString &point : fieldOf(mountinfo, 4, false))
        mounts.mounted << trimmed(point);
    for (const QString &target : fieldOf(fstab, 1, true))
        if (target.startsWith(QLatin1Char('/')))
            mounts.listed << trimmed(target);
    return mounts;
}

bool existsLocal(const QString &localPath)
{
    return QFileInfo::exists(localPath);
}

QString unmountedDrive(const QString &localFolder,
                       const Mounts &mounts,
                       const Exists &exists,
                       const QString &user)
{
    const QString path = trimmed(localFolder);
    if (path.isEmpty() || !path.startsWith(QLatin1Char('/')) || !exists || exists(path))
        return QString();
    // A drive /etc/fstab names, not mounted now: the longest such target holding the folder.
    QString listed;
    for (const QString &target : mounts.listed)
        if (target != QLatin1String("/") && atOrUnder(path, target) &&
            !mounts.mounted.contains(target) && target.size() > listed.size())
            listed = target;
    if (!listed.isEmpty())
        return listed;

    static const QStringList kRoots{QStringLiteral("/run/media"),
                                    QStringLiteral("/media"),
                                    QStringLiteral("/mnt")};
    QString root;
    for (const QString &r : kRoots)
        if (path.startsWith(r + QLatin1Char('/')))
            root = r;
    if (root.isEmpty())
        return QString();
    QString nearest = path;
    while (nearest != QLatin1String("/") && !exists(nearest)) {
        const qsizetype slash = nearest.lastIndexOf(QLatin1Char('/'));
        nearest = slash <= 0 ? QStringLiteral("/") : nearest.left(slash);
    }
    QString holder; // the filesystem the nearest existing folder is on
    for (const QString &point : mounts.mounted)
        if (atOrUnder(nearest, point) && point.size() > holder.size())
            holder = point;
    if (holder.startsWith(root + QLatin1Char('/')))
        return QString(); // its drive is mounted: the folder is gone from it
    const QStringList parts = path.mid(root.size() + 1).split(QLatin1Char('/'));
    int depth = 1;
    if (root == QLatin1String("/run/media") ||
        (root == QLatin1String("/media") && parts.size() > 1 && parts.first() == user))
        depth = 2;
    return root + QLatin1Char('/') + parts.mid(0, depth).join(QLatin1Char('/'));
}

std::optional<qint64> statLocal(const QString &localPath)
{
    const QFileInfo info(localPath);
    if (!info.exists() || !info.isFile())
        return std::nullopt;
    return info.size();
}

QString shownName(const QList<ContentFile> &files, const QString &path)
{
    const qsizetype slash = path.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return path;
    const QString top = path.left(slash + 1);
    for (const ContentFile &file : files)
        if (!file.path.startsWith(top))
            return path;
    return path.mid(slash + 1);
}

} // namespace omaseed::core::LocateCheck
