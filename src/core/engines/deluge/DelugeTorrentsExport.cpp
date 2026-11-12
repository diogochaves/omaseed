#include "core/engines/deluge/DelugeTorrentsExport.hpp"

#include "core/TorrentFile.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTranslate.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

namespace omaseed::core {

namespace DT = DelugeTranslate;

namespace {

using Retry = DelugeRpc::Retry;

QString text(const QJsonObject &json, const char *name)
{
    return json.value(QLatin1String(name)).toString();
}

qint64 integer(const QJsonObject &json, const char *name, qint64 fallback = 0)
{
    const QJsonValue value = json.value(QLatin1String(name));
    return value.isDouble() ? qint64(value.toDouble()) : fallback;
}

/// A magnet waiting for its metadata has no pieces yet (DelugeTranslate).
bool hasMetadata(const QJsonObject &json)
{
    const QJsonValue pieces = json.value(QLatin1String("num_pieces"));
    return !pieces.isDouble() || pieces.toDouble() > 0;
}

/// A limit in Deluge's KiB/s as bytes/s; -1 (and 0) are none.
qint64 limitBytes(const QJsonObject &json, const char *name)
{
    const double kib = json.value(QLatin1String(name)).toDouble(-1);
    return kib > 0.0 ? qint64(std::llround(kib * 1024.0)) : 0;
}

QStringList paths(const QJsonObject &json, const char *name)
{
    QStringList list;
    for (const QJsonValue &file : json.value(QLatin1String(name)).toArray())
        list.append(file.toObject().value(QLatin1String("path")).toString());
    return list;
}

/// core.get_torrents_status' params: `keys` of the torrents of `ids`, of every one when empty.
QJsonArray statusOf(const QStringList &keys, const QStringList &ids = {})
{
    QJsonObject filter;
    if (!ids.isEmpty())
        filter.insert(QStringLiteral("id"), QJsonArray::fromStringList(ids));
    return {filter, QJsonArray::fromStringList(keys)};
}

} // namespace

DelugeTorrentsExport::DelugeTorrentsExport(DelugeEngine *engine, QObject *parent)
    : QObject(parent)
    , m_rpc(engine != nullptr ? engine->rpc() : nullptr)
{}

DelugeTorrentsExport::~DelugeTorrentsExport()
{
    if (m_file)
        m_file->cancelWriting();
}

void DelugeTorrentsExport::setTorrentsFolder(const QString &folder)
{
    if (!m_busy)
        m_torrentsFolder = folder;
}

QString DelugeTorrentsExport::stateFolder(const QJsonObject &config)
{
    // C40 X3: nothing names the config folder. `plugins_location` is `<config>/plugins` as
    // Deluge writes it into a new core.conf; `state` is its sibling.
    const QString plugins = QDir::cleanPath(text(config, "plugins_location"));
    if (!plugins.startsWith(QLatin1Char('/')))
        return {};
    return plugins.left(plugins.lastIndexOf(QLatin1Char('/'))) + QStringLiteral("/state");
}

QString DelugeTorrentsExport::localTorrentPath(const QString &id,
                                               const QString &daemonFolder,
                                               const QString &torrentsFolder)
{
    const QString &folder = torrentsFolder.isEmpty() ? daemonFolder : torrentsFolder;
    if (id.isEmpty() || folder.isEmpty())
        return {};
    return QDir(folder).filePath(id + QStringLiteral(".torrent"));
}

QByteArray DelugeTorrentsExport::readTorrent(const QString &path, const QString &hash)
{
    if (path.isEmpty() || hash.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    const TorrentFile parsed = TorrentFile::parse(bytes);
    // A hybrid or a v2 torrent goes by its v2 hash cut short on a Deluge with libtorrent 2,
    // as on qBittorrent (TorrentFile::torrentId).
    const QString id = hash.toLower();
    if (!parsed.isValid() || (parsed.infoHashV1 != id && parsed.torrentId() != id))
        return {};
    return bytes;
}

QStringList DelugeTorrentsExport::recordFields()
{
    return {QStringLiteral("name"),
            QStringLiteral("state"),
            QStringLiteral("paused"),
            QStringLiteral("num_pieces"),
            QStringLiteral("progress"),
            QStringLiteral("total_wanted"),
            QStringLiteral("download_location"),
            QStringLiteral("save_path"),
            QStringLiteral("label"),
            QStringLiteral("max_download_speed"),
            QStringLiteral("max_upload_speed"),
            QStringLiteral("stop_at_ratio"),
            QStringLiteral("stop_ratio"),
            QStringLiteral("remove_at_ratio"),
            QStringLiteral("sequential_download"),
            QStringLiteral("prioritize_first_last"),
            QStringLiteral("files"),
            QStringLiteral("orig_files"),
            QStringLiteral("file_priorities"),
            QStringLiteral("trackers")};
}

QString DelugeTorrentsExport::magnetOf(const QJsonObject &json)
{
    const QString id = text(json, "hash").toLower();
    if (id.isEmpty())
        return {};
    QString link = QStringLiteral("magnet:?xt=urn:btih:") + id;
    const QString name = text(json, "name");
    if (!name.isEmpty())
        link += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(name));
    for (const QString &url : DT::trackerUrls(json))
        link += QStringLiteral("&tr=") + QString::fromLatin1(QUrl::toPercentEncoding(url));
    return link;
}

TorrentRecord DelugeTorrentsExport::recordFrom(const QJsonObject &json,
                                               bool labelPlugin,
                                               int queuePosition,
                                               const QByteArray &torrent,
                                               const QString &magnet)
{
    TorrentRecord r;
    // Deluge's id for a torrent is its v1 info hash, a hybrid's too.
    r.id = text(json, "hash").toLower();
    r.infohashV1 = r.id;
    r.name = text(json, "name");
    r.size = std::max<qint64>(0, integer(json, "total_wanted"));
    r.progress = std::clamp(json.value(QLatin1String("progress")).toDouble() / 100.0, 0.0, 1.0);
    // Where the files are now. `download_location` follows them: a finished torrent that
    // "move completed" took elsewhere reads its new folder there (and the switch off again),
    // so `move_completed_path` is never the answer; `save_path` is the same key's old name.
    // No `downloadPath`: move-completed is a folder for finished torrents, not an incomplete
    // one, and a record has no place for it.
    r.savePath = text(json, "download_location");
    if (r.savePath.isEmpty())
        r.savePath = text(json, "save_path");
    // C40 X4: the one label is the category; no tags. With the plugin off, deluge-web may
    // still send the label a torrent last had: not read.
    if (labelPlugin)
        r.category = text(json, "label");
    // Paused by its state, as the list reads it (DelugeTranslate::isPaused): `paused` alone
    // is also true of a queued torrent.
    r.state = DT::isPaused(json) ? QStringLiteral("stopped") : QStringLiteral("started");
    r.queuePosition = std::max(0, queuePosition);
    r.dlLimit = limitBytes(json, "max_download_speed");
    r.upLimit = limitBytes(json, "max_upload_speed");
    // The torrent's own keys only. Without `stop_at_ratio` it has no limit of its own, and
    // the record says "the server's" (-2), so the target's default holds there.
    if (json.value(QLatin1String("stop_at_ratio")).toBool()) {
        r.ratioLimit = json.value(QLatin1String("stop_ratio")).toDouble();
        r.shareLimitAction = json.value(QLatin1String("remove_at_ratio")).toBool()
                                 ? QStringLiteral("Remove")
                                 : QStringLiteral("Stop");
    } else {
        r.shareLimitAction = QStringLiteral("Default");
    }
    r.sequential = json.value(QLatin1String("sequential_download")).toBool();
    r.firstLastPiecePrio = json.value(QLatin1String("prioritize_first_last")).toBool();

    // In tier order, whatever order the daemon listed them in. Always the API's (C40 X3).
    QList<QJsonObject> byTier;
    for (const QJsonValue &tracker : json.value(QLatin1String("trackers")).toArray())
        byTier.append(tracker.toObject());
    std::stable_sort(byTier.begin(), byTier.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return integer(a, "tier") < integer(b, "tier");
    });
    for (const QJsonObject &tracker : std::as_const(byTier)) {
        const QString url = text(tracker, "url");
        if (!url.isEmpty() && !r.trackers.contains(url))
            r.trackers.append(url);
    }

