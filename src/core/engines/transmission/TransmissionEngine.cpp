#include "core/engines/transmission/TransmissionEngine.hpp"

#include "core/engines/EngineKind.hpp"
#include "core/engines/transmission/TransmissionFingerprint.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"

#include <QJsonArray>
#include <QPointer>
#include <QSet>

#include <chrono>

namespace omaseed::core {

namespace TT = TransmissionTranslate;
using Retry = TransmissionRpc::Retry;

namespace {

QJsonArray idsOf(const QStringList &hashes)
{
    QJsonArray ids;
    for (const QString &hash : hashes)
        ids.append(hash);
    return ids;
}

QJsonArray fieldsOf(const QStringList &fields)
{
    return QJsonArray::fromStringList(fields);
}

QJsonArray numbers(const QList<int> &values)
{
    QJsonArray array;
    for (const int value : values)
        array.append(value);
    return array;
}

QStringList labelsOf(const QJsonObject &torrent)
{
    QStringList labels;
    for (const QJsonValue &label : torrent.value(QStringLiteral("labels")).toArray())
        labels.append(label.toString());
    return labels;
}

/// Transmission refuses a label with a comma (INVALID_PARAMS) and keeps no empty one
/// (engine matrix §4.6, C25 K14): said before anything is sent.
QString badLabel(const QStringList &labels)
{
    for (const QString &label : labels) {
        if (label.trimmed().isEmpty())
            return QStringLiteral("a label can't be empty");
        if (label.contains(QLatin1Char(',')))
            return QStringLiteral("Transmission's labels can't contain commas: %1").arg(label);
    }
    return {};
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

QString lastOf(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

/// rpc-spec §3.3 `status` 0: what stoppedAmong() and running() go by (C20, C36 M10). A
/// local error stops the torrent, so an errored one is stopped too.
bool isStopped(const QJsonObject &torrent)
{
    return torrent.value(QStringLiteral("status")).toInt(-1) == 0;
}

} // namespace

TransmissionEngine::TransmissionEngine(QObject *parent)
    : EngineClient(parent)
    , m_rpc(new TransmissionRpc(this))
    , m_now([]() {
        return qint64(std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count());
    })
{}

TransmissionEngine::~TransmissionEngine() = default;

void TransmissionEngine::setClock(std::function<qint64()> msNow)
{
    m_now = std::move(msNow);
}

void TransmissionEngine::forget()
{
    dropMoves();
    m_differ.clear();
    m_hashOf.clear();
    m_cursor = 0;
    m_lastPollAt.reset();
    m_session = {};
    m_downloadDir.clear();
}

void TransmissionEngine::dropMoves()
{
    if (m_moves.isEmpty())
        return;
    m_moves.clear();
    Q_EMIT busyMovingChanged();
}

// --- small helpers --------------------------------------------------------------------------

template<class T>
Reply<T> *TransmissionEngine::settled(Result<T> result)
{
    auto *reply = new Reply<T>(this);
    QTimer::singleShot(0, reply, [reply, result]() { reply->complete(result); });
    return reply;
}

Reply<Done> *TransmissionEngine::refused(const QString &why)
{
    return settled<Done>(ApiError(ApiError::Kind::Conflict, why));
}

Reply<Done> *TransmissionEngine::done(Reply<QJsonObject> *reply)
{
    return mapReply<Done>(reply, this, [](const QJsonObject &) { return Done{}; });
}

Reply<Done> *TransmissionEngine::doneAll(Reply<Results> *reply)
{
    return mapReply<Done>(reply, this, [](const Results &results) {
        for (const Result<QJsonObject> &result : results) {
            if (result.isError())
                return Result<Done>(result.error());
        }
        return Result<Done>(Done{});
    });
}

Reply<Done> *TransmissionEngine::forTorrents(const QString &method,
                                             const QStringList &hashes,
                                             QJsonObject params)
{
    if (hashes.isEmpty())
        return doneNow(); // an empty `ids` would be every torrent
    params.insert(QStringLiteral("ids"), idsOf(hashes));
    return done(m_rpc->call(method, params));
}

Reply<QList<QJsonObject>> *TransmissionEngine::torrentRows(const QStringList &hashes,
                                                           const QStringList &fields)
{
    QJsonObject params{{QStringLiteral("fields"), fieldsOf(fields)},
                       {QStringLiteral("format"), QStringLiteral("table")}};
    if (!hashes.isEmpty())
        params.insert(QStringLiteral("ids"), idsOf(hashes));
    return mapReply<QList<QJsonObject>>(
        m_rpc->call(QStringLiteral("torrent_get"), params, Retry::OnTransient),
        this,
        [](const QJsonObject &result) { return TT::rows(result); });
}

Reply<QJsonObject> *TransmissionEngine::oneTorrent(const QString &hash, const QStringList &fields)
{
    if (hash.isEmpty())
        return settled<QJsonObject>(
            ApiError(ApiError::Kind::NotFound, QStringLiteral("torrent_get: no torrent given")));
    return mapReply<QJsonObject>(
        torrentRows({hash}, fields), this, [hash](const QList<QJsonObject> &rows) {
            if (rows.isEmpty())
                return Result<QJsonObject>(
                    ApiError(ApiError::Kind::NotFound,
                             QStringLiteral("torrent_get: no torrent %1").arg(hash)));
            return Result<QJsonObject>(rows.first());
        });
}

template<class T, class U>
Reply<U> *TransmissionEngine::then(Reply<T> *first, std::function<Reply<U> *(const T &)> next)
{
    auto *out = new Reply<U>(this);
    auto current = std::make_shared<QPointer<ReplyBase>>(first);
    connect(out, &ReplyBase::abortRequested, out, [current]() {
        if (!current->isNull())
            (*current)->abort();
    });
    first->then(out, [out, next, current](const Result<T> &result) {
        if (result.isError()) {
            out->complete(Result<U>(result.error()));
            return;
        }
        Reply<U> *second = next(result.value());
        *current = second;
        second->then(out, [out](const Result<U> &answer) { out->complete(answer); });
    });
    return out;
}

// --- the connection -------------------------------------------------------------------------

QUrl TransmissionEngine::baseUrl() const
{
    return m_rpc->baseUrl();
}

void TransmissionEngine::setBaseUrl(const QUrl &baseUrl)
{
    if (baseUrl != m_rpc->baseUrl())
        forget(); // another server: its ids and its snapshot are not this one's
    m_rpc->setBaseUrl(baseUrl);
}

void TransmissionEngine::setApiKey(const QString &)
{
    // Transmission signs in with a login or nothing (rpc-spec §2.2.3).
}

bool TransmissionEngine::hasApiKey() const
{
    return false;
}

void TransmissionEngine::setCredentials(const QString &username, const QString &password)
{
    m_rpc->setCredentials(username, password);
}

void TransmissionEngine::clearAuth()
{
    m_rpc->clearAuth();
    forget();
}

void TransmissionEngine::allowLoginAgain()
{
    m_rpc->allowLoginAgain();
}

void TransmissionEngine::setPinnedCertificate(const QString &sha256)
{
    m_rpc->setPinnedCertificate(sha256);
}

void TransmissionEngine::setTimeout(std::chrono::milliseconds timeout)
{
    m_rpc->setTimeout(timeout);
}

int TransmissionEngine::maxRetries() const
{
    return m_rpc->maxRetries();
}

void TransmissionEngine::setMaxRetries(int retries)
{
    m_rpc->setMaxRetries(retries);
}

void TransmissionEngine::cancelAll()
{
    m_rpc->cancelAll();
    dropMoves();
}

QUrl TransmissionEngine::reachableUrl(const QUrl &base) const
{
    // The RPC answers 409 (or 401) to anyone: up there means Transmission is.
    return TransmissionFingerprint::rpcUrl(base);
}

// --- what the server is ---------------------------------------------------------------------

Reply<ServerInfo> *TransmissionEngine::probe()
{
    return mapReply<ServerInfo>(
        m_rpc->post(TransmissionFingerprint::sessionGetBody(),
                    QStringLiteral("session_get"),
                    Retry::OnTransient),
        this,
        [this](const HttpAnswer &answer) {
            const TransmissionFingerprint::Reading reading = TransmissionFingerprint::read(answer);
            if (reading.fingerprint != Fingerprint::Itself)
                return Result<ServerInfo>(
                    ApiError(ApiError::Kind::Parse,
                             QStringLiteral("session_get: this address doesn't answer as "
                                            "Transmission")));
            ServerInfo info;
            info.version = reading.version;
            info.apiVersion =
                reading.rpcVersion.isEmpty() ? m_rpc->rpcVersionHeader() : reading.rpcVersion;
            // K8: below RPC 6.0.0 (Transmission 4.1) refused, in EngineKind's words.
            const EngineSupport support =
                engineSupport(EngineKind::Transmission, info.version, info.apiVersion);
            if (support.versionRefused)
                return Result<ServerInfo>(ApiError(ApiError::Kind::Conflict, support.refusal));
            return Result<ServerInfo>(info);
        });
}

EngineFeatures TransmissionEngine::features(const ServerInfo &info) const
{
    return TT::features(info);
}

// --- sync -----------------------------------------------------------------------------------

QList<SnapshotDiffer::Entry> TransmissionEngine::entries(const QList<QJsonObject> &rows)
{
    QList<SnapshotDiffer::Entry> list;
    list.reserve(rows.size());
    for (const QJsonObject &row : rows) {
        SnapshotDiffer::Entry entry;
        entry.patch = TT::torrent(row, m_session);
        if (entry.patch.hash.isEmpty())
            continue;
        if (row.contains(QStringLiteral("id")))
            m_hashOf.insert(qint64(row.value(QStringLiteral("id")).toDouble()), entry.patch.hash);
        entry.trackers = TT::trackerUrls(row);
        list.append(entry);
    }
    return list;
}

Reply<SyncDelta> *TransmissionEngine::sync(int cursor)
{
    const qint64 askedAt = m_now();
    // A cursor this engine didn't hand out last (0, or the models of another server): all
    // of it, as a full delta. A gap past what recently_active covers: all of it, diffed.
    const bool full = cursor <= 0 || cursor != m_cursor;
    const bool everything = full || !m_lastPollAt || askedAt - *m_lastPollAt > kFullListGapMs;

    QJsonObject get{{QStringLiteral("fields"), fieldsOf(TT::listFields())},
                    {QStringLiteral("format"), QStringLiteral("table")}};
    if (!everything)
        get.insert(QStringLiteral("ids"), QStringLiteral("recently_active"));
    QList<TransmissionRpc::Call> calls{
        {QStringLiteral("torrent_get"), get},
        {QStringLiteral("session_stats"), {}},
        {QStringLiteral("session_get"),
         {{QStringLiteral("fields"), fieldsOf(TT::sessionFields())}}}};
    if (!m_downloadDir.isEmpty())
        calls.append({QStringLiteral("free_space"), {{QStringLiteral("path"), m_downloadDir}}});

    return mapReply<SyncDelta>(
        m_rpc->batch(calls, Retry::OnTransient),
        this,
        [this, full, everything, askedAt](const Results &results) {
            if (results.value(0).isError())
                return Result<SyncDelta>(results.value(0).error());
            const QJsonObject sessionGet =
                results.value(2).isError() ? QJsonObject() : results.value(2).value();
            if (!sessionGet.isEmpty()) {
                m_session = TT::session(sessionGet);
                m_downloadDir = sessionGet.value(QStringLiteral("download_dir")).toString();
            }

            const QJsonObject got = results.value(0).value();
            const QList<SnapshotDiffer::Entry> listed = entries(TT::rows(got));
            SyncDelta delta;
            if (everything) {
                delta = m_differ.everything(listed, full);
                // The ids of torrents no longer listed are gone for good.
                const QStringList known = m_differ.hashes();
                for (auto it = m_hashOf.begin(); it != m_hashOf.end();)
                    it = known.contains(it.value()) ? std::next(it) : m_hashOf.erase(it);
            } else {
                QStringList removed;
                for (const QJsonValue &id : got.value(QStringLiteral("removed")).toArray()) {
                    const QString hash = m_hashOf.take(qint64(id.toDouble()));
                    if (!hash.isEmpty())
                        removed.append(hash);
                }
                delta = m_differ.some(listed, removed);
            }

            const QJsonObject stats =
                results.value(1).isError() ? QJsonObject() : results.value(1).value();
            std::optional<QJsonObject> freeSpace;
            if (results.size() > 3 && results.at(3).isValid())
                freeSpace = results.at(3).value();
            if (!stats.isEmpty() || !sessionGet.isEmpty())
                delta.serverState = TT::serverState(stats, sessionGet, freeSpace);

            m_lastPollAt = askedAt;
            m_cursor = m_cursor >= std::numeric_limits<int>::max() - 1 ? 1 : m_cursor + 1;
            delta.cursor = m_cursor;
            return Result<SyncDelta>(delta);
        });
}

Reply<QList<TorrentPatch>> *TransmissionEngine::torrents(const QStringList &hashes)
{
    return mapReply<QList<TorrentPatch>>(
        torrentRows(hashes, TT::listFields()), this, [this](const QList<QJsonObject> &rows) {
            QList<TorrentPatch> patches;
            patches.reserve(rows.size());
            for (const QJsonObject &row : rows)
                patches.append(TT::torrent(row, m_session));
            return patches;
        });
}

Reply<QStringList> *TransmissionEngine::stoppedAmong(const QStringList &hashes)
{
    return mapReply<QStringList>(
        torrentRows(hashes, {QStringLiteral("hash_string"), QStringLiteral("status")}),
        this,
        [](const QList<QJsonObject> &rows) {
            QStringList stopped;
            for (const QJsonObject &row : rows) {
                if (isStopped(row))
                    stopped.append(row.value(QStringLiteral("hash_string")).toString().toLower());
            }
            return stopped;
        });
}

Reply<QList<RunningTorrent>> *TransmissionEngine::running()
{
    return mapReply<QList<RunningTorrent>>(
        torrentRows({}, {QStringLiteral("hash_string"), QStringLiteral("status")}),
        this,
        [](const QList<QJsonObject> &rows) {
            QList<RunningTorrent> list;
            for (const QJsonObject &row : rows) {
                // Transmission reports no forced mark (engine matrix §4.5): never forced.
                if (!isStopped(row))
                    list.append(
                        {row.value(QStringLiteral("hash_string")).toString().toLower(), false});
            }
            return list;
        });
}

Reply<QList<CategoryPatch>> *TransmissionEngine::categories()
{
    // No categories (C25 K16): an honest empty list rather than an error.
    return settled<QList<CategoryPatch>>(QList<CategoryPatch>());
}

Reply<QStringList> *TransmissionEngine::tags()
{
    return mapReply<QStringList>(
        torrentRows({}, {QStringLiteral("labels")}), this, [](const QList<QJsonObject> &rows) {
            QStringList labels;
            for (const QJsonObject &row : rows) {
                for (const QString &label : labelsOf(row)) {
                    if (!labels.contains(label))
                        labels.append(label);
                }
            }
            labels.sort();
            return labels;
        });
}

// --- details --------------------------------------------------------------------------------

Reply<TorrentProperties> *TransmissionEngine::properties(const QString &hash)
{
    return mapReply<TorrentProperties>(
        oneTorrent(hash, TT::propertiesFields()), this, [](const QJsonObject &row) {
            return TT::properties(row, QDateTime::currentSecsSinceEpoch());
        });
}

Reply<QList<ContentFile>> *TransmissionEngine::files(const QString &hash)
{
    return mapReply<QList<ContentFile>>(oneTorrent(hash, TT::filesFields()), this, TT::files);
}

Reply<QList<TrackerEntry>> *TransmissionEngine::trackers(const QString &hash)
{
    return mapReply<QList<TrackerEntry>>(
        oneTorrent(hash, TT::trackersFields()), this, TT::trackers);
}

Reply<PeersDelta> *TransmissionEngine::peers(const QString &hash, int)
{
    // Transmission has no peer deltas: the whole list every time (engine matrix §4.8).
    return mapReply<PeersDelta>(oneTorrent(hash, TT::peersFields()), this, TT::peers);
}

Reply<QStringList> *TransmissionEngine::webSeeds(const QString &hash)
{
    return mapReply<QStringList>(oneTorrent(hash, TT::webSeedsFields()), this, TT::webSeeds);
}

Reply<QByteArray> *TransmissionEngine::pieceStates(const QString &hash)
{
    return mapReply<QByteArray>(oneTorrent(hash, TT::piecesFields()), this, TT::pieceStates);
}

// --- actions --------------------------------------------------------------------------------

Reply<Done> *TransmissionEngine::start(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("torrent_start"), hashes);
}

Reply<Done> *TransmissionEngine::stop(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("torrent_stop"), hashes);
}

Reply<Done> *TransmissionEngine::startAll()
{
    return done(m_rpc->call(QStringLiteral("torrent_start"))); // no ids: every torrent
}

Reply<Done> *TransmissionEngine::stopAll()
{
    return done(m_rpc->call(QStringLiteral("torrent_stop")));
}

Reply<Done> *TransmissionEngine::setForceStart(const QStringList &hashes, bool on)
{
    // torrent_start_now starts past the queue; there is no "stop forcing", so off starts it
    // the ordinary way (a torrent already running is left as it is).
    return forTorrents(on ? QStringLiteral("torrent_start_now") : QStringLiteral("torrent_start"),
                       hashes);
}

Reply<Done> *TransmissionEngine::recheck(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("torrent_verify"), hashes);
}

