#include "core/engines/transmission/TransmissionTorrentsExport.hpp"

#include "core/TorrentFile.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"
#include "core/engines/transmission/TransmissionTranslate.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace omaseed::core {

namespace TT = TransmissionTranslate;

namespace {

using Retry = TransmissionRpc::Retry;

/// tr_torrent_activity: 0 is stopped.
constexpr int kStatusStopped = 0;
/// seed_ratio_mode / seed_idle_mode: the session's · the torrent's own · none.
constexpr int kModeOwn = 1;
constexpr int kModeNone = 2;

QString text(const QJsonObject &json, const char *name)
{
    return json.value(QLatin1String(name)).toString();
}

qint64 integer(const QJsonObject &json, const char *name, qint64 fallback = 0)
{
    const QJsonValue value = json.value(QLatin1String(name));
    return value.isDouble() ? qint64(value.toDouble()) : fallback;
}

bool hasMetadata(const QJsonObject &json)
{
    return json.value(QLatin1String("metadata_percent_complete")).toDouble(1) >= 1;
}

QJsonObject torrentGet(const QStringList &fields, const QStringList &hashes = {})
{
    QJsonObject params{{QStringLiteral("fields"), QJsonArray::fromStringList(fields)},
                       {QStringLiteral("format"), QStringLiteral("table")}};
    if (!hashes.isEmpty())
        params.insert(QStringLiteral("ids"), QJsonArray::fromStringList(hashes));
    return params;
}

} // namespace

TransmissionTorrentsExport::TransmissionTorrentsExport(TransmissionEngine *engine, QObject *parent)
    : QObject(parent)
    , m_rpc(engine != nullptr ? engine->rpc() : nullptr)
{}

TransmissionTorrentsExport::~TransmissionTorrentsExport()
{
    if (m_file)
        m_file->cancelWriting();
}

void TransmissionTorrentsExport::setTorrentsFolder(const QString &folder)
{
    if (!m_busy)
        m_torrentsFolder = folder;
}

QString TransmissionTorrentsExport::localTorrentPath(const QString &torrentFile,
                                                     const QString &torrentsFolder)
{
    if (torrentFile.isEmpty() || torrentsFolder.isEmpty())
        return torrentFile;
    // The daemon's path may be another machine's: only its last part means something here.
    const QString name = torrentFile.section(QLatin1Char('/'), -1);
    return name.isEmpty() ? QString() : QDir(torrentsFolder).filePath(name);
}

