#include "core/engines/deluge/DelugeEngine.hpp"

#include "core/MagnetLink.hpp"
#include "core/engines/EngineKind.hpp"
#include "core/engines/deluge/DelugeFingerprint.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTranslate.hpp"

#include <QDateTime>
#include <QPointer>
#include <QSet>

#include <chrono>
#include <limits>
#include <memory>

namespace omaseed::core {

namespace DT = DelugeTranslate;
using Retry = DelugeRpc::Retry;

namespace {

QJsonArray idsOf(const QStringList &hashes)
{
    return QJsonArray::fromStringList(hashes);
}

QJsonArray fieldsOf(const QStringList &fields)
{
    return QJsonArray::fromStringList(fields);
}

/// core.get_torrents_status' filter: these torrents, or every one.
QJsonObject filterOf(const QStringList &hashes)
{
    if (hashes.isEmpty())
        return {};
    return {{QStringLiteral("id"), idsOf(hashes)}};
}

/// A glob that matches `path` itself, whatever it holds (Python's glob.escape: a special
/// character goes in brackets).
QString globEscaped(const QString &path)
{
    QString out;
    out.reserve(path.size());
    for (const QChar c : path) {
        if (c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('['))
            out += QLatin1Char('[') + c + QLatin1Char(']');
        else
            out += c;
    }
    return out;
}

QString parentOf(QString path)
{
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path.left(std::max<qsizetype>(1, path.lastIndexOf(QLatin1Char('/'))));
}

/// "host:port" or "[v6]:port" as (host, port); an empty host when it isn't one.
QPair<QString, int> hostPort(const QString &peer)
{
    const QString text = peer.trimmed();
    const qsizetype colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return {};
    QString host = text.left(colon);
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']')))
        host = host.mid(1, host.size() - 2);
    else if (host.contains(QLatin1Char(':')))
        return {}; // a v6 address needs its brackets
    bool ok = false;
    const int port = text.mid(colon + 1).toInt(&ok);
    if (!ok || port < 1 || port > 65535 || host.isEmpty())
        return {};
    return {host, port};
}

/// Deluge says a torrent is there already in these words (AddTorrentError).
bool isDuplicate(const ApiError &error)
{
    return error.kind() == ApiError::Kind::Conflict &&
           error.message().contains(QLatin1String("already in session"));
}

} // namespace

/// One magnet's lookup: the call in flight, and what it brought.
struct DelugeEngine::Prefetch
{
    bool inFlight = false;
    std::optional<TorrentMetadata> metadata;
    std::optional<ApiError> refused;
};

DelugeEngine::DelugeEngine(QObject *parent)
    : EngineClient(parent)
    , m_rpc(new DelugeRpc(this))
    , m_now([]() { return QDateTime::currentMSecsSinceEpoch(); })
{}

DelugeEngine::~DelugeEngine() = default;

void DelugeEngine::setClock(std::function<qint64()> msNow)
{
    m_now = std::move(msNow);
}

void DelugeEngine::forget()
{
    m_differ.clear();
    m_cursor = 0;
    m_polls = 0;
    m_trackersDirty = true;
    m_labelsDirty = true;
    m_roots.clear();
    m_rootAskedAt.clear();
    m_trackers.clear();
    m_labels.clear();
    m_sentLabels.clear();
    m_prefetches.clear();
}

// --- small helpers --------------------------------------------------------------------------

template<class T>
Reply<T> *DelugeEngine::settled(Result<T> result)
{
    auto *reply = new Reply<T>(this);
    QTimer::singleShot(0, reply, [reply, result]() { reply->complete(result); });
    return reply;
}

Reply<Done> *DelugeEngine::refused(const QString &why)
{
    return settled<Done>(ApiError(ApiError::Kind::Conflict, why));
}

Reply<Done> *DelugeEngine::done(Reply<QJsonValue> *reply)
{
    return mapReply<Done>(reply, this, [](const QJsonValue &) { return Done{}; });
}

Reply<Done> *DelugeEngine::doneAll(Reply<Results> *reply)
{
    return mapReply<Done>(reply, this, [](const Results &results) {
        for (const Result<QJsonValue> &result : results) {
            if (result.isError())
                return Result<Done>(result.error());
        }
        return Result<Done>(Done{});
    });
}

Reply<DelugeEngine::Results> *DelugeEngine::all(const QList<Reply<QJsonValue> *> &replies)
{
    if (replies.isEmpty())
        return settled<Results>(Results());
    auto *out = new Reply<Results>(this);
    auto results = std::make_shared<Results>(replies.size());
    auto left = std::make_shared<qsizetype>(replies.size());
    auto pending = std::make_shared<QList<QPointer<Reply<QJsonValue>>>>();
    for (Reply<QJsonValue> *reply : replies)
        pending->append(reply);
    connect(out, &ReplyBase::abortRequested, out, [pending]() {
        for (const QPointer<Reply<QJsonValue>> &reply : std::as_const(*pending)) {
            if (!reply.isNull())
                reply->abort();
        }
    });
    for (qsizetype i = 0; i < replies.size(); ++i) {
        replies.at(i)->then(out, [out, results, left, i](const Result<QJsonValue> &result) {
            (*results)[i] = result;
            if (--*left == 0)
                out->complete(Result<Results>(*results));
        });
    }
    return out;
}

template<class T, class U>
Reply<U> *DelugeEngine::then(Reply<T> *first, std::function<Reply<U> *(const T &)> next)
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

Reply<Done> *
DelugeEngine::forTorrents(const QString &method, const QStringList &hashes, QJsonArray more)
{
    if (hashes.isEmpty())
        return doneNow();
    QJsonArray params;
    params.append(idsOf(hashes)); // a QJsonArray{array} would copy it, not hold it
    for (const QJsonValue &value : more)
        params.append(value);
    return done(m_rpc->call(method, params));
}

Reply<Done> *DelugeEngine::setOptions(const QStringList &hashes, const QJsonObject &options)
{
    return forTorrents(QStringLiteral("core.set_torrent_options"), hashes, {options});
}

Reply<QList<QJsonObject>> *DelugeEngine::torrentRows(const QStringList &hashes,
                                                     const QStringList &fields)
{
    return mapReply<QList<QJsonObject>>(m_rpc->call(QStringLiteral("core.get_torrents_status"),
                                                    {filterOf(hashes), fieldsOf(fields)},
                                                    Retry::OnTransient),
                                        this,
                                        [](const QJsonValue &result) { return DT::rows(result); });
}

Reply<QJsonObject> *DelugeEngine::oneTorrent(const QString &hash, const QStringList &fields)
{
    if (hash.isEmpty())
        return settled<QJsonObject>(ApiError(ApiError::Kind::NotFound,
                                             QStringLiteral("core.get_torrent_status: no torrent "
                                                            "given")));
    return mapReply<QJsonObject>(
        m_rpc->call(QStringLiteral("core.get_torrent_status"),
                    {hash, fieldsOf(fields)},
                    Retry::OnTransient),
        this,
        [hash](const QJsonValue &result) {
            QJsonObject row = result.toObject();
            if (row.isEmpty()) // Deluge answers {} for a torrent it hasn't
                return Result<QJsonObject>(
                    ApiError(ApiError::Kind::NotFound,
                             QStringLiteral("core.get_torrent_status: no torrent %1").arg(hash)));
            row.insert(QStringLiteral("hash"), hash.toLower());
            return Result<QJsonObject>(row);
        });
}

