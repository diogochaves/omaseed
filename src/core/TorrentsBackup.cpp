#include "core/TorrentsBackup.hpp"

#include "core/TorrentFile.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace omaseed::core {

namespace {

QJsonArray stringArray(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray()) {
        if (item.isString())
            out.append(item.toString());
    }
    return out;
}

template<class T>
std::optional<T> refuse(QString *error, const QString &why)
{
    if (error)
        *error = why;
    return std::nullopt;
}

/// "Original", "Subfolder" or "NoSubfolder"; anything else reads as Original.
QString knownLayout(const QString &layout)
{
    if (layout == QLatin1String("Subfolder") || layout == QLatin1String("NoSubfolder"))
        return layout;
    return QStringLiteral("Original");
}

/// qBittorrent's root folder for a single file put in a subfolder: its name without the
/// extension ("film.mkv" → "film"), the whole name when it has none.
QString withoutExtension(const QString &name)
{
    const int dot = int(name.lastIndexOf(QLatin1Char('.')));
    return dot > 0 ? name.left(dot) : name;
}

} // namespace

// ---- the manifest ---------------------------------------------------------------------------

QJsonObject TorrentsManifest::toJson() const
{
    QJsonObject server{{QStringLiteral("name"), serverName},
                       {QStringLiteral("url"), serverUrl},
                       {QStringLiteral("qbittorrent"), version},
                       {QStringLiteral("webApi"), apiVersion},
                       {QStringLiteral("savePath"), savePath}};
    if (!downloadPath.isEmpty())
        server.insert(QStringLiteral("downloadPath"), downloadPath);
    return {{QStringLiteral("format"), TorrentsBackup::kFormat},
            {QStringLiteral("version"), TorrentsBackup::kVersion},
            {QStringLiteral("created"), created.toUTC().toString(Qt::ISODate)},
            {QStringLiteral("app"), appVersion},
            {QStringLiteral("server"), server},
            {QStringLiteral("count"), count},
            {QStringLiteral("magnets"), magnets},
            {QStringLiteral("categories"), categories},
            {QStringLiteral("tags"), stringArray(tags)}};
}

std::optional<TorrentsManifest> TorrentsManifest::fromJson(const QJsonObject &object,
                                                           QString *error)
{
    if (object.value(QStringLiteral("format")).toString() != TorrentsBackup::kFormat)
        return refuse<TorrentsManifest>(error, QStringLiteral("not an omaseed torrents export"));
    const int version = object.value(QStringLiteral("version")).toInt(0);
    if (version < 1)
        return refuse<TorrentsManifest>(error, QStringLiteral("a damaged manifest"));
    if (version > TorrentsBackup::kVersion)
        return refuse<TorrentsManifest>(
            error,
            QStringLiteral("made by a newer omaseed (format %1); update to import it")
                .arg(version));
    TorrentsManifest m;
    m.created =
        QDateTime::fromString(object.value(QStringLiteral("created")).toString(), Qt::ISODate);
    m.appVersion = object.value(QStringLiteral("app")).toString();
    const QJsonObject server = object.value(QStringLiteral("server")).toObject();
    m.serverName = server.value(QStringLiteral("name")).toString();
    m.serverUrl = server.value(QStringLiteral("url")).toString();
    m.version = server.value(QStringLiteral("qbittorrent")).toString();
    m.apiVersion = server.value(QStringLiteral("webApi")).toString();
    m.savePath = server.value(QStringLiteral("savePath")).toString();
    m.downloadPath = server.value(QStringLiteral("downloadPath")).toString();
    m.count = object.value(QStringLiteral("count")).toInt();
    m.magnets = object.value(QStringLiteral("magnets")).toInt();
    m.categories = object.value(QStringLiteral("categories")).toObject();
    m.tags = stringList(object.value(QStringLiteral("tags")));
    return m;
}

// ---- a record -------------------------------------------------------------------------------

QStringList TorrentRecord::ids() const
{
    QStringList out;
    for (const QString &hash : {id, infohashV1, infohashV2}) {
        const QString lower = hash.toLower();
        if (!lower.isEmpty() && !out.contains(lower))
            out.append(lower);
    }
    return out;
}

