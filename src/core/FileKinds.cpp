#include "core/FileKinds.hpp"

#include "core/TorrentExtras.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace omaseed::core::FileKinds {

namespace {

/// The part after the last '.', lower case; "" when there is none (or only a leading one).
QString extensionOf(const QString &path)
{
    const QString name = path.section(QLatin1Char('/'), -1);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.mid(dot + 1).toLower() : QString();
}

/// 59 A1's extensions, by kind (extras and other have none of their own).
const QHash<QString, Kind> &extensions()
{
    static const QHash<QString, Kind> table = [] {
        QHash<QString, Kind> made;
        const auto add = [&made](Kind kind, const char *list) {
            for (const QString &ext : QString::fromLatin1(list).split(QLatin1Char(' ')))
                made.insert(ext, kind);
        };
        add(Kind::Video, "mkv mp4 m4v avi mov webm wmv ts m2ts mpg mpeg");
        add(Kind::Audio, "flac mp3 m4a aac ogg opus wav ape wv dsf");
        add(Kind::Subtitles, "srt ass ssa sub idx vtt sup");
        add(Kind::Images, "jpg jpeg png webp gif tif tiff bmp heic");
        add(Kind::Books, "epub pdf mobi azw3 cbz cbr djvu");
        add(Kind::Archives, "zip rar 7z tar gz xz zst");
        add(Kind::DiscImages, "iso img bin cue nrg dmg");
        return made;
    }();
    return table;
}

/// A split set's volumes: ".r00" … ".r99" and ".001" ….
bool isVolumeExtension(const QString &extension)
{
    static const QRegularExpression volume(QStringLiteral("^(r\\d\\d|\\d{3})$"));
    return volume.match(extension).hasMatch();
}

/// The key an archive's file counts under: one per split set (its folder and stem), the path
/// itself for a lone archive.
QString archiveKey(const QString &path)
{
    const QString lower = path.toLower();
    // x.part1.rar, x.part02.rar
    static const QRegularExpression part(QStringLiteral("^(.*)\\.part\\d+\\.rar$"));
    if (const QRegularExpressionMatch match = part.match(lower); match.hasMatch())
        return QStringLiteral("rar:") + match.captured(1);
    // x.rar with x.r00, x.r01
    static const QRegularExpression rar(QStringLiteral("^(.*)\\.(rar|r\\d\\d)$"));
    if (const QRegularExpressionMatch match = rar.match(lower); match.hasMatch())
        return QStringLiteral("rar:") + match.captured(1);
    // x.7z.001, x.002
    static const QRegularExpression numbered(QStringLiteral("^(.*)\\.\\d{3}$"));
    if (const QRegularExpressionMatch match = numbered.match(lower); match.hasMatch())
        return QStringLiteral("num:") + match.captured(1);
    return QStringLiteral("one:") + lower;
}

} // namespace

QList<Kind> all()
{
    return {Kind::Video,
            Kind::Audio,
            Kind::Subtitles,
            Kind::Images,
            Kind::Books,
            Kind::Archives,
            Kind::DiscImages,
            Kind::Extras,
            Kind::Other};
}

QString name(Kind kind)
{
    switch (kind) {
    case Kind::Video:
        return QStringLiteral("video");
    case Kind::Audio:
        return QStringLiteral("audio");
    case Kind::Subtitles:
        return QStringLiteral("subtitles");
    case Kind::Images:
        return QStringLiteral("images");
    case Kind::Books:
        return QStringLiteral("books");
    case Kind::Archives:
        return QStringLiteral("archives");
    case Kind::DiscImages:
        return QStringLiteral("discImages");
    case Kind::Extras:
        return QStringLiteral("extras");
    case Kind::Other:
        break;
    }
    return QStringLiteral("other");
}

std::optional<Kind> fromName(const QString &name)
{
    for (Kind kind : all()) {
        if (FileKinds::name(kind) == name)
            return kind;
    }
    return std::nullopt;
}

QString word(Kind kind, bool plural)
{
    switch (kind) {
    case Kind::Video:
        return plural ? QStringLiteral("videos") : QStringLiteral("video");
    case Kind::Audio:
        return plural ? QStringLiteral("audio files") : QStringLiteral("audio file");
    case Kind::Subtitles:
        return plural ? QStringLiteral("subtitles") : QStringLiteral("subtitle");
    case Kind::Images:
        return plural ? QStringLiteral("images") : QStringLiteral("image");
    case Kind::Books:
        return plural ? QStringLiteral("books") : QStringLiteral("book");
    case Kind::Archives:
        return plural ? QStringLiteral("archives") : QStringLiteral("archive");
    case Kind::DiscImages:
        return plural ? QStringLiteral("disc images") : QStringLiteral("disc image");
    case Kind::Extras:
        return plural ? QStringLiteral("extras") : QStringLiteral("extra");
    case Kind::Other:
        break;
    }
    return plural ? QStringLiteral("other files") : QStringLiteral("other file");
}

QList<Kind> classify(const QList<TorrentFileEntry> &files)
{
    QSet<int> extras;
    for (const TorrentExtras::Extra &extra : TorrentExtras::find(files))
        extras.insert(extra.index);

    QList<Kind> kinds;
    kinds.reserve(files.size());
    for (int index = 0; index < int(files.size()); ++index) {
        if (extras.contains(index)) {
            kinds.append(Kind::Extras);
            continue;
        }
        const QString extension = extensionOf(files.at(index).path);
        const auto known = extensions().constFind(extension);
        if (known != extensions().constEnd())
            kinds.append(*known);
        else if (isVolumeExtension(extension))
            kinds.append(Kind::Archives);
        else
            kinds.append(Kind::Other);
    }
    return kinds;
}

int units(Kind kind, const QList<TorrentFileEntry> &files, const QList<Kind> &kinds)
{
    if (kind != Kind::Archives) {
        int count = 0;
        for (Kind each : kinds) {
            if (each == kind)
                ++count;
        }
        return count;
    }
    QSet<QString> sets;
    for (int index = 0; index < int(kinds.size()) && index < int(files.size()); ++index) {
        if (kinds.at(index) == Kind::Archives)
            sets.insert(archiveKey(files.at(index).path));
    }
    return int(sets.size());
}

} // namespace omaseed::core::FileKinds
