#include "core/MoveJob.hpp"

#include "core/Paths.hpp"
#include "core/SharedData.hpp"
#include "core/TorrentModel.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QSaveFile>

#include <algorithm>

namespace omaseed::core {

namespace {

constexpr int kFileVersion = 1;
/// A torrent sent, not moving and not at its target on this many polls in a row, this long
/// after it was sent, is no longer with the server (a restart emptied its queue): sent again.
/// Two polls, so the log's failure line (read after each poll) comes first when there is one.
constexpr int kLostPolls = 2;
constexpr qint64 kLostAfterSeconds = 10;
constexpr int kMaxSends = 3;
/// M14: stuck after 4 × the expected time, and never before 10 min.
constexpr qint64 kStuckFactor = 4;
constexpr qint64 kStuckFloorSeconds = 600;
/// The speed is read over the last 10 min of landings.
constexpr qint64 kSpeedWindowSeconds = 600;
/// M15: errors and missing files are tied to the job for 24 h after its end.
constexpr qint64 kAfterSeconds = 24 * 3600;

template<class E>
QString keyOf(E value)
{
    return QString::fromLatin1(QMetaEnum::fromType<E>().valueToKey(int(value)));
}

template<class E>
E valueOf(const QJsonValue &json, E fallback)
{
    bool ok = false;
    const int value = QMetaEnum::fromType<E>().keyToValue(json.toString().toLatin1(), &ok);
    return ok ? E(value) : fallback;
}

QString dateText(const QDateTime &at)
{
    return at.isValid() ? at.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime dateOf(const QJsonValue &json)
{
    return QDateTime::fromString(json.toString(), Qt::ISODateWithMs);
}

bool withServer(MoveJob::Step step)
{
    return step == MoveJob::Step::Queued || step == MoveJob::Step::Moving;
}

bool sendable(MoveJob::Step step)
{
    return step != MoveJob::Step::LeftOut && step != MoveJob::Step::AlreadyThere;
}

QString kindKey(MovePlan::Item::Kind kind)
{
    switch (kind) {
    case MovePlan::Item::Kind::Move:
    case MovePlan::Item::Kind::PulledIn:
        return QStringLiteral("move");
    case MovePlan::Item::Kind::AlreadyThere:
        return QStringLiteral("alreadyThere");
    case MovePlan::Item::Kind::LeftOut:
    case MovePlan::Item::Kind::Spans:
    case MovePlan::Item::Kind::MovedSince:
    case MovePlan::Item::Kind::IntoItself:
        return QStringLiteral("leftOut");
    }
    return QStringLiteral("leftOut");
}

} // namespace

MoveJob::MoveJob(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &MoveJob::save);
    // L6: a quiet job shows once it has run 3 s.
    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(3000);
    connect(&m_showTimer, &QTimer::timeout, this, [this]() {
        if (m_shown || !isActive())
            return;
        m_shown = true;
        touch();
    });
    // L10: every 2 s while a torrent moves.
    m_liveTimer.setInterval(2000);
    connect(&m_liveTimer, &QTimer::timeout, this, &MoveJob::sampleLive);
}

QString MoveJob::defaultFilePath(const QString &serverId)
{
    return paths::configDir() + QStringLiteral("/moves/") + serverId + QStringLiteral(".json");
}

void MoveJob::setTorrents(TorrentModel *torrents)
{
    m_torrents = torrents;
}

QDateTime MoveJob::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

MoveJob::Entry *MoveJob::find(const QString &hash)
{
    for (Entry &entry : m_entries) {
        if (entry.hash == hash)
            return &entry;
    }
    return nullptr;
}

bool MoveJob::isRunningNow(const QString &hash) const
{
    if (m_torrents.isNull() || m_torrents->indexOfHash(hash) < 0)
        return false;
    switch (m_torrents->torrent(hash).state) {
    case TorrentState::StoppedDownload:
    case TorrentState::StoppedUpload:
    case TorrentState::PausedDownload:
    case TorrentState::PausedUpload:
    case TorrentState::Error:
    case TorrentState::MissingFiles:
    case TorrentState::Unknown:
        return false;
    default:
        return true;
    }
}

const MoveJob::Entry *MoveJob::entry(const QString &hash) const
{
    for (const Entry &one : m_entries) {
        if (one.hash == hash)
            return &one;
    }
    return nullptr;
}

bool MoveJob::isActive() const
{
    switch (m_state) {
    case State::Running:
    case State::Pausing:
    case State::Paused:
    case State::Ending:
    case State::Carrying:
        return true;
    default:
        return false;
    }
}

QJsonObject
MoveJob::planJson(const MovePlan &plan, bool carryLoose, qint64 looseBytes, const QJsonObject &spec)
{
    QJsonArray items;
    for (const MovePlan::Item &item : plan.items()) {
        QJsonObject json{{QStringLiteral("hash"), item.hash},
                         {QStringLiteral("name"), item.name},
                         {QStringLiteral("from"), item.from},
                         {QStringLiteral("to"), item.to},
                         {QStringLiteral("bytes"), item.bytes},
                         {QStringLiteral("kind"), kindKey(item.kind)},
                         {QStringLiteral("why"), item.why}};
        if (item.autoTmm)
            json.insert(QStringLiteral("autoTmm"), true);
        items.append(json);
    }
    QJsonObject json{{QStringLiteral("root"), plan.root()},
                     {QStringLiteral("newPath"), plan.newPath()},
                     {QStringLiteral("sameDrive"), plan.sameDrive()},
                     {QStringLiteral("carryLoose"), carryLoose},
                     {QStringLiteral("looseBytes"), looseBytes},
                     {QStringLiteral("items"), items}};
    QJsonObject withKind = spec;
    if (!withKind.contains(QStringLiteral("kind")))
        withKind.insert(QStringLiteral("kind"),
                        plan.isSelection() ? QStringLiteral("selection")
                                           : QStringLiteral("folder"));
    json.insert(QStringLiteral("spec"), withKind);
    return json;
}

bool MoveJob::start(const MovePlan &plan,
                    const QString &serverId,
                    bool carryLoose,
                    qint64 looseBytes,
                    const QJsonObject &spec)
{
    if (!plan.canMove())
        return false;
    const QJsonObject json = planJson(plan, carryLoose, looseBytes, spec);
    if (m_state != State::Idle) {
        // M18: one at a time; this one waits behind the running one (or its summary).
        m_queued.append(json);
        touch();
        return false;
    }
    m_serverId = serverId;
    startPlan(json);
    return true;
}

void MoveJob::startPlan(const QJsonObject &plan)
{
    m_spec = plan.value(QStringLiteral("spec")).toObject();
    m_root = plan.value(QStringLiteral("root")).toString();
    m_newPath = plan.value(QStringLiteral("newPath")).toString();
    m_sameDrive = plan.value(QStringLiteral("sameDrive")).toBool();
    m_carryLoose = plan.value(QStringLiteral("carryLoose")).toBool();
    m_looseBytes = plan.value(QStringLiteral("looseBytes")).toInteger();
    m_entries.clear();
    for (const QJsonValue &value : plan.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        Entry entry;
        entry.hash = item.value(QStringLiteral("hash")).toString();
        entry.name = item.value(QStringLiteral("name")).toString();
        entry.from = item.value(QStringLiteral("from")).toString();
        entry.to = item.value(QStringLiteral("to")).toString();
        entry.bytes = item.value(QStringLiteral("bytes")).toInteger();
        entry.why = item.value(QStringLiteral("why")).toString();
        entry.autoTmm = item.value(QStringLiteral("autoTmm")).toBool();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        entry.step = kind == QLatin1String("move")           ? Step::Waiting
                     : kind == QLatin1String("alreadyThere") ? Step::AlreadyThere
                                                             : Step::LeftOut;
        m_entries << entry;
    }
    const QDateTime at = now();
    m_startedAt = at;
    m_runningSince = at;
    m_progressAt = at;
    m_endedAt = QDateTime();
    m_lastLandedAt = QDateTime();
    m_probeDone = false;
    m_pauseReason = PauseReason::None;
    m_carried = -1;
    m_stayed = -1;
    m_landedWhileAway = 0;
    m_secondsAway = 0;
    m_nearlyFullHash.clear();
    m_liveHash.clear();
    m_liveBytes = -1;
    m_liveFirstBytes = -1;
    m_liveFirstAt = QDateTime();
    m_shown = !isQuiet();
    if (!m_shown)
        m_showTimer.start();
    setState(State::Running);
    pump();
    touch();
}

void MoveJob::startNext()
{
    while (!m_queued.isEmpty()) {
        QJsonObject next = m_queued.takeFirst();
        const QJsonObject spec = next.value(QStringLiteral("spec")).toObject();
        // L12: checked again now, against the library as it is.
        if (m_replan && !spec.isEmpty()) {
            const QJsonObject fresh = m_replan(spec);
            if (fresh.isEmpty()) {
                Q_EMIT queuedDropped(spec.value(QStringLiteral("title")).toString());
                continue;
            }
            next = fresh;
        }
        startPlan(next);
        return;
    }
    touch();
}

QString MoveJob::kind() const
{
    const QString kind = m_spec.value(QStringLiteral("kind")).toString();
    return kind.isEmpty() ? QStringLiteral("folder") : kind;
}

QString MoveJob::title() const
{
    const QString title = m_spec.value(QStringLiteral("title")).toString();
    if (!title.isEmpty())
        return title;
    const QString root = SharedData::normalize(m_root);
    const qsizetype slash = root.lastIndexOf(QLatin1Char('/'));
    return slash < 0 || root == QLatin1String("/") ? root : root.mid(slash + 1);
}

QStringList MoveJob::queuedTitles() const
{
    QStringList titles;
    for (const QJsonObject &plan : m_queued) {
        QString title =
            plan.value(QStringLiteral("spec")).toObject().value(QStringLiteral("title")).toString();
        if (title.isEmpty()) {
            const QString root = plan.value(QStringLiteral("root")).toString();
            title = root.mid(root.lastIndexOf(QLatin1Char('/')) + 1);
        }
        titles << title;
    }
    return titles;
}

QList<QJsonObject> MoveJob::queuedSpecs() const
{
    QList<QJsonObject> specs;
    for (const QJsonObject &plan : m_queued) {
        QJsonObject spec = plan.value(QStringLiteral("spec")).toObject();
        spec.insert(QStringLiteral("newPath"), plan.value(QStringLiteral("newPath")));
        specs << spec;
    }
    return specs;
}

QStringList MoveJob::queuedRoots() const
{
    QStringList roots;
    for (const QJsonObject &plan : m_queued)
        roots << plan.value(QStringLiteral("root")).toString();
    return roots;
}

void MoveJob::dropQueued(int index)
{
    if (index < 0 || index >= m_queued.size())
        return;
    m_queued.removeAt(index);
    touch();
}

void MoveJob::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    // L6: a pause asks something of someone; a quiet job shows from then on.
    if (state == State::Paused || state == State::Pausing || state == State::Ending)
        m_shown = true;
    updateLiveTimer();
    Q_EMIT stateChanged();
}