QJsonObject TorrentRecord::toJson() const
{
    QJsonObject o{{QStringLiteral("id"), id},
                  {QStringLiteral("name"), name},
                  {QStringLiteral("size"), size},
                  {QStringLiteral("progress"), progress},
                  {QStringLiteral("savePath"), savePath},
                  {QStringLiteral("category"), category},
                  {QStringLiteral("tags"), stringArray(tags)},
                  {QStringLiteral("autoTmm"), autoTmm},
                  {QStringLiteral("state"), state},
                  {QStringLiteral("queuePosition"), queuePosition},
                  {QStringLiteral("trackers"), stringArray(trackers)}};
    if (!infohashV1.isEmpty())
        o.insert(QStringLiteral("infohashV1"), infohashV1);
    if (!infohashV2.isEmpty())
        o.insert(QStringLiteral("infohashV2"), infohashV2);
    if (!downloadPath.isEmpty())
        o.insert(QStringLiteral("downloadPath"), downloadPath);
    QJsonObject limits{{QStringLiteral("up"), upLimit},
                       {QStringLiteral("down"), dlLimit},
                       {QStringLiteral("ratio"), ratioLimit},
                       {QStringLiteral("seedingTime"), seedingTimeLimit},
                       {QStringLiteral("inactiveSeedingTime"), inactiveSeedingTimeLimit}};
    if (!shareLimitAction.isEmpty())
        limits.insert(QStringLiteral("action"), shareLimitAction);
    o.insert(QStringLiteral("limits"), limits);
    if (sequential)
        o.insert(QStringLiteral("sequential"), true);
    if (firstLastPiecePrio)
        o.insert(QStringLiteral("firstLastPiecePrio"), true);
    if (isMagnet()) {
        o.insert(QStringLiteral("magnet"), magnet);
        return o;
    }
    o.insert(QStringLiteral("layout"), layout);
    if (!renamed.isEmpty()) {
        QJsonArray list;
        for (const Rename &r : renamed)
            list.append(QJsonObject{{QStringLiteral("index"), r.index},
                                    {QStringLiteral("from"), r.from},
                                    {QStringLiteral("to"), r.to}});
        o.insert(QStringLiteral("renamed"), list);
    }
    if (!priorities.isEmpty()) {
        QJsonArray list;
        for (const int p : priorities)
            list.append(p);
        o.insert(QStringLiteral("priorities"), list);
    }
    return o;
}

std::optional<TorrentRecord> TorrentRecord::fromJson(const QJsonObject &o, QString *error)
{
    TorrentRecord r;
    r.id = o.value(QStringLiteral("id")).toString().toLower();
    if (!TorrentsBackup::isTorrentId(r.id))
        return refuse<TorrentRecord>(error, QStringLiteral("a record without a torrent id"));
    r.infohashV1 = o.value(QStringLiteral("infohashV1")).toString().toLower();
    r.infohashV2 = o.value(QStringLiteral("infohashV2")).toString().toLower();
    r.name = o.value(QStringLiteral("name")).toString();
    r.size = o.value(QStringLiteral("size")).toInteger();
    r.progress = o.value(QStringLiteral("progress")).toDouble();
    r.savePath = o.value(QStringLiteral("savePath")).toString();
    r.downloadPath = o.value(QStringLiteral("downloadPath")).toString();
    r.category = o.value(QStringLiteral("category")).toString();
    r.tags = stringList(o.value(QStringLiteral("tags")));
    r.autoTmm = o.value(QStringLiteral("autoTmm")).toBool();
    r.state = o.value(QStringLiteral("state")).toString();
    if (r.state != QLatin1String("stopped") && r.state != QLatin1String("forced"))
        r.state = QStringLiteral("started");
    r.queuePosition = std::max(0, o.value(QStringLiteral("queuePosition")).toInt());
    const QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
    r.upLimit = std::max<qint64>(0, limits.value(QStringLiteral("up")).toInteger());
    r.dlLimit = std::max<qint64>(0, limits.value(QStringLiteral("down")).toInteger());
    r.ratioLimit = limits.value(QStringLiteral("ratio")).toDouble(-2);
    r.seedingTimeLimit = limits.value(QStringLiteral("seedingTime")).toInteger(-2);
    r.inactiveSeedingTimeLimit = limits.value(QStringLiteral("inactiveSeedingTime")).toInteger(-2);
    r.shareLimitAction = limits.value(QStringLiteral("action")).toString();
    r.sequential = o.value(QStringLiteral("sequential")).toBool();
    r.firstLastPiecePrio = o.value(QStringLiteral("firstLastPiecePrio")).toBool();
    r.trackers = stringList(o.value(QStringLiteral("trackers")));
    r.magnet = o.value(QStringLiteral("magnet")).toString();
    if (!r.magnet.isEmpty() && !r.magnet.startsWith(QLatin1String("magnet:?")))
        return refuse<TorrentRecord>(error, QStringLiteral("a damaged magnet link"));
    r.layout = knownLayout(o.value(QStringLiteral("layout")).toString());
    for (const QJsonValue &item : o.value(QStringLiteral("renamed")).toArray()) {
        const QJsonObject x = item.toObject();
        Rename rename{x.value(QStringLiteral("index")).toInt(-1),
                      x.value(QStringLiteral("from")).toString(),
                      x.value(QStringLiteral("to")).toString()};
        // A name that climbs out of the save path is never sent (`..`, an absolute path).
        const auto safe = [](const QString &path) {
            if (path.isEmpty() || path.startsWith(QLatin1Char('/')))
                return false;
            for (const QString &part : path.split(QLatin1Char('/'))) {
                if (part.isEmpty() || part == QLatin1String("..") || part == QLatin1String("."))
                    return false;
            }
            return true;
        };
        if (rename.index < 0 || !safe(rename.from) || !safe(rename.to))
            return refuse<TorrentRecord>(error, QStringLiteral("a damaged rename"));
        r.renamed.append(rename);
    }
    for (const QJsonValue &item : o.value(QStringLiteral("priorities")).toArray())
        r.priorities.append(item.toInt(1));
    return r;
}