Reply<Done> *TransmissionEngine::reannounce(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("torrent_reannounce"), hashes);
}

Reply<Done> *
TransmissionEngine::renamePath(const QString &hash, const QString &oldPath, const QString &newPath)
{
    if (hash.isEmpty())
        return refused(QStringLiteral("torrent_rename_path: no torrent given"));
    // torrent_rename_path gives the last part of a path a new name; it can't move it to
    // another folder (rpc-spec §3.7).
    if (parentOf(oldPath) != parentOf(newPath))
        return refused(QStringLiteral("Transmission renames in place: a new name, not a new "
                                      "folder (%1 → %2)")
                           .arg(oldPath, newPath));
    if (oldPath == newPath)
        return doneNow();
    return done(m_rpc->call(QStringLiteral("torrent_rename_path"),
                            {{QStringLiteral("ids"), idsOf({hash})},
                             {QStringLiteral("path"), oldPath},
                             {QStringLiteral("name"), lastOf(newPath)}}));
}

Reply<Done> *TransmissionEngine::rename(const QString &hash, const QString &name)
{
    // The torrent's name is its top folder (or its one file) on disk: renaming it renames
    // that (C25 K28). Its current name is read first.
    return then<QJsonObject, Done>(
        oneTorrent(hash, {QStringLiteral("name")}), [this, hash, name](const QJsonObject &row) {
            return renamePath(hash, row.value(QStringLiteral("name")).toString(), name);
        });
}