// --- the connection -------------------------------------------------------------------------

QUrl DelugeEngine::baseUrl() const
{
    return m_rpc->baseUrl();
}

void DelugeEngine::setBaseUrl(const QUrl &baseUrl)
{
    if (baseUrl != m_rpc->baseUrl())
        forget(); // another server: its torrents, labels and lookups are not this one's
    m_rpc->setBaseUrl(baseUrl);
}

void DelugeEngine::setApiKey(const QString &)
{
    // deluge-web signs in with its one password (K7).
}

bool DelugeEngine::hasApiKey() const
{
    return false;
}

void DelugeEngine::setCredentials(const QString &, const QString &password)
{
    m_rpc->setPassword(password);
}

void DelugeEngine::clearAuth()
{
    m_rpc->clearAuth();
    forget();
}

void DelugeEngine::allowLoginAgain()
{
    m_rpc->allowLoginAgain();
}

void DelugeEngine::setPinnedCertificate(const QString &sha256)
{
    m_rpc->setPinnedCertificate(sha256);
}

void DelugeEngine::setTimeout(std::chrono::milliseconds timeout)
{
    m_rpc->setTimeout(timeout);
}

int DelugeEngine::maxRetries() const
{
    return m_rpc->maxRetries();
}

void DelugeEngine::setMaxRetries(int retries)
{
    m_rpc->setMaxRetries(retries);
}

void DelugeEngine::cancelAll()
{
    m_rpc->cancelAll();
}

QUrl DelugeEngine::reachableUrl(const QUrl &base) const
{
    // /json answers anyone (auth.check_session before a sign-in): up there means deluge-web is.
    return DelugeFingerprint::jsonUrl(base);
}

// --- the daemon step (K10) ------------------------------------------------------------------

void DelugeEngine::setDaemonConnected(bool connected)
{
    if (m_daemonConnected == connected)
        return;
    m_daemonConnected = connected;
    Q_EMIT daemonConnectedChanged();
}

void DelugeEngine::setLabelPlugin(bool on)
{
    // DECISIONS C43: the plugin off or on rewrites every torrent's label, which nobody filed.
    // The poll that saw the flip and the next one say so (the labels' folders follow a poll
    // later). Its first reading is no flip.
    const bool flip = m_labelPluginKnown && m_labelPlugin != on;
    m_labelPluginKnown = true;
    if (m_labelPlugin == on)
        return;
    m_labelPlugin = on;
    m_labelsDirty = true;
    if (flip)
        m_filingResetDeltas = 2;
    Q_EMIT labelPluginChanged();
}

void DelugeEngine::setDaemon(const QString &hostId)
{
    m_hostId = hostId;
}

Reply<QList<DelugeHost>> *DelugeEngine::hosts()
{
    return then<QJsonValue, QList<DelugeHost>>(
        m_rpc->call(QStringLiteral("web.get_hosts"), {}, Retry::OnTransient),
        [this](const QJsonValue &rows) {
            QList<DelugeHost> list;
            QList<Reply<QJsonValue> *> statuses;
            for (const QJsonValue &value : rows.toArray()) {
                const QJsonArray row = value.toArray();
                DelugeHost host;
                host.id = row.at(0).toString();
                host.host = row.at(1).toString();
                host.port = row.at(2).toInt();
                host.user = row.at(3).toString();
                list.append(host);
                statuses.append(m_rpc->call(
                    QStringLiteral("web.get_host_status"), {host.id}, Retry::OnTransient));
            }
            return mapReply<QList<DelugeHost>>(all(statuses), this, [list](const Results &results) {
                QList<DelugeHost> out = list;
                for (qsizetype i = 0; i < out.size() && i < results.size(); ++i) {
                    if (results.at(i).isError())
                        continue; // the host stays listed, its status unknown
                    const QJsonArray status = results.at(i).value().toArray();
                    out[i].status = status.at(1).toString();
                    out[i].version = status.at(2).toString();
                }
                return out;
            });
        });
}

Reply<Done> *DelugeEngine::connectDaemon(const QString &hostId)
{
    if (hostId.isEmpty())
        return refused(QStringLiteral("web.connect: no daemon given"));
    QPointer<DelugeEngine> self(this);
    return mapReply<Done>(m_rpc->call(QStringLiteral("web.connect"), {hostId}),
                          this,
                          [self, hostId](const QJsonValue &) {
                              if (!self.isNull()) {
                                  // Another daemon's torrents are another library.
                                  self->forget();
                                  self->m_hostId = hostId;
                                  self->setDaemonConnected(true);
                              }
                              return Done{};
                          });
}

void DelugeEngine::reconnectStored()
{
    // K10: switching deluge-web's daemon switches it for everyone. With none connected there
    // is nobody to switch, so the server's own is connected again (a daemon that restarted).
    if (m_hostId.isEmpty())
        return;
    const qint64 now = m_now();
    if (m_lastReconnectAt != 0 && now - m_lastReconnectAt < 10000)
        return;
    m_lastReconnectAt = now;
    m_rpc->call(QStringLiteral("web.connect"), {m_hostId});
}

// --- what the server is ---------------------------------------------------------------------

Reply<ServerInfo> *DelugeEngine::probe()
{
    auto connected = then<QJsonValue, QJsonValue>(
        m_rpc->call(QStringLiteral("web.connected"), {}, Retry::OnTransient),
        [this](const QJsonValue &connected) -> Reply<QJsonValue> * {
            if (connected.toBool())
                return settled<QJsonValue>(QJsonValue(true));
            if (m_hostId.isEmpty()) {
                setDaemonConnected(false);
                return settled<QJsonValue>(
                    ApiError(ApiError::Kind::Conflict,
                             QStringLiteral("web.connected: deluge-web isn't connected to a "
                                            "Deluge daemon")));
            }
            // Nobody has a daemon there: the server's own is connected (K10).
            m_lastReconnectAt = m_now();
            return m_rpc->call(QStringLiteral("web.connect"), {m_hostId});
        });
    return then<QJsonValue, ServerInfo>(connected, [this](const QJsonValue &) {
        const QList<Reply<QJsonValue> *> calls{
            m_rpc->call(QStringLiteral("daemon.get_version"), {}, Retry::OnTransient),
            m_rpc->call(QStringLiteral("core.get_enabled_plugins"), {}, Retry::OnTransient),
            m_rpc->call(QStringLiteral("core.get_libtorrent_version"), {}, Retry::OnTransient)};
        return mapReply<ServerInfo>(all(calls), this, [this](const Results &results) {
            if (results.value(0).isError())
                return Result<ServerInfo>(results.value(0).error());
            setDaemonConnected(true);
            if (results.value(1).isValid())
                setLabelPlugin(
                    results.value(1).value().toArray().contains(QStringLiteral("Label")));
            if (results.value(2).isValid())
                m_libtorrentVersion = results.value(2).value().toString();
            ServerInfo info;
            info.version = results.value(0).value().toString();
            // K8: below 2.1 (no magnet prefetch) refused, in EngineKind's words.
            const EngineSupport support = engineSupport(EngineKind::Deluge, info.version, {});
            if (info.version.isEmpty())
                return Result<ServerInfo>(
                    ApiError(ApiError::Kind::Parse,
                             QStringLiteral("daemon.get_version: no version from the daemon")));
            if (support.versionRefused)
                return Result<ServerInfo>(ApiError(ApiError::Kind::Conflict, support.refusal));
            return Result<ServerInfo>(info);
        });
    });
}