void MoveJob::updateLiveTimer()
{
    const bool moving =
        m_state == State::Running || m_state == State::Pausing || m_state == State::Ending;
    if (moving && m_growthProbe && !m_liveTimer.isActive())
        m_liveTimer.start();
    else if ((!moving || !m_growthProbe) && m_liveTimer.isActive())
        m_liveTimer.stop();
}

void MoveJob::sampleLive()
{
    const Entry *moving = nullptr;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.step == Step::Moving) {
            moving = &entry;
            break;
        }
    }
    if (moving == nullptr || !m_growthProbe) {
        if (!m_liveHash.isEmpty()) {
            m_liveHash.clear();
            m_liveBytes = -1;
            Q_EMIT changed();
        }
        return;
    }
    const qint64 bytes = m_growthProbe(moving->hash, moving->to);
    const QDateTime at = now();
    if (bytes < 0) {
        const bool was = m_liveBytes >= 0 || m_liveHash != moving->hash;
        m_liveHash = moving->hash;
        m_liveBytes = -1;
        if (was)
            Q_EMIT changed();
        return;
    }
    if (m_liveHash != moving->hash || m_liveBytes < 0) {
        m_liveHash = moving->hash;
        m_liveBytes = bytes;
        m_liveFirstBytes = bytes;
        m_liveFirstAt = at;
        Q_EMIT changed();
    } else if (bytes != m_liveBytes) {
        // M14: a file growing at the destination is progress.
        m_liveBytes = bytes;
        m_progressAt = at;
        Q_EMIT changed();
    }
}