Reply<Done> *TransmissionEngine::setLocation(const QStringList &hashes, const QString &path)
{
    if (hashes.isEmpty())
        return doneNow(); // an empty `ids` would be every torrent
    // K22 (board 47 §3, checked on 4.1.3): Transmission answers nothing else, this call
    // included, until the move's copy ends. Its own timeout is long; busyMoving() tells the
    // poll that a silence meanwhile is the move, not a lost server.
    const quint64 serial = ++m_moveSerial;
    m_moves.append({serial, hashes});
    Q_EMIT busyMovingChanged();
    const QJsonObject params{{QStringLiteral("ids"), idsOf(hashes)},
                             {QStringLiteral("location"), path},
                             {QStringLiteral("move"), true}};
    Reply<QJsonObject> *sent = m_rpc->call(QStringLiteral("torrent_set_location"),
                                           params,
                                           Retry::No,
                                           std::chrono::milliseconds(kMoveTimeout));
    QPointer<TransmissionEngine> self(this);
    sent->then(this, [self, serial](const Result<QJsonObject> &) {
        if (self.isNull())
            return;
        const auto mine = [serial](const QPair<quint64, QStringList> &move) {
            return move.first == serial;
        };
        if (self->m_moves.removeIf(mine) > 0) {
            // The moved torrent may be quiet now, out of recently_active: the next poll lists
            // every torrent, so its new folder (or its error) is read whatever it did.
            self->m_lastPollAt.reset();
            Q_EMIT self->busyMovingChanged();
        }
    });
    return done(sent);
}