EngineFeatures DelugeEngine::features(const ServerInfo &info) const
{
    return DT::features(info, m_labelPlugin);
}

// --- sync -----------------------------------------------------------------------------------

QList<SnapshotDiffer::Entry> DelugeEngine::entries(const QList<QJsonObject> &rows)
{
    DT::Context context;
    context.labelPlugin = m_labelPlugin;
    context.nowSecs = m_now() / 1000;
    QList<SnapshotDiffer::Entry> list;
    list.reserve(rows.size());
    for (const QJsonObject &row : rows) {
        const QString hash = row.value(QStringLiteral("hash")).toString();
        if (hash.isEmpty())
            continue;
        if (row.contains(QStringLiteral("files"))) {
            const QString root = DT::contentRoot(row);
            if (!root.isEmpty())
                m_roots.insert(hash, root);
        }
        if (row.contains(QStringLiteral("trackers")))
            m_trackers.insert(hash, DT::trackerUrls(row));
        context.contentRoot = m_roots.value(hash);
        SnapshotDiffer::Entry entry;
        entry.patch = DT::torrent(row, context);
        // `time_since_transfer` counts whole seconds from a "now" each answer takes anew: a
        // second either way is the same moment, not a change.
        if (entry.patch.has(TorrentField::LastActivity) && m_differ.knows(hash)) {
            const QDateTime before = m_differ.torrent(hash).lastActivity;
            const QDateTime now = entry.patch.values.lastActivity;
            if (before.isValid() && now.isValid() && qAbs(before.secsTo(now)) <= 2)
                entry.patch.values.lastActivity = before;
        }
        entry.trackers = m_trackers.contains(hash) ? m_trackers.value(hash) : DT::trackerUrls(row);
        // Deluge's `tracker` stays empty until a torrent announces, so a new one paused at add
        // read as trackerless: its list (asked with its files, or on the next poll) says.
        if (!row.contains(QStringLiteral("trackers"))) {
            if (!m_trackers.contains(hash)) {
                m_trackersDirty = true;
            } else {
                const int count = int(m_trackers.value(hash).size());
                if (!m_differ.knows(hash) || m_differ.torrent(hash).trackersCount != count)
                    entry.patch.set(&Torrent::trackersCount, TorrentField::TrackersCount, count);
            }
        }
        list.append(entry);
    }
    return list;
}

void DelugeEngine::putCategories(SyncDelta &delta, bool full)
{
    const QMap<QString, CategoryPatch> now =
        m_labelPlugin ? m_labels : QMap<QString, CategoryPatch>();
    if (full) {
        for (const CategoryPatch &label : now)
            delta.categories.append(label);
    } else {
        for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
            if (!m_sentLabels.contains(it.key()) || m_sentLabels.value(it.key()) != it.value())
                delta.categories.append(it.value());
        }
        for (auto it = m_sentLabels.constBegin(); it != m_sentLabels.constEnd(); ++it) {
            if (!now.contains(it.key()))
                delta.removedCategories.append(it.key());
        }
    }
    m_sentLabels = now;
}

Reply<SyncDelta> *DelugeEngine::sync(int cursor)
{
    // A cursor this engine didn't hand out last (0, or the models of another server): all of
    // it, as a full delta. Otherwise the same whole list, diffed (Deluge sends no deltas).
    const bool full = cursor <= 0 || cursor != m_cursor;
    QStringList fields = DT::listFields();
    const bool askTrackers = full || m_trackersDirty || m_polls % kTrackersEveryPolls == 0;
    if (askTrackers)
        fields.append(QStringLiteral("trackers"));

    QPointer<DelugeEngine> self(this);
    Reply<QJsonValue> *updateUi = m_rpc->call(
        QStringLiteral("web.update_ui"), {fieldsOf(fields), QJsonObject()}, Retry::OnTransient);
    // First the list; then, once the daemon is known to be there (a core call to deluge-web
    // without one is never answered), the session's totals and whatever the list left open.
    auto *second =
        then<QJsonValue, Results>(updateUi, [this, full, askTrackers](const QJsonValue &answer) {
            const DT::Poll poll = DT::poll(answer);
            QList<Reply<QJsonValue> *> calls;
            if (!poll.connected) {
                setDaemonConnected(false);
                reconnectStored();
                return settled<Results>(ApiError(ApiError::Kind::Conflict,
                                                 QStringLiteral("web.update_ui: deluge-web isn't "
                                                                "connected to a Deluge daemon")));
            }
            setDaemonConnected(true);
            setLabelPlugin(poll.labelPlugin);
            if (askTrackers)
                m_trackersDirty = false;
            calls.append(m_rpc->call(QStringLiteral("core.get_session_status"),
                                     {QJsonValue(fieldsOf(DT::sessionStatusKeys()))},
                                     Retry::OnTransient));
            // Where each torrent's content starts, once it has files (a magnet has none yet).
            const qint64 now = m_now();
            QStringList roots;
            for (const QJsonObject &row : poll.torrents) {
                const QString hash = row.value(QStringLiteral("hash")).toString();
                if (m_roots.contains(hash) || row.value(QStringLiteral("num_pieces")).toInt() <= 0)
                    continue;
                const auto asked = m_rootAskedAt.constFind(hash);
                if (asked != m_rootAskedAt.constEnd() && now - *asked < kRootRetryMs)
                    continue;
                m_rootAskedAt.insert(hash, now);
                roots.append(hash);
            }
            calls.append(
                roots.isEmpty()
                    ? settled<QJsonValue>(QJsonValue(QJsonObject()))
                    : m_rpc->call(QStringLiteral("core.get_torrents_status"),
                                  {filterOf(roots),
                                   fieldsOf({QStringLiteral("files"), QStringLiteral("trackers")})},
                                  Retry::OnTransient));
            // Each label's folder, when the labels moved, now and then, and on a full poll.
            QStringList known = m_labels.keys();
            const bool askLabels =
                poll.labelPlugin &&
                (full || m_labelsDirty || known != poll.labels || m_polls % kLabelsEveryPolls == 0);
            if (!poll.labelPlugin) {
                m_labels.clear();
            } else if (askLabels) {
                m_labelsDirty = false;
                for (const QString &label : poll.labels)
                    calls.append(m_rpc->call(
                        QStringLiteral("label.get_options"), {label}, Retry::OnTransient));
            }
            auto *gathered = all(calls);
            // The list rides along as the last result, so the delta is built from one answer.
            return mapReply<Results>(gathered, this, [answer](const Results &results) {
                Results out = results;
                out.append(Result<QJsonValue>(answer));
                return out;
            });
        });

    return mapReply<SyncDelta>(second, this, [self, full](const Results &results) {
        if (self.isNull())
            return Result<SyncDelta>(
                ApiError(ApiError::Kind::Cancelled, QStringLiteral("web.update_ui: cancelled")));
        DelugeEngine *engine = self.data();
        const DT::Poll poll = DT::poll(results.constLast().value());
        const QJsonObject session =
            results.value(0).isError() ? QJsonObject() : results.value(0).value().toObject();
        // The files and trackers asked for torrents new to the engine.
        if (results.value(1).isValid()) {
            for (const QJsonObject &row : DT::rows(results.value(1).value())) {
                const QString hash = row.value(QStringLiteral("hash")).toString();
                const QString root = DT::contentRoot(row);
                if (!root.isEmpty())
                    engine->m_roots.insert(hash, root);
                if (row.contains(QStringLiteral("trackers")))
                    engine->m_trackers.insert(hash, DT::trackerUrls(row));
            }
        }
        // The labels' options, one answer per label, in the order asked.
        if (poll.labelPlugin && results.size() - 3 == poll.labels.size() &&
            !poll.labels.isEmpty()) {
            QMap<QString, CategoryPatch> labels;
            for (qsizetype i = 0; i < poll.labels.size(); ++i) {
                const Result<QJsonValue> &options = results.at(2 + i);
                const QString &name = poll.labels.at(i);
                labels.insert(name,
                              options.isError()
                                  ? engine->m_labels.value(name, CategoryPatch{name, QString()})
                                  : DT::category(name, options.value().toObject()));
            }
            engine->m_labels = labels;
        } else if (poll.labelPlugin && poll.labels.isEmpty()) {
            engine->m_labels.clear();
        }

        const QList<SnapshotDiffer::Entry> listed = engine->entries(poll.torrents);
        SyncDelta delta = engine->m_differ.everything(listed, full);
        // What the engine keeps of torrents no longer listed goes with them.
        const QStringList known = engine->m_differ.hashes();
        const QSet<QString> still(known.cbegin(), known.cend());
        const auto keepListed = [&still](auto &map) {
            for (auto it = map.begin(); it != map.end();)
                it = still.contains(it.key()) ? std::next(it) : map.erase(it);
        };
        keepListed(engine->m_roots);
        keepListed(engine->m_trackers);
        keepListed(engine->m_rootAskedAt);
        engine->putCategories(delta, full);
        delta.serverState = DT::serverState(poll.stats, session);
        if (engine->m_filingResetDeltas > 0) {
            delta.filingReset = true;
            --engine->m_filingResetDeltas;
        }

        ++engine->m_polls;
        engine->m_cursor =
            engine->m_cursor >= std::numeric_limits<int>::max() - 1 ? 1 : engine->m_cursor + 1;
        delta.cursor = engine->m_cursor;
        return Result<SyncDelta>(delta);
    });
}