    // The record's priorities are qBittorrent's (TorrentsBackup), by the Files tab's mapping:
    // 0 not wanted, 1–3 low (2 in a record, C40 X6), 4 normal (1), 5–7 high (6).
    const QStringList names = paths(json, "files");
    const QJsonArray filePriorities = json.value(QLatin1String("file_priorities")).toArray();
    QList<int> priorities;
    bool allNormal = true;
    for (const QJsonValue &value : filePriorities) {
        const int priority = DT::filePriority(value.toInt(4));
        priorities.append(priority);
        allNormal = allNormal && priority == int(FilePriority::Normal);
    }
    if (!allNormal)
        r.priorities = priorities;

    const bool metadata = hasMetadata(json);
    const TorrentFile parsed = torrent.isEmpty() ? TorrentFile() : TorrentFile::parse(torrent);
    if (!parsed.isValid() || !metadata) {
        // C40 X3: a link, with everything above. Deluge keeps the .torrent's own names
        // (`orig_files`) beside the current ones, so a link's renames are told from those,
        // as they read under the layout "Original".
        r.magnet = magnet.startsWith(QLatin1String("magnet:?")) ? magnet : magnetOf(json);
        const QStringList original = paths(json, "orig_files");
        if (metadata && !names.isEmpty() && original.size() == names.size()) {
            for (qsizetype i = 0; i < names.size(); ++i) {
                if (original.at(i) != names.at(i))
                    r.renamed.append({int(i), original.at(i), names.at(i)});
            }
        }
        return r;
    }
    // The id is qBittorrent's (TorrentFile::torrentIdFor), so a hybrid's is its v2 hash cut
    // short: what a qBittorrent will call it once it is added there.
    r.infohashV1 = parsed.infoHashV1; // the daemon's id may be the v2 hash cut short
    r.infohashV2 = parsed.infoHashV2;
    r.id = parsed.torrentId();
    if (!names.isEmpty()) {
        const auto [layout, renames] = TorrentsBackup::renamesOf(parsed, names);
        r.layout = layout;
        r.renamed = renames;
    }
    return r;
}