QStringList TransmissionEngine::busyMoving() const
{
    QStringList hashes;
    for (const auto &move : m_moves)
        hashes << move.second;
    return hashes;
}

void TransmissionEngine::releaseBusyMoving()
{
    dropMoves();
}

Reply<FreeSpaceAt> *TransmissionEngine::freeSpace(const QString &path)
{
    auto *reply = new Reply<FreeSpaceAt>(this);
    if (!path.startsWith(QLatin1Char('/'))) {
        QTimer::singleShot(0, reply, [reply, path]() {
            reply->complete(Result<FreeSpaceAt>(ApiError(
                ApiError::Kind::Conflict, QStringLiteral("free_space: %1 isn't absolute").arg(path))));
        });
        return reply;
    }
    freeSpaceUp(path, reply, 0);
    return reply;
}

void TransmissionEngine::freeSpaceUp(const QString &path, Reply<FreeSpaceAt> *reply, int depth)
{
    QPointer<Reply<FreeSpaceAt>> to(reply);
    m_rpc->call(QStringLiteral("free_space"), {{QStringLiteral("path"), path}}, Retry::OnTransient)
        ->then(this, [this, to, path, depth](const Result<QJsonObject> &result) {
            if (to.isNull())
                return;
            if (result.isError()) {
                // A folder that isn't there yet (the move makes it) is read on its parent; a
                // daemon that didn't answer is an error.
                QString parent = path;
                while (parent.size() > 1 && parent.endsWith(QLatin1Char('/')))
                    parent.chop(1);
                parent = parent.left(std::max<qsizetype>(1, parent.lastIndexOf(QLatin1Char('/'))));
                if (result.error().kind() == ApiError::Kind::Conflict && parent != path &&
                    depth < 32) {
                    freeSpaceUp(parent, to.data(), depth + 1);
                    return;
                }
                to->complete(Result<FreeSpaceAt>(result.error()));
                return;
            }
            const QJsonObject answer = result.value();
            FreeSpaceAt at;
            at.path = answer.value(QStringLiteral("path")).toString(path);
            at.bytesFree = qint64(answer.value(QStringLiteral("size_bytes")).toDouble(-1));
            at.bytesTotal =
                std::max<qint64>(0, qint64(answer.value(QStringLiteral("total_size")).toDouble()));
            to->complete(Result<FreeSpaceAt>(at));
        });
}