Reply<QList<TorrentPatch>> *DelugeEngine::torrents(const QStringList &hashes)
{
    return mapReply<QList<TorrentPatch>>(
        torrentRows(hashes, DT::listFields()), this, [this](const QList<QJsonObject> &rows) {
            DT::Context context;
            context.labelPlugin = m_labelPlugin;
            context.nowSecs = m_now() / 1000;
            QList<TorrentPatch> patches;
            patches.reserve(rows.size());
            for (const QJsonObject &row : rows) {
                context.contentRoot = m_roots.value(row.value(QStringLiteral("hash")).toString());
                patches.append(DT::torrent(row, context));
            }
            return patches;
        });
}

Reply<QStringList> *DelugeEngine::stoppedAmong(const QStringList &hashes)
{
    // #244: paused, not just stopped by its state word, so a resumed torrent Deluge keeps in
    // Error reads as running in error.
    return mapReply<QStringList>(
        torrentRows(hashes, {QStringLiteral("state"), QStringLiteral("paused")}),
        this,
        [](const QList<QJsonObject> &rows) {
            QStringList stopped;
            for (const QJsonObject &row : rows) {
                if (DT::isPaused(row))
                    stopped.append(row.value(QStringLiteral("hash")).toString());
            }
            return stopped;
        });
}

Reply<QList<RunningTorrent>> *DelugeEngine::running()
{
    return mapReply<QList<RunningTorrent>>(
        torrentRows({}, {QStringLiteral("state"), QStringLiteral("is_auto_managed")}),
        this,
        [](const QList<QJsonObject> &rows) {
            QList<RunningTorrent> list;
            for (const QJsonObject &row : rows) {
                if (DT::isStopped(row))
                    continue;
                // Out of the queue's hands: started past it (engine matrix §4.5).
                list.append({row.value(QStringLiteral("hash")).toString(),
                             !row.value(QStringLiteral("is_auto_managed")).toBool(true)});
            }
            return list;
        });
}

Reply<QList<CategoryPatch>> *DelugeEngine::categories()
{
    if (!m_labelPlugin)
        return settled<QList<CategoryPatch>>(QList<CategoryPatch>()); // no labels at all (K15)
    return then<QJsonValue, QList<CategoryPatch>>(
        m_rpc->call(QStringLiteral("label.get_labels"), {}, Retry::OnTransient),
        [this](const QJsonValue &labels) {
            QStringList names;
            QList<Reply<QJsonValue> *> calls;
            for (const QJsonValue &label : labels.toArray()) {
                names.append(label.toString());
                calls.append(m_rpc->call(
                    QStringLiteral("label.get_options"), {label.toString()}, Retry::OnTransient));
            }
            return mapReply<QList<CategoryPatch>>(
                all(calls), this, [names](const Results &results) {
                    QList<CategoryPatch> list;
                    for (qsizetype i = 0; i < names.size(); ++i) {
                        if (i < results.size() && results.at(i).isError())
                            return Result<QList<CategoryPatch>>(results.at(i).error());
                        list.append(DT::category(names.at(i), results.value(i).value().toObject()));
                    }
                    return Result<QList<CategoryPatch>>(list);
                });
        });
}

Reply<QStringList> *DelugeEngine::tags()
{
    return settled<QStringList>(QStringList()); // none on Deluge (C25 B): an honest empty list
}

// --- details --------------------------------------------------------------------------------

Reply<TorrentProperties> *DelugeEngine::properties(const QString &hash)
{
    return mapReply<TorrentProperties>(
        oneTorrent(hash, DT::propertiesFields()), this, DT::properties);
}

Reply<QList<ContentFile>> *DelugeEngine::files(const QString &hash)
{
    return mapReply<QList<ContentFile>>(oneTorrent(hash, DT::filesFields()), this, DT::files);
}

Reply<QList<TrackerEntry>> *DelugeEngine::trackers(const QString &hash)
{
    return mapReply<QList<TrackerEntry>>(
        oneTorrent(hash, DT::trackersFields()), this, DT::trackers);
}

Reply<PeersDelta> *DelugeEngine::peers(const QString &hash, int)
{
    // Deluge has no peer deltas: the whole list every time.
    return mapReply<PeersDelta>(oneTorrent(hash, DT::peersFields()), this, DT::peers);
}

Reply<QStringList> *DelugeEngine::webSeeds(const QString &)
{
    return unsupported<QStringList>(QStringLiteral("web seeds")); // board 47 H: none reported
}

Reply<QByteArray> *DelugeEngine::pieceStates(const QString &hash)
{
    return mapReply<QByteArray>(oneTorrent(hash, DT::piecesFields()), this, DT::pieceStates);
}

