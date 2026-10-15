#include "core/TorrentExtras.hpp"

#include <QStringList>

#include <optional>

namespace omaseed::core::TorrentExtras {
namespace {

constexpr int kImageSetFrom = 5;          // this many images or more: an image set
constexpr double kImageShareBelow = 0.01; // images under 1 % of the size are extras

/// The part after the last '.', lower case; "" when there is none (or only a leading one).
QString extensionOf(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.mid(dot + 1).toLower() : QString();
}

/// The name without its extension, lower case.
QString stemOf(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return (dot > 0 ? name.left(dot) : name).toLower();
}

bool isSampleName(const QString &name)
{
    return name == QLatin1String("sample") || name == QLatin1String("samples");
}

bool isImage(const QString &extension)
{
    return extension == QLatin1String("jpg") || extension == QLatin1String("jpeg") ||
           extension == QLatin1String("png");
}

/// Info, Sample or Advert for one path, from its name alone.
std::optional<Kind> kindByName(const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return std::nullopt;
    const QString name = parts.constLast();
    const QString extension = extensionOf(name);

    static const QStringList info{QStringLiteral("nfo"),
                                  QStringLiteral("txt"),
                                  QStringLiteral("sfv"),
                                  QStringLiteral("md5"),
                                  QStringLiteral("srr")};
    if (info.contains(extension))
        return Kind::Info;

    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        if (isSampleName(parts.at(i).toLower()))
            return Kind::Sample;
    }
    if (isSampleName(stemOf(name)) || name.toLower().contains(QLatin1String("-sample.")))
        return Kind::Sample;

    static const QStringList adverts{QStringLiteral("url"),
                                     QStringLiteral("website"),
                                     QStringLiteral("html"),
                                     QStringLiteral("lnk"),
                                     QStringLiteral("exe")};
    if (parts.size() == 1 && adverts.contains(extension))
        return Kind::Advert;
    return std::nullopt;
}

} // namespace

QList<Extra> find(const QList<TorrentFileEntry> &files)
{
    if (files.size() < 2)
        return {};

    qint64 total = 0;
    qint64 imageBytes = 0;
    int images = 0;
    for (const TorrentFileEntry &file : files) {
        total += file.size;
        const QString name = file.path.section(QLatin1Char('/'), -1);
        if (isImage(extensionOf(name))) {
            ++images;
            imageBytes += file.size;
        }
    }
    const bool imagesAreExtras = images > 0 && images < kImageSetFrom &&
                                 double(imageBytes) < kImageShareBelow * double(total);

    QList<Extra> extras;
    for (int index = 0; index < files.size(); ++index) {
        const QString &path = files.at(index).path;
        if (const std::optional<Kind> kind = kindByName(path)) {
            extras.append({index, *kind});
            continue;
        }
        if (imagesAreExtras && isImage(extensionOf(path.section(QLatin1Char('/'), -1))))
            extras.append({index, Kind::Image});
    }
    if (extras.size() == files.size())
        return {};
    return extras;
}

} // namespace omaseed::core::TorrentExtras