Reply<Done> *
TransmissionEngine::renameFile(const QString &hash, const QString &oldPath, const QString &newPath)
{
    return renamePath(hash, oldPath, newPath);
}

Reply<Done> *TransmissionEngine::renameFolder(const QString &hash,
                                              const QString &oldPath,
                                              const QString &newPath)
{
    return renamePath(hash, oldPath, newPath);
}

Reply<Done> *TransmissionEngine::setFilePriority(const QString &hash,
                                                 const QList<int> &fileIds,
                                                 FilePriority priority)
{
    if (fileIds.isEmpty())
        return doneNow(); // an empty list is "every file" to Transmission
    QJsonObject params;
    const QJsonArray ids = numbers(fileIds);
    switch (priority) {
    case FilePriority::Skip:
        params.insert(QStringLiteral("files_unwanted"), ids);
        break;
    case FilePriority::Low:
        params.insert(QStringLiteral("files_wanted"), ids);
        params.insert(QStringLiteral("priority_low"), ids);
        break;
    case FilePriority::High:
    case FilePriority::Maximal: // Transmission's highest is high (C25 K17)
        params.insert(QStringLiteral("files_wanted"), ids);
        params.insert(QStringLiteral("priority_high"), ids);
        break;
    case FilePriority::Normal:
    case FilePriority::Mixed:
        params.insert(QStringLiteral("files_wanted"), ids);
        params.insert(QStringLiteral("priority_normal"), ids);
        break;
    }
    return forTorrents(QStringLiteral("torrent_set"), {hash}, params);
}

Reply<Done> *TransmissionEngine::queueUp(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("queue_move_up"), hashes);
}

Reply<Done> *TransmissionEngine::queueDown(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("queue_move_down"), hashes);
}

Reply<Done> *TransmissionEngine::toggleSequentialDownload(const QStringList &hashes)
{
    if (hashes.isEmpty())
        return doneNow();
    // Transmission sets a value, it doesn't flip one: read each, then send the opposite.
    return then<QList<QJsonObject>, Done>(
        torrentRows(hashes, {QStringLiteral("hash_string"), QStringLiteral("sequential_download")}),
        [this](const QList<QJsonObject> &rows) {
            QStringList on;
            QStringList off;
            for (const QJsonObject &row : rows) {
                const QString hash = row.value(QStringLiteral("hash_string")).toString();
                (row.value(QStringLiteral("sequential_download")).toBool() ? off : on).append(hash);
            }
            QList<TransmissionRpc::Call> calls;
            if (!on.isEmpty())
                calls.append({QStringLiteral("torrent_set"),
                              {{QStringLiteral("ids"), idsOf(on)},
                               {QStringLiteral("sequential_download"), true}}});
            if (!off.isEmpty())
                calls.append({QStringLiteral("torrent_set"),
                              {{QStringLiteral("ids"), idsOf(off)},
                               {QStringLiteral("sequential_download"), false}}});
            if (calls.isEmpty())
                return doneNow();
            return doneAll(m_rpc->batch(calls));
        });
}

Reply<Done> *TransmissionEngine::toggleFirstLastPiece(const QStringList &)
{
    return unsupported<Done>(QStringLiteral("first and last pieces first")); // C25 I
}

Reply<Done> *TransmissionEngine::setSuperSeeding(const QStringList &, bool)
{
    return unsupported<Done>(QStringLiteral("super seeding")); // C25 I
}

Reply<Done> *TransmissionEngine::setAutoManagement(const QStringList &, bool)
{
    return unsupported<Done>(QStringLiteral("automatic torrent management")); // C25 I
}

Reply<Done> *TransmissionEngine::remove(const QStringList &hashes, bool deleteFiles)
{
    return forTorrents(QStringLiteral("torrent_remove"),
                       hashes,
                       {{QStringLiteral("delete_local_data"), deleteFiles}});
}