Reply<FetchedMetadata> *DelugeEngine::fetchMetadata(const QString &source, const QString &)
{
    if (!source.trimmed().startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
        // Deluge prefetches magnets only; a link's .torrent is read once it is added.
        return unsupported<FetchedMetadata>(QStringLiteral("a link's files before the add"));
    if (MagnetLink::parse(source).infoHashV1.isEmpty())
        return settled<FetchedMetadata>(
            ApiError(ApiError::Kind::Parse,
                     QStringLiteral("core.prefetch_magnet_metadata: Deluge looks up magnets "
                                    "with a v1 hash only")));

    std::shared_ptr<Prefetch> &slot = m_prefetches[source];
    if (!slot)
        slot = std::make_shared<Prefetch>();
    const std::shared_ptr<Prefetch> lookup = slot;
    if (lookup->metadata) {
        FetchedMetadata fetched;
        fetched.ready = true;
        fetched.metadata = lookup->metadata;
        return settled<FetchedMetadata>(fetched);
    }
    if (lookup->refused) {
        const ApiError error = *lookup->refused;
        m_prefetches.remove(source); // said once; asked again, it is tried again
        return settled<FetchedMetadata>(error);
    }
    if (!lookup->inFlight) {
        // One blocking call (research/87): it answers with the metadata, or empty after its
        // own wait. The HTTP wait is longer than Deluge's, so the answer always gets here.
        lookup->inFlight = true;
        QPointer<DelugeEngine> self(this);
        m_rpc
            ->call(QStringLiteral("core.prefetch_magnet_metadata"),
                   {source, kPrefetchSeconds},
                   Retry::No,
                   std::chrono::seconds(kPrefetchSeconds + 15))
            ->then(this, [lookup, source](const Result<QJsonValue> &result) {
                lookup->inFlight = false;
                if (result.isError()) {
                    // The daemon refused the magnet itself (it is in the library already, or
                    // not one it can read): the lookup stops. A lost answer is asked again.
                    if (result.error().kind() == ApiError::Kind::Conflict)
                        lookup->refused = ApiError(ApiError::Kind::Parse, result.error().message());
                    return;
                }
                bool unreadable = false;
                lookup->metadata = DT::metadata(result.value(), source, &unreadable);
                if (unreadable)
                    lookup->refused =
                        ApiError(ApiError::Kind::Parse,
                                 QStringLiteral("core.prefetch_magnet_metadata: the metadata "
                                                "doesn't read as a torrent's"));
            });
    }
    return settled<FetchedMetadata>(FetchedMetadata{});
}

Reply<ServerFolder> *DelugeEngine::folderContent(const QString &path)
{
    if (!path.startsWith(QLatin1Char('/'))) {
        ServerFolder failed;
        failed.state = ServerFolder::State::Failed;
        return settled<ServerFolder>(failed);
    }
    QString folder = path;
    while (folder.size() > 1 && folder.endsWith(QLatin1Char('/')))
        folder.chop(1);
    const QString pattern = globEscaped(folder) + QStringLiteral("/*");
    auto *out = new Reply<ServerFolder>(this);
    QPointer<Reply<ServerFolder>> to(out);
    m_rpc->call(QStringLiteral("core.glob"), {pattern}, Retry::OnTransient)
        ->then(this, [this, to, folder](const Result<QJsonValue> &result) {
            if (to.isNull())
                return;
            ServerFolder listed;
            if (result.isError()) {
                to->complete(Result<ServerFolder>(listed)); // Failed: can't tell
                return;
            }
            for (const QJsonValue &entry : result.value().toArray()) {
                const QString name = entry.toString();
                listed.names.insert(name.mid(name.lastIndexOf(QLatin1Char('/')) + 1));
            }
            listed.state = ServerFolder::State::Listed;
            if (!listed.names.isEmpty()) {
                to->complete(Result<ServerFolder>(listed));
                return;
            }
            // Nothing in it, or nothing there: Deluge's size of a missing path is -1.
            m_rpc->call(QStringLiteral("core.get_path_size"), {folder}, Retry::OnTransient)
                ->then(this, [to, listed](const Result<QJsonValue> &size) {
                    if (to.isNull())
                        return;
                    ServerFolder answer = listed;
                    if (size.isError())
                        answer.state = ServerFolder::State::Failed;
                    else if (size.value().toDouble() < 0)
                        answer.state = ServerFolder::State::Missing;
                    to->complete(Result<ServerFolder>(answer));
                });
        });
    return out;
}

Reply<FreeSpaceAt> *DelugeEngine::freeSpace(const QString &path)
{
    auto *reply = new Reply<FreeSpaceAt>(this);
    if (!path.startsWith(QLatin1Char('/'))) {
        QTimer::singleShot(0, reply, [reply, path]() {
            reply->complete(Result<FreeSpaceAt>(
                ApiError(ApiError::Kind::Conflict,
                         QStringLiteral("core.get_free_space: %1 isn't absolute").arg(path))));
        });
        return reply;
    }
    freeSpaceUp(path, reply, 0);
    return reply;
}

void DelugeEngine::freeSpaceUp(const QString &path, Reply<FreeSpaceAt> *reply, int depth)
{
    QPointer<Reply<FreeSpaceAt>> to(reply);
    m_rpc->call(QStringLiteral("core.get_free_space"), {path}, Retry::OnTransient)
        ->then(this, [this, to, path, depth](const Result<QJsonValue> &result) {
            if (to.isNull())
                return;
            if (result.isError()) {
                to->complete(Result<FreeSpaceAt>(result.error()));
                return;
            }
            const qint64 free = qint64(result.value().toDouble(-1));
            if (free < 0) {
                // A folder that isn't there yet (the move makes it) is read on its parent.
                const QString parent = parentOf(path);
                if (parent != path && depth < 32) {
                    freeSpaceUp(parent, to.data(), depth + 1);
                    return;
                }
            }
            FreeSpaceAt at;
            at.path = path;
            at.bytesFree = free < 0 ? -1 : free;
            to->complete(Result<FreeSpaceAt>(at)); // Deluge doesn't say the drive's size
        });
}

// --- actions --------------------------------------------------------------------------------

Reply<Done> *DelugeEngine::start(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("core.resume_torrents"), hashes);
}

Reply<Done> *DelugeEngine::stop(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("core.pause_torrents"), hashes);
}

Reply<Done> *DelugeEngine::startAll()
{
    return done(m_rpc->call(QStringLiteral("core.resume_torrents"))); // no ids: every torrent
}

Reply<Done> *DelugeEngine::stopAll()
{
    return done(m_rpc->call(QStringLiteral("core.pause_torrents")));
}

Reply<Done> *DelugeEngine::setForceStart(const QStringList &hashes, bool on)
{
    if (hashes.isEmpty())
        return doneNow();
    // Out of the queue's hands and running is Deluge's "forced" (engine matrix §4.5); back in
    // them, the queue decides again.
    if (!on)
        return setOptions(hashes, {{QStringLiteral("auto_managed"), true}});
    return then<Done, Done>(setOptions(hashes, {{QStringLiteral("auto_managed"), false}}),
                            [this, hashes](const Done &) { return start(hashes); });
}