Reply<DelugeTorrentsExport::Reach> *
DelugeTorrentsExport::reach(DelugeEngine *engine, const QString &torrentsFolder, QObject *parent)
{
    if (parent == nullptr)
        parent = engine;
    auto *reply = new Reply<Reach>(parent);
    if (engine == nullptr) {
        QMetaObject::invokeMethod(
            reply,
            [reply] {
                reply->complete(
                    Result<Reach>(ApiError(ApiError::Kind::Unknown, QStringLiteral("no server"))));
            },
            Qt::QueuedConnection);
        return reply;
    }
    // deluge-web takes one call per POST: the settings, then the torrents.
    const QPointer<DelugeRpc> rpc(engine->rpc());
    rpc->call(QStringLiteral("core.get_config"), {}, Retry::OnTransient)
        ->then(reply, [reply, rpc, torrentsFolder](const Result<QJsonValue> &config) {
            if (config.isError())
                return reply->complete(Result<Reach>(config.error()));
            if (rpc.isNull())
                return reply->complete(
                    Result<Reach>(ApiError(ApiError::Kind::Unknown, QStringLiteral("no server"))));
            const QString daemonFolder = stateFolder(config.value().toObject());
            rpc->call(QStringLiteral("core.get_torrents_status"),
                      statusOf({QStringLiteral("num_pieces")}),
                      Retry::OnTransient)
                ->then(
                    reply, [reply, daemonFolder, torrentsFolder](const Result<QJsonValue> &status) {
                        if (status.isError())
                            return reply->complete(Result<Reach>(status.error()));
                        Reach reach;
                        reach.daemonFolder = daemonFolder;
                        for (const QJsonObject &row : DT::rows(status.value())) {
                            if (!hasMetadata(row))
                                continue;
                            const QString id = text(row, "hash");
                            ++reach.tried;
                            if (readTorrent(localTorrentPath(id, daemonFolder, torrentsFolder), id)
                                    .isEmpty())
                                ++reach.missing;
                            if (reach.tried >= kReachSample)
                                break;
                        }
                        if (reach.tried > 0) {
                            reach.readable = reach.missing == 0;
                        } else {
                            const QString where =
                                torrentsFolder.isEmpty() ? daemonFolder : torrentsFolder;
                            const QFileInfo folder(where);
                            reach.readable =
                                !where.isEmpty() && folder.isDir() && folder.isReadable();
                        }
                        reply->complete(reach);
                    });
        });
    return reply;
}