Reply<Done> *TransmissionEngine::addTrackers(const QString &hash, const QStringList &urls)
{
    if (urls.isEmpty())
        return doneNow();
    // tracker_list is replaced whole: read it, add, write it back (engine matrix §4.8).
    return then<QJsonObject, Done>(
        oneTorrent(hash, {QStringLiteral("tracker_list")}),
        [this, hash, urls](const QJsonObject &row) {
            const QString before = row.value(QStringLiteral("tracker_list")).toString();
            const QString after = TT::trackerListWith(before, urls);
            if (after == before)
                return doneNow();
            return forTorrents(
                QStringLiteral("torrent_set"), {hash}, {{QStringLiteral("tracker_list"), after}});
        });
}

Reply<Done> *TransmissionEngine::removeTrackers(const QString &hash, const QStringList &urls)
{
    if (urls.isEmpty())
        return doneNow();
    return then<QJsonObject, Done>(
        oneTorrent(hash, {QStringLiteral("tracker_list")}),
        [this, hash, urls](const QJsonObject &row) {
            const QString before = row.value(QStringLiteral("tracker_list")).toString();
            const QString after = TT::trackerListWithout(before, urls);
            if (after == before)
                return doneNow();
            return forTorrents(
                QStringLiteral("torrent_set"), {hash}, {{QStringLiteral("tracker_list"), after}});
        });
}

Reply<Done> *TransmissionEngine::addWebSeeds(const QString &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("editing web seeds")); // C25 H, K29: read only
}

Reply<Done> *TransmissionEngine::removeWebSeeds(const QString &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("editing web seeds"));
}

Reply<Done> *TransmissionEngine::addPeers(const QStringList &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("adding peers")); // C25 H
}

Reply<Done> *TransmissionEngine::banPeers(const QStringList &)
{
    return unsupported<Done>(QStringLiteral("banning peers")); // C25 H
}

// --- labels ---------------------------------------------------------------------------------

Reply<Done> *TransmissionEngine::setCategory(const QStringList &, const QString &)
{
    return unsupported<Done>(QStringLiteral("categories")); // C25 K16
}

Reply<Done> *TransmissionEngine::createCategory(const QString &, const QString &)
{
    return unsupported<Done>(QStringLiteral("categories"));
}

Reply<Done> *TransmissionEngine::removeCategories(const QStringList &)
{
    return unsupported<Done>(QStringLiteral("categories"));
}

Reply<Done> *TransmissionEngine::relabel(const QStringList &hashes,
                                         std::function<QStringList(const QStringList &)> change)
{
    return then<QList<QJsonObject>, Done>(
        torrentRows(hashes, {QStringLiteral("hash_string"), QStringLiteral("labels")}),
        [this, change](const QList<QJsonObject> &rows) {
            // labels are replaced whole: one torrent_set per resulting set of labels.
            QList<QStringList> sets;
            QList<QStringList> torrentsOf;
            for (const QJsonObject &row : rows) {
                const QStringList before = labelsOf(row);
                const QStringList after = change(before);
                if (after == before)
                    continue;
                const qsizetype at = sets.indexOf(after);
                const QString hash = row.value(QStringLiteral("hash_string")).toString();
                if (at < 0) {
                    sets.append(after);
                    torrentsOf.append(QStringList{hash});
                } else {
                    torrentsOf[at].append(hash);
                }
            }
            if (sets.isEmpty())
                return doneNow();
            QList<TransmissionRpc::Call> calls;
            for (qsizetype i = 0; i < sets.size(); ++i)
                calls.append(
                    {QStringLiteral("torrent_set"),
                     {{QStringLiteral("ids"), idsOf(torrentsOf.at(i))},
                      {QStringLiteral("labels"), QJsonArray::fromStringList(sets.at(i))}}});
            return doneAll(m_rpc->batch(calls));
        });
}

Reply<Done> *TransmissionEngine::addTags(const QStringList &hashes, const QStringList &tags)
{
    if (hashes.isEmpty() || tags.isEmpty())
        return doneNow();
    if (const QString why = badLabel(tags); !why.isEmpty())
        return refused(why);
    return relabel(hashes, [tags](const QStringList &labels) {
        QStringList after = labels;
        for (const QString &tag : tags) {
            if (!after.contains(tag))
                after.append(tag);
        }
        return after;
    });
}

Reply<Done> *TransmissionEngine::removeTags(const QStringList &hashes, const QStringList &tags)
{
    if (hashes.isEmpty() || tags.isEmpty())
        return doneNow();
    return relabel(hashes, [tags](const QStringList &labels) {
        QStringList after = labels;
        for (const QString &tag : tags)
            after.removeAll(tag);
        return after;
    });
}

Reply<Done> *TransmissionEngine::createTags(const QStringList &)
{
    // A label exists only on torrents: "new label…" is offered only while applying it
    // (C25 K14), which is addTags.
    return unsupported<Done>(QStringLiteral("a label on no torrent"));
}

Reply<Done> *TransmissionEngine::deleteTags(const QStringList &tags)
{
    if (tags.isEmpty())
        return doneNow();
    // Deleting a label is taking it off every torrent that has it.
    return relabel({}, [tags](const QStringList &labels) {
        QStringList after = labels;
        for (const QString &tag : tags)
            after.removeAll(tag);
        return after;
    });
}

// --- limits ---------------------------------------------------------------------------------

Reply<Done> *
TransmissionEngine::speedLimit(const QStringList &hashes, qint64 bytesPerSec, const char *which)
{
    const QString name = QString::fromLatin1(which);
    QJsonObject params{{name + QStringLiteral("_limited"), bytesPerSec > 0}};
    if (bytesPerSec > 0)
        params.insert(name + QStringLiteral("_limit"),
                      TT::speedUnits(bytesPerSec, m_session.speedBytes));
    return forTorrents(QStringLiteral("torrent_set"), hashes, params);
}