Reply<Done> *DelugeEngine::recheck(const QStringList &hashes)
{
    // Also what clears a failed move's error, which a resume doesn't (K24, matrix §4.4).
    return forTorrents(QStringLiteral("core.force_recheck"), hashes);
}

Reply<Done> *DelugeEngine::reannounce(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("core.force_reannounce"), hashes);
}

Reply<Done> *DelugeEngine::rename(const QString &hash, const QString &name)
{
    if (hash.isEmpty())
        return refused(QStringLiteral("core.set_torrent_options: no torrent given"));
    if (name.trimmed().isEmpty())
        return refused(QStringLiteral("a torrent's name can't be empty"));
    return setOptions({hash}, {{QStringLiteral("name"), name}});
}

Reply<Done> *DelugeEngine::setLocation(const QStringList &hashes, const QString &path)
{
    // Deluge answers at once and shows the move as Moving with its own % (K21). A target it
    // can't make is refused in its log only: the folder stays the old one (research/87).
    return forTorrents(QStringLiteral("core.move_storage"), hashes, {path});
}

Reply<Done> *
DelugeEngine::renameFile(const QString &hash, const QString &oldPath, const QString &newPath)
{
    if (oldPath == newPath)
        return doneNow();
    return then<QJsonObject, Done>(
        oneTorrent(hash, {QStringLiteral("files")}),
        [this, hash, oldPath, newPath](const QJsonObject &row) -> Reply<Done> * {
            int index = -1;
            for (const QJsonValue &value : row.value(QStringLiteral("files")).toArray()) {
                const QJsonObject file = value.toObject();
                if (file.value(QStringLiteral("path")).toString() == oldPath)
                    index = file.value(QStringLiteral("index")).toInt(-1);
            }
            if (index < 0)
                return settled<Done>(
                    ApiError(ApiError::Kind::NotFound,
                             QStringLiteral("core.rename_files: no file %1").arg(oldPath)));
            // Renamed in the background: where its content starts is asked again.
            m_roots.remove(hash);
            m_rootAskedAt.remove(hash);
            return done(m_rpc->call(QStringLiteral("core.rename_files"),
                                    {hash, QJsonArray{QJsonValue(QJsonArray{index, newPath})}}));
        });
}

Reply<Done> *
DelugeEngine::renameFolder(const QString &hash, const QString &oldPath, const QString &newPath)
{
    if (hash.isEmpty())
        return refused(QStringLiteral("core.rename_folder: no torrent given"));
    if (oldPath == newPath)
        return doneNow();
    m_roots.remove(hash);
    m_rootAskedAt.remove(hash);
    return done(m_rpc->call(QStringLiteral("core.rename_folder"), {hash, oldPath, newPath}));
}

Reply<Done> *
DelugeEngine::setFilePriority(const QString &hash, const QList<int> &fileIds, FilePriority priority)
{
    if (fileIds.isEmpty())
        return doneNow();
    // Deluge takes every file's priority at once: read them, change those given, send all.
    return then<QJsonObject, Done>(
        oneTorrent(hash, {QStringLiteral("file_priorities")}),
        [this, hash, fileIds, priority](const QJsonObject &row) -> Reply<Done> * {
            QJsonArray priorities = row.value(QStringLiteral("file_priorities")).toArray();
            for (const int id : fileIds) {
                if (id < 0 || id >= priorities.size())
                    return settled<Done>(ApiError(
                        ApiError::Kind::NotFound,
                        QStringLiteral("core.set_torrent_file_priorities: no file %1").arg(id)));
                priorities[id] = DT::delugePriority(priority);
            }
            return done(m_rpc->call(QStringLiteral("core.set_torrent_file_priorities"),
                                    {hash, priorities}));
        });
}

Reply<Done> *DelugeEngine::queueUp(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("core.queue_up"), hashes);
}

Reply<Done> *DelugeEngine::queueDown(const QStringList &hashes)
{
    return forTorrents(QStringLiteral("core.queue_down"), hashes);
}

Reply<Done> *
DelugeEngine::toggle(const QStringList &hashes, const QString &readKey, const QString &optionKey)
{
    if (hashes.isEmpty())
        return doneNow();
    // Deluge sets a value, it doesn't flip one: read each, then send the opposite.
    return then<QList<QJsonObject>, Done>(
        torrentRows(hashes, {readKey}), [this, readKey, optionKey](const QList<QJsonObject> &rows) {
            QStringList on;
            QStringList off;
            for (const QJsonObject &row : rows)
                (row.value(readKey).toBool() ? off : on)
                    .append(row.value(QStringLiteral("hash")).toString());
            QList<Reply<QJsonValue> *> calls;
            if (!on.isEmpty())
                calls.append(m_rpc->call(QStringLiteral("core.set_torrent_options"),
                                         {idsOf(on), QJsonObject{{optionKey, true}}}));
            if (!off.isEmpty())
                calls.append(m_rpc->call(QStringLiteral("core.set_torrent_options"),
                                         {idsOf(off), QJsonObject{{optionKey, false}}}));
            return doneAll(all(calls));
        });
}

Reply<Done> *DelugeEngine::toggleSequentialDownload(const QStringList &hashes)
{
    return toggle(
        hashes, QStringLiteral("sequential_download"), QStringLiteral("sequential_download"));
}

Reply<Done> *DelugeEngine::toggleFirstLastPiece(const QStringList &hashes)
{
    // Read as `prioritize_first_last`, set as the option `prioritize_first_last_pieces`.
    return toggle(hashes,
                  QStringLiteral("prioritize_first_last"),
                  QStringLiteral("prioritize_first_last_pieces"));
}

Reply<Done> *DelugeEngine::setSuperSeeding(const QStringList &hashes, bool on)
{
    return setOptions(hashes, {{QStringLiteral("super_seeding"), on}});
}

Reply<Done> *DelugeEngine::setAutoManagement(const QStringList &, bool)
{
    return unsupported<Done>(QStringLiteral("automatic torrent management")); // C25 I
}

Reply<Done> *DelugeEngine::remove(const QStringList &hashes, bool deleteFiles)
{
    if (hashes.isEmpty())
        return doneNow();
    return mapReply<Done>(
        m_rpc->call(QStringLiteral("core.remove_torrents"), {idsOf(hashes), deleteFiles}),
        this,
        [](const QJsonValue &failures) {
            // [[hash, why], …] for each one Deluge couldn't remove; one it doesn't have is gone
            // already, which is what was asked.
            for (const QJsonValue &value : failures.toArray()) {
                const QString why = value.toArray().at(1).toString();
                if (!why.contains(QLatin1String("not in session")))
                    return Result<Done>(
                        ApiError(ApiError::Kind::Conflict,
                                 QStringLiteral("core.remove_torrents: %1").arg(why)));
            }
            return Result<Done>(Done{});
        });
}

Reply<Done> *DelugeEngine::trackersEdit(const QString &hash,
                                        std::function<QJsonArray(const QJsonArray &)> change)
{
    // The trackers are replaced whole (core.set_torrent_trackers): read, change, write.
    return then<QJsonObject, Done>(
        oneTorrent(hash, {QStringLiteral("trackers")}),
        [this, hash, change](const QJsonObject &row) -> Reply<Done> * {
            const QJsonArray before = row.value(QStringLiteral("trackers")).toArray();
            const QJsonArray after = change(before);
            if (DT::trackersWithout(before, {}) == after)
                return doneNow();
            m_trackersDirty = true;
            return done(m_rpc->call(QStringLiteral("core.set_torrent_trackers"), {hash, after}));
        });
}