QByteArray TransmissionTorrentsExport::readTorrent(const QString &path, const QString &hash)
{
    if (path.isEmpty() || hash.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    const TorrentFile parsed = TorrentFile::parse(bytes);
    if (!parsed.isValid() || parsed.infoHashV1 != hash.toLower())
        return {};
    return bytes;
}

QStringList TransmissionTorrentsExport::recordFields()
{
    return {QStringLiteral("hash_string"),
            QStringLiteral("name"),
            QStringLiteral("status"),
            QStringLiteral("metadata_percent_complete"),
            QStringLiteral("percent_done"),
            QStringLiteral("size_when_done"),
            QStringLiteral("download_dir"),
            QStringLiteral("labels"),
            QStringLiteral("download_limit"),
            QStringLiteral("download_limited"),
            QStringLiteral("upload_limit"),
            QStringLiteral("upload_limited"),
            QStringLiteral("seed_ratio_mode"),
            QStringLiteral("seed_ratio_limit"),
            QStringLiteral("seed_idle_mode"),
            QStringLiteral("seed_idle_limit"),
            QStringLiteral("sequential_download"),
            QStringLiteral("files"),
            QStringLiteral("file_stats"),
            QStringLiteral("trackers"),
            QStringLiteral("torrent_file"),
            QStringLiteral("magnet_link")};
}

TorrentRecord TransmissionTorrentsExport::recordFrom(const QJsonObject &json,
                                                     qint64 speedBytes,
                                                     int queuePosition,
                                                     const QByteArray &torrent)
{
    TorrentRecord r;
    // Transmission keys a torrent by its v1 info hash, a hybrid's too.
    r.id = text(json, "hash_string").toLower();
    r.infohashV1 = r.id;
    r.name = text(json, "name");
    r.size = std::max<qint64>(0, integer(json, "size_when_done"));
    r.progress = json.value(QLatin1String("percent_done")).toDouble();
    r.savePath = text(json, "download_dir");
    // C40 X4: no category; the labels are the tags, in the daemon's order.
    for (const QJsonValue &label : json.value(QLatin1String("labels")).toArray())
        r.tags.append(label.toString());
    r.state = integer(json, "status") == kStatusStopped ? QStringLiteral("stopped")
                                                        : QStringLiteral("started");
    r.queuePosition = std::max(0, queuePosition);
    if (speedBytes <= 0)
        speedBytes = 1000;
    if (json.value(QLatin1String("download_limited")).toBool())
        r.dlLimit = std::max<qint64>(0, integer(json, "download_limit")) * speedBytes;
    if (json.value(QLatin1String("upload_limited")).toBool())
        r.upLimit = std::max<qint64>(0, integer(json, "upload_limit")) * speedBytes;
    const qint64 ratioMode = integer(json, "seed_ratio_mode");
    if (ratioMode == kModeOwn)
        r.ratioLimit = json.value(QLatin1String("seed_ratio_limit")).toDouble();
    else if (ratioMode == kModeNone)
        r.ratioLimit = -1;
    const qint64 idleMode = integer(json, "seed_idle_mode");
    if (idleMode == kModeOwn)
        r.inactiveSeedingTimeLimit = integer(json, "seed_idle_limit");
    else if (idleMode == kModeNone)
        r.inactiveSeedingTimeLimit = -1;
    r.shareLimitAction = QStringLiteral("Default");
    r.sequential = json.value(QLatin1String("sequential_download")).toBool();

    // In tier order, whatever order the daemon listed them in.
    QJsonArray trackers = json.value(QLatin1String("trackers")).toArray();
    QList<QJsonObject> byTier;
    for (const QJsonValue &tracker : std::as_const(trackers))
        byTier.append(tracker.toObject());
    std::stable_sort(byTier.begin(), byTier.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return integer(a, "tier") < integer(b, "tier");
    });
    for (const QJsonObject &tracker : std::as_const(byTier)) {
        const QString url = text(tracker, "announce");
        if (!url.isEmpty() && !r.trackers.contains(url))
            r.trackers.append(url);
    }

    // The record's priorities are qBittorrent's (TorrentsBackup): not wanted 0, normal 1,
    // high 6. Low is 2 (FilePriority::Low), which qBittorrent has no place for: its importer
    // takes it as normal, a Transmission's as low again (C40 X6).
    QStringList names;
    const QJsonArray files = json.value(QLatin1String("files")).toArray();
    const QJsonArray stats = json.value(QLatin1String("file_stats")).toArray();
    QList<int> priorities;
    bool allNormal = true;
    for (qsizetype i = 0; i < files.size(); ++i) {
        names.append(files.at(i).toObject().value(QLatin1String("name")).toString());
        const QJsonObject stat = stats.at(i).toObject();
        const int priority = TT::filePriority(stat.value(QLatin1String("wanted")).toBool(true),
                                              int(integer(stat, "priority")));
        priorities.append(priority);
        allNormal = allNormal && priority == int(FilePriority::Normal);
    }
    if (!allNormal)
        r.priorities = priorities;

    const TorrentFile parsed = torrent.isEmpty() ? TorrentFile() : TorrentFile::parse(torrent);
    if (!parsed.isValid() || !hasMetadata(json)) {
        // C40 X3: a link, with everything above. Its renames can't be told without the
        // .torrent's own names.
        r.magnet = text(json, "magnet_link");
        return r;
    }
    // The id is qBittorrent's (TorrentFile::torrentIdFor), so a hybrid's is its v2 hash cut
    // short: what a qBittorrent will call it once it is added there.
    r.infohashV2 = parsed.infoHashV2;
    r.id = parsed.torrentId();
    if (!names.isEmpty()) {
        const auto [layout, renames] = TorrentsBackup::renamesOf(parsed, names);
        r.layout = layout;
        r.renamed = renames;
    }
    return r;
}