qint64 MoveJob::liveBytes(const QString &hash) const
{
    if (hash.isEmpty() || hash != m_liveHash || m_liveBytes < 0)
        return -1;
    const Entry *one = entry(hash);
    if (one == nullptr || one->step != Step::Moving)
        return -1;
    return std::min(m_liveBytes, one->bytes);
}

QHash<QString, QString> MoveJob::movedFrom() const
{
    QHash<QString, QString> out;
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moved)
            out.insert(entry.hash, entry.from);
    }
    return out;
}

QHash<QString, QString> MoveJob::movedTo() const
{
    QHash<QString, QString> out;
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moved)
            out.insert(entry.hash, entry.to);
    }
    return out;
}

QStringList MoveJob::hadAutoTmm() const
{
    QStringList out;
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moved && entry.autoTmm)
            out << entry.hash;
    }
    return out;
}

void MoveJob::pause(PauseReason reason)
{
    if (m_state != State::Running)
        return;
    m_pauseReason = reason;
    setState(withServer() > 0 && reason != PauseReason::Stuck ? State::Pausing : State::Paused);
    touch();
}

void MoveJob::resume()
{
    if (m_state != State::Paused && m_state != State::Pausing)
        return;
    m_pauseReason = PauseReason::None;
    m_nearlyFullHash.clear();
    const QDateTime at = now();
    m_runningSince = at;
    m_progressAt = at;
    setState(State::Running);
    pump();
    settle();
    touch();
}