Reply<Done> *DelugeEngine::addTrackers(const QString &hash, const QStringList &urls)
{
    if (urls.isEmpty())
        return doneNow();
    return trackersEdit(
        hash, [urls](const QJsonArray &trackers) { return DT::trackersWith(trackers, urls); });
}

Reply<Done> *DelugeEngine::removeTrackers(const QString &hash, const QStringList &urls)
{
    if (urls.isEmpty())
        return doneNow();
    return trackersEdit(
        hash, [urls](const QJsonArray &trackers) { return DT::trackersWithout(trackers, urls); });
}

Reply<Done> *DelugeEngine::addWebSeeds(const QString &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("editing web seeds")); // board 47 H
}

Reply<Done> *DelugeEngine::removeWebSeeds(const QString &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("editing web seeds"));
}

Reply<Done> *DelugeEngine::addPeers(const QStringList &hashes, const QStringList &peers)
{
    if (hashes.isEmpty() || peers.isEmpty())
        return doneNow();
    QList<QPair<QString, int>> addresses;
    for (const QString &peer : peers) {
        const QPair<QString, int> address = hostPort(peer);
        if (address.first.isEmpty())
            return refused(QStringLiteral("%1 isn't host:port").arg(peer));
        addresses.append(address);
    }
    QList<Reply<QJsonValue> *> calls;
    for (const QString &hash : hashes) {
        for (const auto &[host, port] : std::as_const(addresses))
            calls.append(m_rpc->call(QStringLiteral("core.connect_peer"), {hash, host, port}));
    }
    return doneAll(all(calls));
}

Reply<Done> *DelugeEngine::banPeers(const QStringList &)
{
    return unsupported<Done>(QStringLiteral("banning peers")); // board 47 H
}

// --- labels (the Label plugin, in the categories place: C25 B) -------------------------------

Reply<Done> *DelugeEngine::setCategory(const QStringList &hashes, const QString &category)
{
    if (!m_labelPlugin)
        return unsupported<Done>(QStringLiteral("labels (the Label plugin is off)"));
    if (hashes.isEmpty())
        return doneNow();
    QString label;
    if (!category.isEmpty()) {
        QString why;
        label = DT::labelName(category, &why);
        if (label.isEmpty())
            return refused(why);
    }
    QList<Reply<QJsonValue> *> calls;
    for (const QString &hash : hashes)
        calls.append(m_rpc->call(QStringLiteral("label.set_torrent"), {hash, label}));
    return doneAll(all(calls));
}

Reply<Done> *DelugeEngine::createCategory(const QString &name, const QString &savePath)
{
    if (!m_labelPlugin)
        return unsupported<Done>(QStringLiteral("labels (the Label plugin is off)"));
    QString why;
    const QString label = DT::labelName(name, &why);
    if (label.isEmpty())
        return refused(why);
    m_labelsDirty = true;
    Reply<Done> *added = done(m_rpc->call(QStringLiteral("label.add"), {label}));
    if (savePath.isEmpty())
        return added;
    // A label's folder is where its torrents move when they finish (C25 B).
    return then<Done, Done>(added, [this, label, savePath](const Done &) {
        return done(m_rpc->call(QStringLiteral("label.set_options"),
                                {label,
                                 QJsonObject{{QStringLiteral("apply_move_completed"), true},
                                             {QStringLiteral("move_completed"), true},
                                             {QStringLiteral("move_completed_path"), savePath}}}));
    });
}

Reply<Done> *DelugeEngine::removeCategories(const QStringList &names)
{
    if (!m_labelPlugin)
        return unsupported<Done>(QStringLiteral("labels (the Label plugin is off)"));
    m_labelsDirty = true;
    QList<Reply<QJsonValue> *> calls;
    for (const QString &name : names)
        calls.append(m_rpc->call(QStringLiteral("label.remove"), {name.trimmed().toLower()}));
    return doneAll(all(calls));
}

Reply<Done> *DelugeEngine::addTags(const QStringList &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("tags")); // C25 B: one label per torrent only
}

Reply<Done> *DelugeEngine::removeTags(const QStringList &, const QStringList &)
{
    return unsupported<Done>(QStringLiteral("tags"));
}

Reply<Done> *DelugeEngine::createTags(const QStringList &)
{
    return unsupported<Done>(QStringLiteral("tags"));
}

Reply<Done> *DelugeEngine::deleteTags(const QStringList &)
{
    return unsupported<Done>(QStringLiteral("tags"));
}

// --- limits ---------------------------------------------------------------------------------

Reply<Done> *DelugeEngine::setDownloadLimit(const QStringList &hashes, qint64 bytesPerSec)
{
    return setOptions(hashes, {{QStringLiteral("max_download_speed"), DT::speedKiB(bytesPerSec)}});
}

Reply<Done> *DelugeEngine::setUploadLimit(const QStringList &hashes, qint64 bytesPerSec)
{
    return setOptions(hashes, {{QStringLiteral("max_upload_speed"), DT::speedKiB(bytesPerSec)}});
}

Reply<Done> *DelugeEngine::setShareLimits(const QStringList &hashes,
                                          RatioLimit ratio,
                                          MinutesLimit seedingTime,
                                          MinutesLimit inactiveSeedingTime,
                                          const std::optional<QString> &action)
{
    // D, K18: a ratio, then stop or remove keeping the files; no time limits.
    if (seedingTime.mode == LimitMode::Value)
        return unsupported<Done>(QStringLiteral("a limit on seeding time"));
    if (inactiveSeedingTime.mode == LimitMode::Value)
        return unsupported<Done>(QStringLiteral("a limit on time without traffic"));
    std::optional<bool> removeAtRatio;
    if (action) {
        if (*action == QLatin1String("Remove"))
            removeAtRatio = true;
        else if (*action == QLatin1String("Stop") || *action == QLatin1String("Default"))
            removeAtRatio = false;
        else
            return unsupported<Done>(QStringLiteral("share limit action %1").arg(*action));
    }
    if (hashes.isEmpty())
        return doneNow();
    if (ratio.mode != LimitMode::Global) {
        QJsonObject options{{QStringLiteral("stop_at_ratio"), ratio.mode == LimitMode::Value}};
        if (ratio.mode == LimitMode::Value)
            options.insert(QStringLiteral("stop_ratio"), ratio.ratio);
        if (removeAtRatio)
            options.insert(QStringLiteral("remove_at_ratio"), *removeAtRatio);
        return setOptions(hashes, options);
    }
    // "The server's": a Deluge torrent keeps its own copy, so the server's goes onto it.
    return then<QJsonValue, Done>(
        m_rpc->call(QStringLiteral("core.get_config_values"),
                    {QJsonValue(fieldsOf({QStringLiteral("stop_seed_at_ratio"),
                                          QStringLiteral("stop_seed_ratio"),
                                          QStringLiteral("remove_seed_at_ratio")}))},
                    Retry::OnTransient),
        [this, hashes, removeAtRatio](const QJsonValue &value) {
            const QJsonObject config = value.toObject();
            return setOptions(
                hashes,
                {{QStringLiteral("stop_at_ratio"),
                  config.value(QStringLiteral("stop_seed_at_ratio")).toBool()},
                 {QStringLiteral("stop_ratio"),
                  config.value(QStringLiteral("stop_seed_ratio")).toDouble(2.0)},
                 {QStringLiteral("remove_at_ratio"),
                  removeAtRatio.value_or(
                      config.value(QStringLiteral("remove_seed_at_ratio")).toBool())}});
        });
}