void DelugeTorrentsExport::start(const QString &path,
                                 const QString &serverName,
                                 const QString &serverUrl,
                                 const QString &appVersion)
{
    if (m_busy)
        return;
    m_busy = true;
    ++m_generation;
    m_path = path;
    m_manifest = TorrentsManifest();
    m_manifest.engine = EngineKind::Deluge; // DECISIONS C40 X1
    m_manifest.created = QDateTime::currentDateTimeUtc();
    m_manifest.appVersion = appVersion;
    m_manifest.serverName = serverName;
    m_manifest.serverUrl = serverUrl;
    m_ids.clear();
    m_queue.clear();
    m_pending.clear();
    m_daemonFolder.clear();
    m_labelPlugin = false;
    m_outcome = Outcome();
    m_outcome.path = path;
    m_next = 0;
    readVersion();
}

void DelugeTorrentsExport::cancel()
{
    if (!m_busy)
        return;
    Outcome outcome = m_outcome;
    outcome.cancelled = true;
    outcome.error = QStringLiteral("stopped");
    finish(outcome);
}

void DelugeTorrentsExport::fail(const QString &error)
{
    Outcome outcome = m_outcome;
    outcome.error = error;
    finish(outcome);
}

void DelugeTorrentsExport::finish(Outcome outcome)
{
    ++m_generation; // answers still on their way are dropped
    m_busy = false;
    m_pending.clear();
    m_writer.reset();
    if (m_file) {
        if (outcome.ok) {
            if (!m_file->commit()) {
                outcome.ok = false;
                outcome.error =
                    QStringLiteral("cannot write %1: %2").arg(m_path, m_file->errorString());
            }
        } else {
            m_file->cancelWriting();
        }
        m_file.reset();
    }
    Q_EMIT finished(outcome);
}

void DelugeTorrentsExport::readVersion()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    m_rpc->call(QStringLiteral("daemon.get_version"), {}, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            // "2.2.0", or a build's "2.2.0.dev12+g…" as it says it; no words after it.
            m_manifest.version = r.value().toString().section(QLatin1Char(' '), 0, 0);
            readConfig();
        });
}

void DelugeTorrentsExport::readConfig()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    m_rpc->call(QStringLiteral("core.get_config"), {}, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            const QJsonObject config = r.value().toObject();
            m_manifest.savePath = text(config, "download_location");
            // `downloadPath` stays empty: Deluge's `move_completed_path` is where finished
            // torrents go, not a folder for the unfinished ones.
            m_daemonFolder = stateFolder(config);
            readPlugins();
        });
}

void DelugeTorrentsExport::readPlugins()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    m_rpc->call(QStringLiteral("core.get_enabled_plugins"), {}, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            m_labelPlugin = r.value().toArray().contains(QStringLiteral("Label"));
            readList();
        });
}

void DelugeTorrentsExport::readList()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    m_rpc
        ->call(QStringLiteral("core.get_torrents_status"),
               statusOf({QStringLiteral("queue"), QStringLiteral("num_pieces")}),
               Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            QList<QPair<qint64, QString>> unfinished;
            // By id: the answer is keyed by it, and Deluge has no order of its own.
            for (const QJsonObject &row : DT::rows(r.value())) {
                const QString id = text(row, "hash");
                if (id.isEmpty())
                    continue;
                m_ids.append(id);
                // Deluge queues only what isn't finished: -1 is "not in it".
                const qint64 queue = integer(row, "queue", -1);
                if (queue >= 0)
                    unfinished.append({queue, id});
                // What the manifest says before the torrents are read: a link for want of
                // metadata, or of a file to open. One that turns out to be another torrent's
                // is only known when it is read, and counted in the outcome.
                const QString path = localTorrentPath(id, m_daemonFolder, m_torrentsFolder);
                const QFileInfo file(path);
                if (!hasMetadata(row) || path.isEmpty() || !file.isFile() || !file.isReadable())
                    ++m_manifest.magnets;
            }
            m_manifest.count = int(m_ids.size());
            // qBittorrent's queue: the unfinished ones, from 1.
            std::stable_sort(unfinished.begin(),
                             unfinished.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            for (qsizetype i = 0; i < unfinished.size(); ++i)
                m_queue.insert(unfinished.at(i).second, int(i) + 1);
            open();
        });
}