void MoveJob::end()
{
    if (m_state != State::Running && m_state != State::Pausing && m_state != State::Paused)
        return;
    // Stuck: what is with the server may never land; the job ends now, and a late landing
    // still counts on the next polls.
    if (withServer() == 0 || m_pauseReason == PauseReason::Stuck) {
        m_endedAt = now();
        setState(State::Ended);
        touch();
        Q_EMIT finished();
        return;
    }
    setState(State::Ending);
    touch();
}

void MoveJob::keepWaiting()
{
    if (m_state != State::Paused || m_pauseReason != PauseReason::Stuck)
        return;
    resume();
}

void MoveJob::finishCarry(int carried, int stayed)
{
    if (m_state != State::Carrying)
        return;
    m_carried = carried;
    m_stayed = stayed;
    m_endedAt = now();
    setState(State::Done);
    touch();
    Q_EMIT finished();
}

void MoveJob::closeSummary()
{
    if (m_state != State::Done && m_state != State::Ended && m_state != State::Idle)
        return;
    reset();
    startNext();
}

void MoveJob::reset()
{
    m_showTimer.stop();
    m_spec = QJsonObject();
    m_shown = false;
    m_liveHash.clear();
    m_liveBytes = -1;
    m_entries.clear();
    m_root.clear();
    m_newPath.clear();
    m_pauseReason = PauseReason::None;
    m_startedAt = QDateTime();
    m_endedAt = QDateTime();
    m_lastLandedAt = QDateTime();
    setState(State::Idle);
}

void MoveJob::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    // T4: a stuck queue is usually freed by restarting qBittorrent; when it is back, what
    // hadn't landed is sent again.
    if (connected && m_state == State::Paused && m_pauseReason == PauseReason::Stuck)
        resume();
    pump();
    Q_EMIT changed();
}

void MoveJob::pump()
{
    if (m_state != State::Running || !m_connected)
        return;
    int with = withServer();
    const int limit = m_probeDone ? m_ahead : 1;
    qint64 pending = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (::omaseed::core::withServer(entry.step))
            pending += entry.bytes;
    }
    for (Entry &entry : m_entries) {
        if (with >= limit || m_state != State::Running)
            break;
        if (entry.step != Step::Waiting)
            continue;
        if (!m_sameDrive && m_roomProbe) {
            const qint64 free = m_roomProbe(entry.to);
            if (free >= 0 && free - pending - entry.bytes < m_lowDiskThreshold) {
                m_nearlyFullHash = entry.hash;
                m_nearlyFullFreeAfter = free - pending - entry.bytes;
                pause(PauseReason::NearlyFull);
                Q_EMIT pausedItself(PauseReason::NearlyFull);
                return;
            }
        }
        // C20 (#138): whether it was running, as the job first sends it; a later send (after a
        // restart or a stuck move) finds it in whatever state the move left it.
        if (entry.sends == 0)
            entry.wasRunning = isRunningNow(entry.hash);
        entry.step = Step::Queued;
        entry.sentAt = now();
        entry.unsure = false;
        entry.quietPolls = 0;
        ++entry.sends;
        ++with;
        pending += entry.bytes;
        touch();
        const QString hash = entry.hash;
        const QString to = entry.to;
        Q_EMIT sendRequested({hash}, to);
        // The handler may have answered at once and changed the list: start over.
        pump();
        return;
    }
}

void MoveJob::onSendResult(const QStringList &hashes, const ApiError &error)
{
    if (!error.isError())
        return;
    bool refused = false;
    bool wasProbe = !m_probeDone;
    for (const QString &hash : hashes) {
        Entry *entry = find(hash);
        if (entry == nullptr || !::omaseed::core::withServer(entry->step))
            continue;
        switch (error.kind()) {
        case ApiError::Kind::Network:
        case ApiError::Kind::Timeout:
        case ApiError::Kind::Cancelled:
        case ApiError::Kind::Server:
            // No answer, or none to trust: the next polls tell whether it was taken.
            entry->unsure = true;
            break;
        case ApiError::Kind::NotFound:
            entry->step = Step::Removed;
            break;
        default:
            entry->step = Step::Failed;
            entry->reason = error.message();
            entry->causeKey =
                error.kind() == ApiError::Kind::Conflict    ? QStringLiteral("cantCreate")
                : error.kind() == ApiError::Kind::Forbidden ? QStringLiteral("noAccess")
                                                            : QStringLiteral("refused");
            refused = refused || error.kind() == ApiError::Kind::Conflict ||
                      error.kind() == ApiError::Kind::Forbidden;
            break;
        }
    }
    // Nothing left to send: there is nothing to hold back, so the job just ends (C16: a quick
    // move's failure is told in its line).
    if ((refused || (wasProbe && failed() > 0)) && m_state == State::Running && waiting() > 0) {
        const PauseReason reason = wasProbe ? PauseReason::FirstFailed : PauseReason::Refused;
        pause(reason);
        Q_EMIT pausedItself(reason);
    }
    settle();
    pump();
    touch();
}