Reply<TransmissionTorrentsExport::Reach> *TransmissionTorrentsExport::reach(
    TransmissionEngine *engine, const QString &torrentsFolder, QObject *parent)
{
    if (parent == nullptr)
        parent = engine;
    if (engine == nullptr) {
        auto *reply = new Reply<Reach>(parent);
        QMetaObject::invokeMethod(
            reply,
            [reply] {
                reply->complete(
                    Result<Reach>(ApiError(ApiError::Kind::Unknown, QStringLiteral("no server"))));
            },
            Qt::QueuedConnection);
        return reply;
    }
    const QList<TransmissionRpc::Call> calls{
        {QStringLiteral("session_get"),
         {{QStringLiteral("fields"), QJsonArray{QStringLiteral("config_dir")}}}},
        {QStringLiteral("torrent_get"),
         torrentGet({QStringLiteral("hash_string"),
                     QStringLiteral("torrent_file"),
                     QStringLiteral("metadata_percent_complete")})}};
    using Results = QList<Result<QJsonObject>>;
    return mapReply<Reach>(
        engine->rpc()->batch(calls, Retry::OnTransient),
        parent,
        [torrentsFolder](const Results &results) -> Result<Reach> {
            for (const Result<QJsonObject> &result : results) {
                if (result.isError())
                    return result.error();
            }
            if (results.size() < 2)
                return ApiError(ApiError::Kind::Parse, QStringLiteral("torrent_get: no answer"));
            Reach reach;
            reach.daemonFolder = QDir(text(results.at(0).value(), "config_dir"))
                                     .filePath(QStringLiteral("torrents"));
            for (const QJsonObject &row : TT::rows(results.at(1).value())) {
                const QString file = text(row, "torrent_file");
                if (!hasMetadata(row) || file.isEmpty())
                    continue;
                if (reach.tried == 0 && file.contains(QLatin1Char('/')))
                    reach.daemonFolder = file.section(QLatin1Char('/'), 0, -2);
                ++reach.tried;
                if (readTorrent(localTorrentPath(file, torrentsFolder), text(row, "hash_string"))
                        .isEmpty())
                    ++reach.missing;
                if (reach.tried >= kReachSample)
                    break;
            }
            if (reach.tried > 0) {
                reach.readable = reach.missing == 0;
            } else {
                const QFileInfo folder(torrentsFolder.isEmpty() ? reach.daemonFolder
                                                                : torrentsFolder);
                reach.readable = folder.isDir() && folder.isReadable();
            }
            return reach;
        });
}

void TransmissionTorrentsExport::start(const QString &path,
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
    m_manifest.engine = EngineKind::Transmission; // DECISIONS C40 X1
    m_manifest.created = QDateTime::currentDateTimeUtc();
    m_manifest.appVersion = appVersion;
    m_manifest.serverName = serverName;
    m_manifest.serverUrl = serverUrl;
    m_hashes.clear();
    m_queue.clear();
    m_speedBytes = 1000;
    m_outcome = Outcome();
    m_outcome.path = path;
    m_next = 0;
    readSession();
}

void TransmissionTorrentsExport::cancel()
{
    if (!m_busy)
        return;
    Outcome outcome = m_outcome;
    outcome.cancelled = true;
    outcome.error = QStringLiteral("stopped");
    finish(outcome);
}

void TransmissionTorrentsExport::fail(const QString &error)
{
    Outcome outcome = m_outcome;
    outcome.error = error;
    finish(outcome);
}

void TransmissionTorrentsExport::finish(Outcome outcome)
{
    ++m_generation; // answers still on their way are dropped
    m_busy = false;
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

void TransmissionTorrentsExport::readSession()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    const QJsonObject params{{QStringLiteral("fields"),
                              QJsonArray{QStringLiteral("version"),
                                         QStringLiteral("download_dir"),
                                         QStringLiteral("incomplete_dir"),
                                         QStringLiteral("incomplete_dir_enabled"),
                                         QStringLiteral("units")}}};
    m_rpc->call(QStringLiteral("session_get"), params, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonObject> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            // "4.1.3 (838877323f)": the number is the version, the rest its build.
            m_manifest.version = text(r.value(), "version").section(QLatin1Char(' '), 0, 0);
            m_manifest.savePath = text(r.value(), "download_dir");
            const TT::Session session = TT::session(r.value());
            m_manifest.downloadPath = session.incompleteDir;
            m_speedBytes = session.speedBytes;
            readList();
        });
}