Reply<Done> *TransmissionEngine::setDownloadLimit(const QStringList &hashes, qint64 bytesPerSec)
{
    return speedLimit(hashes, bytesPerSec, "download");
}

Reply<Done> *TransmissionEngine::setUploadLimit(const QStringList &hashes, qint64 bytesPerSec)
{
    return speedLimit(hashes, bytesPerSec, "upload");
}

Reply<Done> *TransmissionEngine::setShareLimits(const QStringList &hashes,
                                                RatioLimit ratio,
                                                MinutesLimit seedingTime,
                                                MinutesLimit inactiveSeedingTime,
                                                const std::optional<QString> &action)
{
    // D, K18: ratio and idle time, and a limit always stops the torrent.
    if (seedingTime.mode == LimitMode::Value)
        return unsupported<Done>(QStringLiteral("a limit on seeding time"));
    if (action && *action != QLatin1String("Default") && *action != QLatin1String("Stop"))
        return unsupported<Done>(QStringLiteral("share limit action %1").arg(*action));
    const auto mode = [](LimitMode limit) {
        return limit == LimitMode::Global ? 0 : limit == LimitMode::Value ? 1 : 2;
    };
    QJsonObject params{{QStringLiteral("seed_ratio_mode"), mode(ratio.mode)},
                       {QStringLiteral("seed_idle_mode"), mode(inactiveSeedingTime.mode)}};
    if (ratio.mode == LimitMode::Value)
        params.insert(QStringLiteral("seed_ratio_limit"), ratio.ratio);
    if (inactiveSeedingTime.mode == LimitMode::Value)
        params.insert(QStringLiteral("seed_idle_limit"), inactiveSeedingTime.minutes);
    return forTorrents(QStringLiteral("torrent_set"), hashes, params);
}

Reply<GlobalLimits> *TransmissionEngine::globalLimits()
{
    return mapReply<GlobalLimits>(
        m_rpc->call(QStringLiteral("session_get"),
                    {{QStringLiteral("fields"), fieldsOf(TT::sessionFields())}},
                    Retry::OnTransient),
        this,
        [this](const QJsonObject &sessionGet) {
            m_session = TT::session(sessionGet);
            return TT::globalLimits(sessionGet);
        });
}

Reply<Done> *TransmissionEngine::setGlobalLimits(const GlobalLimits &limits)
{
    const qint64 unit = m_session.speedBytes;
    QJsonObject params;
    if (limits.download) {
        params.insert(QStringLiteral("speed_limit_down_enabled"), *limits.download > 0);
        if (*limits.download > 0)
            params.insert(QStringLiteral("speed_limit_down"),
                          TT::speedUnits(*limits.download, unit));
    }
    if (limits.upload) {
        params.insert(QStringLiteral("speed_limit_up_enabled"), *limits.upload > 0);
        if (*limits.upload > 0)
            params.insert(QStringLiteral("speed_limit_up"), TT::speedUnits(*limits.upload, unit));
    }
    if (limits.altDownload)
        params.insert(QStringLiteral("alt_speed_down"), TT::speedUnits(*limits.altDownload, unit));
    if (limits.altUpload)
        params.insert(QStringLiteral("alt_speed_up"), TT::speedUnits(*limits.altUpload, unit));
    if (params.isEmpty())
        return doneNow();
    return done(m_rpc->call(QStringLiteral("session_set"), params));
}

Reply<QVariantMap> *TransmissionEngine::sessionPreferences()
{
    return mapReply<QVariantMap>(
        m_rpc->call(QStringLiteral("session_get"),
                    {{QStringLiteral("fields"), fieldsOf(TT::sessionFields())}},
                    Retry::OnTransient),
        this,
        [this](const QJsonObject &sessionGet) {
            m_session = TT::session(sessionGet);
            return TT::speedPreferences(sessionGet);
        });
}

Reply<Done> *TransmissionEngine::setSessionPreferences(const QVariantMap &changes)
{
    if (changes.isEmpty())
        return doneNow();
    // The session as it is first: a time's other half and the speed units come from it.
    return then<QJsonObject, Done>(
        m_rpc->call(QStringLiteral("session_get"),
                    {{QStringLiteral("fields"), fieldsOf(TT::sessionFields())}},
                    Retry::OnTransient),
        [this, changes](const QJsonObject &sessionGet) -> Reply<Done> * {
            m_session = TT::session(sessionGet);
            QString why;
            const QJsonObject params = TT::speedSessionSet(changes, sessionGet, &why);
            if (params.isEmpty())
                return settled<Done>(ApiError(ApiError::Kind::Conflict, why));
            return done(m_rpc->call(QStringLiteral("session_set"), params));
        });
}

Reply<Done> *TransmissionEngine::toggleAltSpeed()
{
    return then<QJsonObject, Done>(
        m_rpc->call(QStringLiteral("session_get"),
                    {{QStringLiteral("fields"), QJsonArray{QStringLiteral("alt_speed_enabled")}}},
                    Retry::OnTransient),
        [this](const QJsonObject &sessionGet) {
            return setAltSpeed(!sessionGet.value(QStringLiteral("alt_speed_enabled")).toBool());
        });
}

Reply<Done> *TransmissionEngine::setAltSpeed(bool on)
{
    return done(
        m_rpc->call(QStringLiteral("session_set"), {{QStringLiteral("alt_speed_enabled"), on}}));
}