void MoveJob::onFailed(const QString &hash,
                       const QDateTime &at,
                       const QString &reason,
                       const QString &causeKey,
                       const QString &line)
{
    Entry *entry = find(hash);
    if (entry == nullptr || !::omaseed::core::withServer(entry->step))
        return;
    // A line from before this send is about an earlier one.
    if (entry->sentAt.isValid() && at.isValid() && at < entry->sentAt.addSecs(-2))
        return;
    entry->step = Step::Failed;
    entry->reason = reason;
    entry->causeKey = causeKey;
    entry->line = line;
    entry->unsure = false;
    if (m_state == State::Running || m_state == State::Pausing) {
        PauseReason why = PauseReason::None;
        if (!m_probeDone)
            why = PauseReason::FirstFailed;
        else if (causeKey == QLatin1String("diskFull"))
            why = PauseReason::NoSpace;
        // The first failing with nothing left to send just ends the job; a full disk always
        // says so.
        if (why != PauseReason::None &&
            (why != PauseReason::FirstFailed || m_state == State::Pausing || waiting() > 0)) {
            if (m_state == State::Running)
                pause(why);
            else
                m_pauseReason = why;
            Q_EMIT pausedItself(why);
        }
    }
    settle();
    pump();
    touch();
}

void MoveJob::landed(Entry &entry)
{
    const QDateTime at = now();
    entry.step = Step::Moved;
    entry.landedAt = at;
    entry.unsure = false;
    m_lastLandedAt = at;
    m_progressAt = at;
    m_probeDone = true;
}

void MoveJob::onSynced(bool fullUpdate)
{
    if (m_torrents.isNull() || m_state == State::Idle)
        return;
    const QDateTime at = now();
    const bool after = m_state == State::Done || m_state == State::Ended;
    if (after && m_endedAt.isValid() && m_endedAt.secsTo(at) > kAfterSeconds && !m_restored)
        return;

    int landedNow = 0;
    bool dirty = false;
    for (Entry &entry : m_entries) {
        if (!sendable(entry.step) || entry.step == Step::Removed)
            continue;
        const int row = m_torrents->indexOfHash(entry.hash);
        if (row < 0) {
            if (entry.step != Step::Moved) {
                entry.step = Step::Removed;
                dirty = true;
            }
            continue;
        }
        const Torrent &torrent = m_torrents->torrent(entry.hash);
        const QString save = SharedData::normalize(torrent.savePath);
        const bool moving = torrent.state == TorrentState::Moving;
        if (entry.step == Step::Moved) {
            // M15: what goes wrong with it in the next 24 h belongs to the job's record.
            if (entry.later.isEmpty() && (torrent.state == TorrentState::Error ||
                                          torrent.state == TorrentState::MissingFiles)) {
                entry.later = torrent.state == TorrentState::Error ? QStringLiteral("error")
                                                                   : QStringLiteral("missingFiles");
                dirty = true;
            }
            continue;
        }
        if (save == entry.to && !moving) {
            landed(entry);
            ++landedNow;
            dirty = true;
            continue;
        }
        if (::omaseed::core::withServer(entry.step)) {
            if (moving) {
                entry.unsure = false;
                entry.quietPolls = 0;
                continue;
            }
            if (save != entry.from) {
                entry.step = Step::Elsewhere;
                dirty = true;
                continue;
            }
            // Not moving, not landed, no failure heard: is it still with the server? Not
            // after a restart (a full update), nor after kLostPolls quiet polls.
            if (!fullUpdate &&
                (entry.sentAt.secsTo(at) < kLostAfterSeconds || ++entry.quietPolls < kLostPolls))
                continue;
            entry.quietPolls = 0;
            if (entry.sends >= kMaxSends) {
                entry.step = Step::Failed;
                entry.reason = tr("qBittorrent didn't take it");
                entry.causeKey = QStringLiteral("notTaken");
            } else {
                entry.step = Step::Waiting;
            }
            dirty = true;
            continue;
        }
        if (entry.step == Step::Waiting && !moving && save != entry.from) {
            entry.step = Step::Elsewhere; // its category moved it, or someone else did
            dirty = true;
        }
    }

    // The one moving now: the first sent that hasn't landed and reads moving.
    const Entry *first = nullptr;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!::omaseed::core::withServer(entry.step))
            continue;
        const int row = m_torrents->indexOfHash(entry.hash);
        const bool moving =
            row >= 0 && m_torrents->torrent(entry.hash).state == TorrentState::Moving;
        if (moving && (first == nullptr || entry.sentAt < first->sentAt))
            first = &entry;
    }
    for (Entry &entry : m_entries) {
        if (!::omaseed::core::withServer(entry.step))
            continue;
        const Step step = &entry == first ? Step::Moving : Step::Queued;
        if (step == Step::Moving && entry.step != Step::Moving)
            entry.movingSince = at;
        if (entry.step != step) {
            entry.step = step;
            dirty = true;
        }
    }

    if (m_restored) {
        m_restored = false;
        m_landedWhileAway = landedNow;
        m_secondsAway = m_savedAt.isValid() ? std::max<qint64>(0, m_savedAt.secsTo(at)) : 0;
        dirty = true;
    }
    if (fullUpdate && m_state == State::Paused && m_pauseReason == PauseReason::Stuck)
        resume();

    checkStuck();
    settle();
    pump();
    if (dirty)
        touch();
    else
        Q_EMIT changed();
}

