#include "core/BackupFormat.hpp"

#include "core/OmaseedBackup.hpp"
#include "core/QbtSettingsExport.hpp"
#include "core/TarArchive.hpp"
#include "core/TorrentsBackup.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace omaseed::core::BackupFormat {

namespace {

constexpr qint64 kBlock = 512;
// The biggest JSON file read whole to find its "format": a settings export is tens of KB.
constexpr qint64 kMaxJson = 64 * 1024 * 1024;
// How many entries are passed over looking for the manifest. omaseed writes it first; a tar
// rebuilt by hand may not, but its manifest is never far.
constexpr int kMaxEntries = 64;

Kind fromFormat(const QString &format)
{
    if (format == OmaseedBackup::kFormat)
        return Kind::OmaseedBackup;
    if (format == TorrentsBackup::kFormat)
        return Kind::OmaseedTorrents;
    if (format == QbtSettingsExport::kFormat)
        return Kind::QbtSettings;
    return Kind::Unknown;
}

QString formatOf(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return QString();
    return doc.object().value(QStringLiteral("format")).toString();
}

Kind detectTar(QIODevice *device)
{
    TarReader reader(device);
    for (int i = 0; i < kMaxEntries; ++i) {
        const std::optional<TarReader::Header> header = reader.next();
        if (!header)
            return Kind::Unknown;
        if (header->name != TorrentsBackup::kManifest)
            continue;
        if (header->size > kMaxJson)
            return Kind::Unknown;
        return fromFormat(formatOf(reader.read(*header)));
    }
    return Kind::Unknown;
}

} // namespace

Kind detect(QIODevice *device)
{
    if (!device || !device->isOpen() || device->isSequential())
        return Kind::Unknown;
    const qint64 was = device->pos();
    Kind kind = Kind::Unknown;
    if (device->seek(0)) {
        const QByteArray head = device->peek(kBlock);
        if (head.size() == kBlock && head.mid(257, 5) == "ustar") {
            kind = detectTar(device);
        } else if (device->size() <= kMaxJson) {
            kind = fromFormat(formatOf(device->readAll()));
            // A settings export is the only JSON omaseed writes; a tar's manifest read on its
            // own is not one of its files.
            if (kind != Kind::QbtSettings)
                kind = Kind::Unknown;
        }
    }
    device->seek(was);
    return kind;
}

Kind detectFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Kind::Unknown;
    return detect(&file);
}

QString formatName(Kind kind)
{
    switch (kind) {
    case Kind::OmaseedBackup:
        return OmaseedBackup::kFormat;
    case Kind::OmaseedTorrents:
        return TorrentsBackup::kFormat;
    case Kind::QbtSettings:
        return QbtSettingsExport::kFormat;
    case Kind::Unknown:
        break;
    }
    return QString();
}

} // namespace omaseed::core::BackupFormat