Reply<GlobalLimits> *DelugeEngine::globalLimits()
{
    return mapReply<GlobalLimits>(
        m_rpc->call(QStringLiteral("core.get_config_values"),
                    {QJsonValue(fieldsOf(DT::configKeys()))},
                    Retry::OnTransient),
        this,
        [](const QJsonValue &config) { return DT::globalLimits(config.toObject()); });
}

Reply<Done> *DelugeEngine::setGlobalLimits(const GlobalLimits &limits)
{
    QJsonObject config;
    if (limits.download)
        config.insert(QStringLiteral("max_download_speed"), DT::speedKiB(*limits.download));
    if (limits.upload)
        config.insert(QStringLiteral("max_upload_speed"), DT::speedKiB(*limits.upload));
    // The alternative ones have no place in Deluge (K20).
    if (config.isEmpty())
        return doneNow();
    return done(m_rpc->call(QStringLiteral("core.set_config"), {config}));
}

Reply<QVariantMap> *DelugeEngine::sessionPreferences()
{
    // Settings › Deluge reads the whole of core.conf in one call (its plugins among it).
    return mapReply<QVariantMap>(
        m_rpc->call(QStringLiteral("core.get_config"), {}, Retry::OnTransient),
        this,
        [](const QJsonValue &config) { return DT::speedPreferences(config.toObject()); });
}

Reply<Done> *DelugeEngine::setSessionPreferences(const QVariantMap &changes)
{
    if (changes.isEmpty())
        return doneNow();
    QString why;
    const QJsonObject config = DT::configSet(changes, &why);
    if (config.isEmpty())
        return refused(why);
    return done(m_rpc->call(QStringLiteral("core.set_config"), {config}));
}

Reply<bool> *DelugeEngine::testPort()
{
    return mapReply<bool>(
        m_rpc->call(
            QStringLiteral("core.test_listen_port"), {}, Retry::No, std::chrono::seconds(60)),
        this,
        [](const QJsonValue &open) { return open.toBool(); });
}

Reply<Done> *DelugeEngine::toggleAltSpeed()
{
    return unsupported<Done>(QStringLiteral("turtle mode")); // K20
}

Reply<Done> *DelugeEngine::setAltSpeed(bool)
{
    return unsupported<Done>(QStringLiteral("turtle mode")); // K20
}

// --- adding ---------------------------------------------------------------------------------

Reply<AddResult> *DelugeEngine::add(const QList<QPair<QString, QJsonArray>> &calls,
                                    const AddOptions &options)
{
    if (calls.isEmpty())
        return settled<AddResult>(AddResult());
    QString label;
    if (options.category && !options.category->isEmpty() && m_labelPlugin) {
        QString why;
        label = DT::labelName(*options.category, &why);
        if (label.isEmpty())
            return settled<AddResult>(ApiError(ApiError::Kind::Conflict, why));
    }

    // Deluge's add options (core.add_torrent_*); no content layout (K27), no tags, and the
    // label is set right after (engine matrix §4.9).
    QJsonObject common;
    if (options.savePath && !options.savePath->isEmpty())
        common.insert(QStringLiteral("download_location"), *options.savePath);
    if (options.stopped)
        common.insert(QStringLiteral("add_paused"), *options.stopped);
    if (options.skipChecking)
        common.insert(QStringLiteral("seed_mode"), *options.skipChecking);
    if (options.rename && !options.rename->isEmpty())
        common.insert(QStringLiteral("name"), *options.rename);
    if (options.sequentialDownload)
        common.insert(QStringLiteral("sequential_download"), *options.sequentialDownload);
    if (options.firstLastPiece)
        common.insert(QStringLiteral("prioritize_first_last_pieces"), *options.firstLastPiece);
    if (options.dlLimit)
        common.insert(QStringLiteral("max_download_speed"), DT::speedKiB(*options.dlLimit));
    if (options.upLimit)
        common.insert(QStringLiteral("max_upload_speed"), DT::speedKiB(*options.upLimit));
    if (!options.filePriorities.isEmpty()) {
        QJsonArray priorities;
        for (const FilePriority priority : options.filePriorities)
            priorities.append(DT::delugePriority(priority));
        common.insert(QStringLiteral("file_priorities"), priorities);
    }

    QList<Reply<QJsonValue> *> sent;
    for (const auto &[method, params] : calls) {
        QJsonArray withOptions = params;
        withOptions.append(common);
        sent.append(m_rpc->call(method, withOptions));
    }
    auto *added = mapReply<AddResult>(all(sent), this, [](const Results &results) {
        AddResult result;
        int duplicates = 0;
        for (const Result<QJsonValue> &answer : results) {
            if (answer.isError()) {
                if (isDuplicate(answer.error())) {
                    ++duplicates;
                    continue;
                }
                return Result<AddResult>(answer.error());
            }
            const QString id = answer.value().toString().toLower();
            if (!id.isEmpty())
                result.addedIds.append(id);
        }
        // Every one already there: the server took the request and added nothing.
        result.refused = result.addedIds.isEmpty() && duplicates > 0;
        return Result<AddResult>(result);
    });
    if (label.isEmpty())
        return added;
    return then<AddResult, AddResult>(added, [this, label](const AddResult &result) {
        if (result.addedIds.isEmpty())
            return settled<AddResult>(result);
        QList<Reply<QJsonValue> *> labels;
        for (const QString &id : result.addedIds)
            labels.append(m_rpc->call(QStringLiteral("label.set_torrent"), {id, label}));
        return mapReply<AddResult>(
            doneAll(all(labels)), this, [result](const Done &) { return result; });
    });
}

Reply<AddResult> *DelugeEngine::addUrls(const QStringList &urls, const AddOptions &options)
{
    return addMany({}, urls, options);
}

Reply<AddResult> *
DelugeEngine::addFile(const QString &fileName, const QByteArray &bytes, const AddOptions &options)
{
    return addMany({AddFile{fileName, bytes}}, {}, options);
}

Reply<AddResult> *DelugeEngine::addMany(const QList<AddFile> &files,
                                        const QStringList &urls,
                                        const AddOptions &options)
{
    QList<QPair<QString, QJsonArray>> calls;
    for (const AddFile &file : files)
        calls.append({QStringLiteral("core.add_torrent_file"),
                      {file.fileName, QString::fromLatin1(file.bytes.toBase64())}});
    for (const QString &url : urls) {
        const bool magnet = url.trimmed().startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive);
        calls.append({magnet ? QStringLiteral("core.add_torrent_magnet")
                             : QStringLiteral("core.add_torrent_url"),
                      {url.trimmed()}});
    }
    return add(calls, options);
}

} // namespace omaseed::core