void MoveJob::checkStuck()
{
    if (m_state != State::Running || withServer() == 0)
        return;
    const Entry *now_ = nullptr;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.step == Step::Moving) {
            now_ = &entry;
            break;
        }
    }
    if (now_ == nullptr)
        return;
    const QDateTime at = now();
    sampleLive();
    QDateTime since = m_progressAt;
    if (now_->movingSince.isValid() && now_->movingSince > since)
        since = now_->movingSince;
    const qint64 limit = std::max(kStuckFloorSeconds, kStuckFactor * expected().count());
    if (since.isValid() && since.secsTo(at) > limit) {
        pause(PauseReason::Stuck);
        Q_EMIT pausedItself(PauseReason::Stuck);
    }
}

void MoveJob::settle()
{
    const int with = withServer();
    if (m_state == State::Pausing && with == 0) {
        setState(State::Paused);
    } else if (m_state == State::Ending && with == 0) {
        m_endedAt = now();
        setState(State::Ended);
        Q_EMIT finished();
    } else if (m_state == State::Running && with == 0 && waiting() == 0) {
        finishTorrents();
    }
}

void MoveJob::finishTorrents()
{
    if (m_carryLoose) {
        setState(State::Carrying);
        touch();
        Q_EMIT carryWanted();
        return;
    }
    m_endedAt = now();
    setState(State::Done);
    touch();
    Q_EMIT finished();
}

QStringList MoveJob::hashes() const
{
    QStringList out;
    for (const Entry &entry : m_entries) {
        if (sendable(entry.step) && entry.step != Step::Removed)
            out << entry.hash;
    }
    return out;
}

int MoveJob::count(Step step) const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [step](const Entry &entry) {
        return entry.step == step;
    }));
}

int MoveJob::total() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &entry) {
        return sendable(entry.step);
    }));
}

qint64 MoveJob::bytesTotal() const
{
    qint64 bytes = 0;
    for (const Entry &entry : m_entries) {
        if (sendable(entry.step) && entry.step != Step::Removed && entry.step != Step::Elsewhere)
            bytes += entry.bytes;
    }
    return bytes;
}

qint64 MoveJob::bytesMoved() const
{
    qint64 bytes = 0;
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moved)
            bytes += entry.bytes;
    }
    // L10: and what of the moving one is already at its target.
    return bytes + std::max<qint64>(0, liveBytes(m_liveHash));
}

int MoveJob::percent() const
{
    const qint64 all = bytesTotal();
    if (all <= 0)
        return total() > 0 ? moved() * 100 / total() : 0;
    return int(std::clamp<qint64>(bytesMoved() * 100 / all, 0, 100));
}

qint64 MoveJob::speed() const
{
    const QDateTime at = now();
    QDateTime from = at.addSecs(-kSpeedWindowSeconds);
    if (m_runningSince.isValid() && m_runningSince > from)
        from = m_runningSince;
    const qint64 span = from.secsTo(at);
    if (span <= 0)
        return 0;
    qint64 bytes = 0;
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moved && entry.landedAt.isValid() && entry.landedAt >= from)
            bytes += entry.bytes;
    }
    // L10: the moving one's growth at its target, from its first reading.
    const qint64 grown = liveBytes(m_liveHash) >= 0 && m_liveFirstAt.isValid()
                             ? std::max<qint64>(0, m_liveBytes - m_liveFirstBytes)
                             : 0;
    if (bytes == 0 && grown > 0) {
        const qint64 since = m_liveFirstAt.secsTo(at);
        return since > 0 ? grown / since : 0;
    }
    return (bytes + grown) / span;
}