void TransmissionTorrentsExport::readList()
{
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const int generation = m_generation;
    const QJsonObject params = torrentGet({QStringLiteral("hash_string"),
                                           QStringLiteral("labels"),
                                           QStringLiteral("queue_position"),
                                           QStringLiteral("left_until_done"),
                                           QStringLiteral("metadata_percent_complete"),
                                           QStringLiteral("torrent_file")});
    m_rpc->call(QStringLiteral("torrent_get"), params, Retry::OnTransient)
        ->then(this, [this, generation](const Result<QJsonObject> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            QList<QPair<qint64, QString>> unfinished;
            for (const QJsonObject &row : TT::rows(r.value())) {
                const QString hash = text(row, "hash_string").toLower();
                if (hash.isEmpty())
                    continue;
                m_hashes.append(hash);
                for (const QJsonValue &label : row.value(QLatin1String("labels")).toArray()) {
                    if (!m_manifest.tags.contains(label.toString()))
                        m_manifest.tags.append(label.toString());
                }
                const bool metadata = hasMetadata(row);
                if (!metadata || integer(row, "left_until_done") > 0)
                    unfinished.append({integer(row, "queue_position"), hash});
                // What the manifest says before the torrents are read: a link for want of
                // metadata, or of a file to open. One that turns out to be another torrent's
                // is only known when it is read, and counted in the outcome.
                const QFileInfo file(localTorrentPath(text(row, "torrent_file"), m_torrentsFolder));
                if (!metadata || !file.isFile() || !file.isReadable())
                    ++m_manifest.magnets;
            }
            m_manifest.tags.sort(Qt::CaseInsensitive);
            m_manifest.count = int(m_hashes.size());
            // qBittorrent's queue: the unfinished ones, from 1.
            std::stable_sort(unfinished.begin(),
                             unfinished.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            for (qsizetype i = 0; i < unfinished.size(); ++i)
                m_queue.insert(unfinished.at(i).second, int(i) + 1);
            open();
        });
}

void TransmissionTorrentsExport::open()
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

void TransmissionTorrentsExport::nextBatch()
{
    if (!m_busy) // stopped from a progress handler
        return;
    if (m_next >= m_hashes.size()) {
        QString error;
        if (!m_writer->finish(&error))
            return fail(error);
        Outcome outcome = m_outcome;
        outcome.ok = true;
        return finish(outcome);
    }
    if (m_rpc.isNull())
        return fail(QStringLiteral("no server"));
    const QStringList hashes = m_hashes.mid(m_next, kBatch);
    m_next += int(hashes.size());
    const int generation = m_generation;
    m_rpc
        ->call(
            QStringLiteral("torrent_get"), torrentGet(recordFields(), hashes), Retry::OnTransient)
        ->then(this, [this, generation, hashes](const Result<QJsonObject> &r) {
            if (!m_busy || generation != m_generation)
                return;
            if (r.isError())
                return fail(r.error().toString());
            // In the order asked; one the daemon no longer has is not among them.
            QHash<QString, QJsonObject> byHash;
            for (const QJsonObject &row : TT::rows(r.value()))
                byHash.insert(text(row, "hash_string").toLower(), row);
            QList<QJsonObject> rows;
            for (const QString &hash : hashes) {
                if (byHash.contains(hash))
                    rows.append(byHash.value(hash));
                else
                    ++m_outcome.gone;
            }
            writeBatch(rows);
        });
}

void TransmissionTorrentsExport::writeBatch(const QList<QJsonObject> &rows)
{
    const qint64 now = m_manifest.created.toSecsSinceEpoch();
    QString error;
    for (const QJsonObject &row : rows) {
        const QString hash = text(row, "hash_string").toLower();
        // C40 X3: where the daemon says it is, or under the folder given.
        const QByteArray torrent =
            hasMetadata(row)
                ? readTorrent(localTorrentPath(text(row, "torrent_file"), m_torrentsFolder), hash)
                : QByteArray();
        const TorrentRecord record = recordFrom(row, m_speedBytes, m_queue.value(hash), torrent);
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
            !m_writer->add(TorrentsBackup::torrentEntry(record.id), torrent, now, &error))
            return fail(error);
        ++m_outcome.torrents;
        if (record.isMagnet())
            ++m_outcome.magnets;
    }
    Q_EMIT progress(m_next, int(m_hashes.size()));
    nextBatch();
}

} // namespace omaseed::core
