#include "core/LocateCheck.hpp"

#include <QFileInfo>

namespace omaseed::core::LocateCheck {

Result check(const QList<ContentFile> &files, const QString &localFolder, const Stat &stat)
{
    Result result;
    QString folder = localFolder;
    while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    for (const ContentFile &file : files) {
        if (file.priority && *file.priority == 0)
            continue; // Q9: a skipped file isn't required
        ++result.files;
        const std::optional<qint64> size =
            stat ? stat(folder + QLatin1Char('/') + file.path) : std::nullopt;
        const bool complete = file.progress >= 1.0;
        const bool fits = size.has_value() && (complete ? *size == file.size : *size <= file.size);
        if (fits) {
            ++result.found;
            result.bytes += *size;
        } else if (result.firstMissing.isEmpty()) {
            result.firstMissing = file.path;
        }
    }
    return result;
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