qint64 MoveJob::secondsLeft() const
{
    if (m_sameDrive)
        return -1;
    const qint64 rate = speed();
    if (rate <= 0)
        return -1;
    return (bytesTotal() - bytesMoved()) / rate;
}

std::chrono::seconds MoveJob::expected() const
{
    const qint64 rate = speed();
    if (rate <= 0)
        return std::chrono::seconds(0);
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moving)
            return std::chrono::seconds(entry.bytes / rate);
    }
    return std::chrono::seconds(0);
}

QString MoveJob::current() const
{
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Moving)
            return entry.hash;
    }
    return {};
}

MoveJob::Summary MoveJob::summary() const
{
    Summary summary;
    summary.kind = kind();
    summary.title = title();
    summary.root = m_root;
    summary.newPath = m_newPath;
    summary.total = total();
    summary.moved = moved();
    summary.failed = failed();
    summary.leftOut = count(Step::LeftOut);
    summary.alreadyThere = count(Step::AlreadyThere);
    summary.removed = count(Step::Removed);
    summary.elsewhere = count(Step::Elsewhere);
    summary.notMoved = waiting() + withServer();
    summary.bytesMoved = bytesMoved();
    summary.startedAt = m_startedAt;
    summary.endedAt = m_endedAt;
    summary.endedEarly = m_state == State::Ended;
    for (const Entry &entry : m_entries) {
        if (entry.step == Step::Failed)
            summary.failedHashes << entry.hash;
    }
    return summary;
}

QStringList MoveJob::addedDuring() const
{
    QStringList out;
    if (m_torrents.isNull() || m_root.isEmpty())
        return out;
    QSet<QString> known;
    for (const Entry &entry : m_entries)
        known.insert(entry.hash);
    for (int row = 0; row < m_torrents->count(); ++row) {
        const Torrent &torrent = m_torrents->torrentAt(row);
        if (!known.contains(torrent.hash) && SharedData::isUnder(torrent.savePath, m_root))
            out << torrent.hash;
    }
    return out;
}

// ---- the file ---------------------------------------------------------------------------

void MoveJob::touch()
{
    Q_EMIT changed();
    if (!m_filePath.isEmpty() && !m_saveTimer.isActive())
        m_saveTimer.start();
}

void MoveJob::saveNow()
{
    m_saveTimer.stop();
    save();
}

void MoveJob::save()
{
    if (m_filePath.isEmpty())
        return;
    if (m_state == State::Idle && m_queued.isEmpty()) {
        QFile::remove(m_filePath);
        return;
    }
    QDir().mkpath(QFileInfo(m_filePath).path());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    file.commit();
}

QJsonObject MoveJob::toJson() const
{
    QJsonArray entries;
    for (const Entry &entry : m_entries) {
        QJsonObject json{{QStringLiteral("hash"), entry.hash},
                         {QStringLiteral("name"), entry.name},
                         {QStringLiteral("from"), entry.from},
                         {QStringLiteral("to"), entry.to},
                         {QStringLiteral("bytes"), entry.bytes},
                         {QStringLiteral("step"), keyOf(entry.step)},
                         {QStringLiteral("sends"), entry.sends}};
        if (entry.autoTmm)
            json.insert(QStringLiteral("autoTmm"), true);
        if (entry.wasRunning)
            json.insert(QStringLiteral("wasRunning"), true);
        const auto put = [&json](const char *key, const QString &value) {
            if (!value.isEmpty())
                json.insert(QLatin1String(key), value);
        };
        put("reason", entry.reason);
        put("causeKey", entry.causeKey);
        put("line", entry.line);
        put("why", entry.why);
        put("later", entry.later);
        put("sentAt", dateText(entry.sentAt));
        put("movingSince", dateText(entry.movingSince));
        put("landedAt", dateText(entry.landedAt));
        entries.append(json);
    }
    QJsonArray queued;
    for (const QJsonObject &plan : m_queued)
        queued.append(plan);
    return {{QStringLiteral("version"), kFileVersion},
            {QStringLiteral("server"), m_serverId},
            {QStringLiteral("spec"), m_spec},
            {QStringLiteral("shown"), m_shown},
            {QStringLiteral("state"), keyOf(m_state)},
            {QStringLiteral("pauseReason"), keyOf(m_pauseReason)},
            {QStringLiteral("root"), m_root},
            {QStringLiteral("newPath"), m_newPath},
            {QStringLiteral("sameDrive"), m_sameDrive},
            {QStringLiteral("carryLoose"), m_carryLoose},
            {QStringLiteral("looseBytes"), m_looseBytes},
            {QStringLiteral("carried"), m_carried},
            {QStringLiteral("stayed"), m_stayed},
            {QStringLiteral("probeDone"), m_probeDone},
            {QStringLiteral("startedAt"), dateText(m_startedAt)},
            {QStringLiteral("endedAt"), dateText(m_endedAt)},
            {QStringLiteral("lastLandedAt"), dateText(m_lastLandedAt)},
            {QStringLiteral("savedAt"), dateText(now())},
            {QStringLiteral("entries"), entries},
            {QStringLiteral("queued"), queued}};
}