void DelugeTorrentsExport::open()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    m_file = std::make_unique<QSaveFile>(m_path);
    if (!m_file->open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write %1: %2").arg(m_path, m_file->errorString()));
    // Private before a byte is in it: announce URLs can carry passkeys.
    m_file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_writer = std::make_unique<TarWriter>(m_file.get());
    QString error;
    if (!m_writer->add(TorrentsBackup::kManifest,
                       QJsonDocument(m_manifest.toJson()).toJson(QJsonDocument::Indented),
                       m_manifest.created.toSecsSinceEpoch(),
                       &error))
        return fail(error);
    Q_EMIT progress(0, m_manifest.count);
    nextBatch();
}

void DelugeTorrentsExport::nextBatch()
{
    if (!m_busy) // stopped from a progress handler
        return;
    if (m_next >= m_ids.size()) {
        QString error;
        if (!m_writer->finish(&error))
            return fail(error);
        Outcome outcome = m_outcome;
        outcome.ok = true;
        return finish(outcome);
    }
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const QStringList ids = m_ids.mid(m_next, kBatch);
    m_next += int(ids.size());
    const int generation = m_generation;
    m_rpc
        ->call(QStringLiteral("core.get_torrents_status"),
               statusOf(recordFields(), ids),
               Retry::OnTransient)
        ->then(this, [this, generation, ids](const Result<QJsonValue> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            // In the order asked; one the daemon no longer has is not among them.
            QHash<QString, QJsonObject> byId;
            for (const QJsonObject &row : DT::rows(r.value()))
                byId.insert(text(row, "hash"), row);
            m_pending.clear();
            for (const QString &id : ids) {
                if (!byId.contains(id)) {
                    ++m_outcome.gone;
                    continue;
                }
                Pending pending;
                pending.row = byId.value(id);
                // C40 X3: in the daemon's state folder, or under the folder given.
                if (hasMetadata(pending.row))
                    pending.torrent =
                        readTorrent(localTorrentPath(id, m_daemonFolder, m_torrentsFolder), id);
                m_pending.append(pending);
            }
            m_magnetAt = 0;
            nextMagnet();
        });
}

void DelugeTorrentsExport::nextMagnet()
{
    // One core.get_magnet_uri for each torrent of the batch that goes in as a link.
    while (m_magnetAt < m_pending.size() && !m_pending.at(m_magnetAt).torrent.isEmpty())
        ++m_magnetAt;
    if (m_magnetAt >= m_pending.size())
        return writeBatch();
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    const QString id = text(m_pending.at(m_magnetAt).row, "hash");
    m_rpc->call(QStringLiteral("core.get_magnet_uri"), {id}, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonValue> &r) {
            if (!m_busy || generation != m_generation)
                return;
            // No answer (the torrent just went, a daemon that has no such method): the link
            // recordFrom makes of what the status said.
            if (r.isValid())
                m_pending[m_magnetAt].magnet = r.value().toString();
            ++m_magnetAt;
            nextMagnet();
        });
}

void DelugeTorrentsExport::writeBatch()
{
    const qint64 now = m_manifest.created.toSecsSinceEpoch();
    QString error;
    const QList<Pending> batch = std::exchange(m_pending, {});
    for (const Pending &pending : batch) {
        const QString id = text(pending.row, "hash");
        const TorrentRecord record = recordFrom(
            pending.row, m_labelPlugin, m_queue.value(id), pending.torrent, pending.magnet);
        if (!TorrentsBackup::isTorrentId(record.id) ||
            (record.isMagnet() && !record.magnet.startsWith(QLatin1String("magnet:?")))) {
            ++m_outcome.gone; // nothing we could add back
            continue;
        }
        if (!m_writer->add(TorrentsBackup::recordEntry(record.id),
                           QJsonDocument(record.toJson()).toJson(QJsonDocument::Indented),
                           now,
                           &error))
            return fail(error);
        if (!record.isMagnet() &&
            !m_writer->add(TorrentsBackup::torrentEntry(record.id), pending.torrent, now, &error))
            return fail(error);
        ++m_outcome.torrents;
        if (record.isMagnet())
            ++m_outcome.magnets;
    }
    Q_EMIT progress(m_next, int(m_ids.size()));
    nextBatch();
}

} // namespace omaseed::core