// ---- TorrentsBackup -------------------------------------------------------------------------

QString TorrentsBackup::recordEntry(const QString &id)
{
    return QStringLiteral("torrents/") + id + QStringLiteral(".json");
}

QString TorrentsBackup::torrentEntry(const QString &id)
{
    return QStringLiteral("torrents/") + id + QStringLiteral(".torrent");
}

bool TorrentsBackup::isTorrentId(const QString &id)
{
    static const QRegularExpression hex(QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
    return hex.match(id).hasMatch();
}

QStringList TorrentsBackup::namesUnder(const TorrentFile &parsed, const QString &layout)
{
    QStringList names;
    names.reserve(parsed.files.size());
    for (const TorrentFileEntry &file : parsed.files) {
        if (!parsed.multiFile) {
            names.append(layout == QLatin1String("Subfolder")
                             ? withoutExtension(file.path) + QLatin1Char('/') + file.path
                             : file.path);
        } else if (layout == QLatin1String("NoSubfolder")) {
            names.append(file.path);
        } else {
            names.append(parsed.name + QLatin1Char('/') + file.path);
        }
    }
    return names;
}

QPair<QString, QList<TorrentRecord::Rename>> TorrentsBackup::renamesOf(const TorrentFile &parsed,
                                                                       const QStringList &names)
{
    QPair<QString, QList<TorrentRecord::Rename>> best;
    bool first = true;
    QStringList layouts{QStringLiteral("Original")};
    layouts.append(parsed.multiFile ? QStringLiteral("NoSubfolder") : QStringLiteral("Subfolder"));
    for (const QString &layout : layouts) {
        const QStringList under = namesUnder(parsed, layout);
        QList<TorrentRecord::Rename> renames;
        for (qsizetype i = 0; i < under.size() && i < names.size(); ++i) {
            if (under.at(i) != names.at(i))
                renames.append({int(i), under.at(i), names.at(i)});
        }
        if (first || renames.size() < best.second.size())
            best = {layout, renames};
        first = false;
    }
    return best;
}

TorrentRecord TorrentsBackup::recordFrom(const QJsonObject &info,
                                         const QJsonArray &files,
                                         const QJsonArray &trackers,
                                         const QByteArray &torrent)
{
    TorrentRecord r;
    r.id = info.value(QStringLiteral("hash")).toString().toLower();
    r.infohashV1 = info.value(QStringLiteral("infohash_v1")).toString().toLower();
    r.infohashV2 = info.value(QStringLiteral("infohash_v2")).toString().toLower();
    r.name = info.value(QStringLiteral("name")).toString();
    r.size = std::max<qint64>(0, info.value(QStringLiteral("size")).toInteger());
    r.progress = info.value(QStringLiteral("progress")).toDouble();
    r.savePath = info.value(QStringLiteral("save_path")).toString();
    r.downloadPath = info.value(QStringLiteral("download_path")).toString();
    r.category = info.value(QStringLiteral("category")).toString();
    for (const QString &tag :
         info.value(QStringLiteral("tags")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        r.tags.append(tag.trimmed());
    r.autoTmm = info.value(QStringLiteral("auto_tmm")).toBool();
    const TorrentStatus status =
        QbtTranslate::status(info.value(QStringLiteral("state")).toString());
    if (status.isStopped())
        r.state = QStringLiteral("stopped");
    else if (info.value(QStringLiteral("force_start")).toBool())
        r.state = QStringLiteral("forced");
    else
        r.state = QStringLiteral("started");
    r.queuePosition = std::max(0, info.value(QStringLiteral("priority")).toInt());
    r.upLimit = std::max<qint64>(0, info.value(QStringLiteral("up_limit")).toInteger());
    r.dlLimit = std::max<qint64>(0, info.value(QStringLiteral("dl_limit")).toInteger());
    r.ratioLimit = info.value(QStringLiteral("ratio_limit")).toDouble(-2);
    r.seedingTimeLimit = info.value(QStringLiteral("seeding_time_limit")).toInteger(-2);
    r.inactiveSeedingTimeLimit =
        info.value(QStringLiteral("inactive_seeding_time_limit")).toInteger(-2);
    r.shareLimitAction = info.value(QStringLiteral("share_limit_action")).toString();
    r.sequential = info.value(QStringLiteral("seq_dl")).toBool();
    r.firstLastPiecePrio = info.value(QStringLiteral("f_l_piece_prio")).toBool();
    for (const TrackerEntry &tracker : QbtTranslate::trackers(trackers)) {
        // DHT, PeX and LSD are listed among the trackers: not trackers.
        if (tracker.source == TrackerSource::Announce && !r.trackers.contains(tracker.url))
            r.trackers.append(tracker.url);
    }

    if (torrent.isEmpty()) {
        r.magnet = info.value(QStringLiteral("magnet_uri")).toString();
        return r;
    }
    QStringList names;
    QList<int> priorities;
    bool allNormal = true;
    for (const QJsonValue &item : files) {
        const QJsonObject file = item.toObject();
        names.append(file.value(QStringLiteral("name")).toString());
        const int priority = file.value(QStringLiteral("priority")).toInt(1);
        priorities.append(priority);
        allNormal = allNormal && priority == 1;
    }
    if (!allNormal)
        r.priorities = priorities;
    const TorrentFile parsed = TorrentFile::parse(torrent);
    if (parsed.isValid() && !names.isEmpty()) {
        const auto [layout, renames] = renamesOf(parsed, names);
        r.layout = layout;
        r.renamed = renames;
    }
    return r;
}

std::optional<TorrentsBackup::Contents> TorrentsBackup::read(const QString &path, QString *error)
{
    using Result = std::optional<Contents>;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return refuse<Contents>(error, QStringLiteral("cannot open %1").arg(path));
    TarReader reader(&file);
    const std::optional<TarReader::Header> first = reader.next();
    if (!first || first->name != kManifest) {
        return refuse<Contents>(error,
                                reader.error().isEmpty() || reader.error().contains(u"not a tar")
                                    ? QStringLiteral("not an omaseed torrents export")
                                    : reader.error());
    }
    const auto json = [&reader](const TarReader::Header &h) {
        return QJsonDocument::fromJson(reader.read(h)).object();
    };
    QString why;
    const std::optional<TorrentsManifest> manifest = TorrentsManifest::fromJson(json(*first), &why);
    if (!manifest)
        return refuse<Contents>(error, why);
    Contents contents;
    contents.path = QFileInfo(path).absoluteFilePath();
    contents.manifest = *manifest;
    QSet<QString> seen;
    while (const std::optional<TarReader::Header> header = reader.next()) {
        const QString &name = header->name;
        if (!name.startsWith(QLatin1String("torrents/")))
            continue; // a later version's extra: not ours to read
        const QString stem = QFileInfo(name).completeBaseName();
        if (name == torrentEntry(stem)) {
            contents.torrents.insert(stem, *header);
            continue;
        }
        if (name != recordEntry(stem))
            continue;
        const std::optional<TorrentRecord> record = TorrentRecord::fromJson(json(*header), &why);
        if (!record || record->id != stem)
            return refuse<Contents>(error, QStringLiteral("a damaged record (%1)").arg(name));
        if (seen.contains(record->id))
            continue;
        seen.insert(record->id);
        contents.records.append(*record);
    }
    if (!reader.atEnd())
        return refuse<Contents>(error, QStringLiteral("a damaged file: %1").arg(reader.error()));
    for (const TorrentRecord &record : std::as_const(contents.records)) {
        if (!record.isMagnet() && !contents.torrents.contains(record.id))
            return refuse<Contents>(
                error, QStringLiteral("the .torrent of %1 is missing").arg(record.name));
    }
    return Result(std::move(contents));
}

QByteArray TorrentsBackup::torrentBytes(QIODevice *device,
                                        const Contents &contents,
                                        const QString &id,
                                        QString *error)
{
    const auto it = contents.torrents.constFind(id);
    if (it == contents.torrents.constEnd()) {
        if (error)
            *error = QStringLiteral("no .torrent for %1").arg(id);
        return {};
    }
    return TarReader::read(device, *it, error);
}

} // namespace omaseed::core