bool MoveJob::fromJson(const QJsonObject &json)
{
    if (json.value(QStringLiteral("version")).toInt() != kFileVersion)
        return false;
    const State state = valueOf(json.value(QStringLiteral("state")), State::Idle);
    m_queued.clear();
    for (const QJsonValue &plan : json.value(QStringLiteral("queued")).toArray())
        m_queued << plan.toObject();
    if (state == State::Idle && m_queued.isEmpty())
        return false;

    m_serverId = json.value(QStringLiteral("server")).toString();
    m_spec = json.value(QStringLiteral("spec")).toObject();
    m_root = json.value(QStringLiteral("root")).toString();
    m_newPath = json.value(QStringLiteral("newPath")).toString();
    m_sameDrive = json.value(QStringLiteral("sameDrive")).toBool();
    m_carryLoose = json.value(QStringLiteral("carryLoose")).toBool();
    m_looseBytes = json.value(QStringLiteral("looseBytes")).toInteger();
    m_carried = json.value(QStringLiteral("carried")).toInt(-1);
    m_stayed = json.value(QStringLiteral("stayed")).toInt(-1);
    m_probeDone = json.value(QStringLiteral("probeDone")).toBool();
    m_startedAt = dateOf(json.value(QStringLiteral("startedAt")));
    m_endedAt = dateOf(json.value(QStringLiteral("endedAt")));
    m_lastLandedAt = dateOf(json.value(QStringLiteral("lastLandedAt")));
    m_savedAt = dateOf(json.value(QStringLiteral("savedAt")));
    m_pauseReason = valueOf(json.value(QStringLiteral("pauseReason")), PauseReason::None);
    m_entries.clear();
    for (const QJsonValue &value : json.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject item = value.toObject();
        Entry entry;
        entry.hash = item.value(QStringLiteral("hash")).toString();
        entry.name = item.value(QStringLiteral("name")).toString();
        entry.from = item.value(QStringLiteral("from")).toString();
        entry.to = item.value(QStringLiteral("to")).toString();
        entry.bytes = item.value(QStringLiteral("bytes")).toInteger();
        entry.step = valueOf(item.value(QStringLiteral("step")), Step::Waiting);
        entry.sends = item.value(QStringLiteral("sends")).toInt();
        entry.reason = item.value(QStringLiteral("reason")).toString();
        entry.causeKey = item.value(QStringLiteral("causeKey")).toString();
        entry.line = item.value(QStringLiteral("line")).toString();
        entry.why = item.value(QStringLiteral("why")).toString();
        entry.later = item.value(QStringLiteral("later")).toString();
        entry.autoTmm = item.value(QStringLiteral("autoTmm")).toBool();
        entry.wasRunning = item.value(QStringLiteral("wasRunning")).toBool();
        entry.sentAt = dateOf(item.value(QStringLiteral("sentAt")));
        entry.movingSince = dateOf(item.value(QStringLiteral("movingSince")));
        entry.landedAt = dateOf(item.value(QStringLiteral("landedAt")));
        m_entries << entry;
    }

    State next = state;
    if (state == State::Pausing)
        next = State::Paused; // what was with the server finished, or the polls will say
    // J5: "pause and quit" means the rest wait until omaseed runs again; it runs again.
    if (next == State::Paused && m_pauseReason == PauseReason::Quit) {
        next = State::Running;
        m_pauseReason = PauseReason::None;
    }
    const QDateTime at = now();
    m_runningSince = at;
    m_progressAt = at; // a long absence is not "stuck"
    m_restored = true;
    m_state = next;
    // L6: a job still going after a restart has been at it a while: it shows. A quiet one
    // that had ended unseen stays unseen.
    m_shown = json.value(QStringLiteral("shown")).toBool() || isActive();
    updateLiveTimer();
    Q_EMIT stateChanged();
    Q_EMIT changed();
    if (m_state == State::Idle && !m_queued.isEmpty())
        startNext();
    return true;
}

bool MoveJob::restore()
{
    if (m_filePath.isEmpty())
        return false;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() && fromJson(document.object());
}

} // namespace omaseed::core