// --- adding ---------------------------------------------------------------------------------

Reply<AddResult> *TransmissionEngine::add(const QList<QJsonObject> &sources,
                                          const AddOptions &options)
{
    if (sources.isEmpty())
        return settled<AddResult>(AddResult());
    if (const QString why = badLabel(options.tags); !why.isEmpty())
        return settled<AddResult>(ApiError(ApiError::Kind::Conflict, why));

    // What torrent_add takes (rpc-spec §3.4); the rest of AddOptions has no place in it
    // (EngineFeatures::addOptions is off: C25 K27).
    QJsonObject common;
    if (options.savePath && !options.savePath->isEmpty())
        common.insert(QStringLiteral("download_dir"), *options.savePath);
    if (!options.tags.isEmpty())
        common.insert(QStringLiteral("labels"), QJsonArray::fromStringList(options.tags));
    if (options.stopped)
        common.insert(QStringLiteral("paused"), *options.stopped);
    if (options.sequentialDownload)
        common.insert(QStringLiteral("sequential_download"), *options.sequentialDownload);
    if (!options.filePriorities.isEmpty()) {
        QList<int> unwanted, low, normal, high;
        for (qsizetype i = 0; i < options.filePriorities.size(); ++i) {
            switch (options.filePriorities.at(i)) {
            case FilePriority::Skip:
                unwanted.append(int(i));
                break;
            case FilePriority::Low:
                low.append(int(i));
                break;
            case FilePriority::High:
            case FilePriority::Maximal:
                high.append(int(i));
                break;
            case FilePriority::Normal:
            case FilePriority::Mixed:
                normal.append(int(i));
                break;
            }
        }
        // An empty list means every file to Transmission: only lists with files go.
        if (!unwanted.isEmpty())
            common.insert(QStringLiteral("files_unwanted"), numbers(unwanted));
        if (!low.isEmpty())
            common.insert(QStringLiteral("priority_low"), numbers(low));
        if (!high.isEmpty())
            common.insert(QStringLiteral("priority_high"), numbers(high));
    }

    QList<TransmissionRpc::Call> calls;
    for (const QJsonObject &source : sources) {
        QJsonObject params = common;
        for (auto it = source.constBegin(); it != source.constEnd(); ++it)
            params.insert(it.key(), it.value());
        calls.append({QStringLiteral("torrent_add"), params});
    }

    const std::optional<qint64> dlLimit = options.dlLimit;
    const std::optional<qint64> upLimit = options.upLimit;
    auto *added = mapReply<AddResult>(m_rpc->batch(calls), this, [](const Results &results) {
        AddResult result;
        int duplicates = 0;
        for (const Result<QJsonObject> &answer : results) {
            if (answer.isError())
                return Result<AddResult>(answer.error());
            const QJsonObject value = answer.value();
            const QJsonObject torrent = value.value(QStringLiteral("torrent_added")).toObject();
            if (!torrent.isEmpty())
                result.addedIds.append(
                    torrent.value(QStringLiteral("hash_string")).toString().toLower());
            else if (value.contains(QStringLiteral("torrent_duplicate")))
                ++duplicates;
        }
        // Every one already there: the server took the request and added nothing.
        result.refused = result.addedIds.isEmpty() && duplicates > 0;
        return Result<AddResult>(result);
    });
    if (!dlLimit && !upLimit)
        return added;

    // Limits aren't taken at add: set them right after on what was added (C25 §4).
    return then<AddResult, AddResult>(added, [this, dlLimit, upLimit](const AddResult &result) {
        if (result.addedIds.isEmpty())
            return settled<AddResult>(result);
        QJsonObject params{{QStringLiteral("ids"), idsOf(result.addedIds)}};
        if (dlLimit) {
            params.insert(QStringLiteral("download_limited"), *dlLimit > 0);
            if (*dlLimit > 0)
                params.insert(QStringLiteral("download_limit"),
                              TT::speedUnits(*dlLimit, m_session.speedBytes));
        }
        if (upLimit) {
            params.insert(QStringLiteral("upload_limited"), *upLimit > 0);
            if (*upLimit > 0)
                params.insert(QStringLiteral("upload_limit"),
                              TT::speedUnits(*upLimit, m_session.speedBytes));
        }
        return mapReply<AddResult>(m_rpc->call(QStringLiteral("torrent_set"), params),
                                   this,
                                   [result](const QJsonObject &) { return result; });
    });
}

Reply<AddResult> *TransmissionEngine::addUrls(const QStringList &urls, const AddOptions &options)
{
    QList<QJsonObject> sources;
    for (const QString &url : urls)
        sources.append({{QStringLiteral("filename"), url}});
    return add(sources, options);
}

Reply<AddResult> *TransmissionEngine::addFile(const QString &fileName,
                                              const QByteArray &bytes,
                                              const AddOptions &options)
{
    return addMany({AddFile{fileName, bytes}}, {}, options);
}

Reply<AddResult> *TransmissionEngine::addMany(const QList<AddFile> &files,
                                              const QStringList &urls,
                                              const AddOptions &options)
{
    QList<QJsonObject> sources;
    for (const AddFile &file : files)
        sources.append({{QStringLiteral("metainfo"), QString::fromLatin1(file.bytes.toBase64())}});
    for (const QString &url : urls)
        sources.append({{QStringLiteral("filename"), url}});
    return add(sources, options);
}

} // namespace omaseed::core
