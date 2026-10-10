#include "core/SeedLedger.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

#include <cmath>

namespace omaseed::core {

namespace {

QDate localDate(qint64 unixSeconds)
{
    return QDateTime::fromSecsSinceEpoch(unixSeconds).date();
}

qint64 startOfDay(const QDate &date)
{
    return date.startOfDay().toSecsSinceEpoch();
}

QString dayKey(const QDate &date)
{
    return date.toString(Qt::ISODate);
}

constexpr qint64 kMinute = 60;
constexpr qint64 kHour = 3600;
constexpr qint64 kDay = 86400;

} // namespace

SeedLedger::SeedLedger()
    : m_connection(QStringLiteral("seedledger-") + QUuid::createUuid().toString(QUuid::Id128))
{}

SeedLedger::~SeedLedger()
{
    close();
    QSqlDatabase::removeDatabase(m_connection);
}

QString SeedLedger::defaultPath(const QString &serverId)
{
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    QString safe = serverId;
    for (QChar &c : safe) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    }
    if (safe.isEmpty())
        safe = QStringLiteral("default");
    return base + QStringLiteral("/omaseed/ledger/") + safe + QStringLiteral(".sqlite");
}

bool SeedLedger::open(const QString &path)
{
    close();
    m_path = path;
    m_start = 0;
    m_lastTime = 0;
    m_lastUp = -1;
    m_lastDown = -1;
    m_baselines.clear();
    m_baselinesDirty.clear();
    m_baselinesGone.clear();
    m_missing.clear();
    m_wholeSeen = false;
    m_speeds.clear();
    dropDayTotals();

    QDir().mkpath(QFileInfo(path).absolutePath());
    bool broken = false;
    if (!openFile(path, &broken)) {
        if (!broken)
            return false;
        // Not a ledger: keep it for whoever wants to look, and start a new one.
        QSqlDatabase::database(m_connection, false).close();
        const QString aside = path + QStringLiteral(".broken-")
                              + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        qWarning("SeedLedger: %s is not a ledger; moved to %s", qPrintable(path), qPrintable(aside));
        QFile::rename(path, aside);
        QFile::remove(path + QStringLiteral("-wal"));
        QFile::remove(path + QStringLiteral("-shm"));
        if (!openFile(path, &broken)) {
            QSqlDatabase::database(m_connection, false).close();
            return false;
        }
    }
    m_open = true;
    loadState();
    return true;
}

bool SeedLedger::openFile(const QString &path, bool *broken)
{
    *broken = false;
    QSqlDatabase db = QSqlDatabase::contains(m_connection)
                          ? QSqlDatabase::database(m_connection, false)
                          : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path);
    if (!db.open()) {
        qWarning("SeedLedger: cannot open %s: %s",
                 qPrintable(path),
                 qPrintable(db.lastError().text()));
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QSqlQuery q(db);
    const char *schema[] = {
        "PRAGMA journal_mode=WAL",
        // A lost last flush costs nothing but precision (see flushInterval): no need to wait
        // for the disk on every commit.
        "PRAGMA synchronous=NORMAL",
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value INTEGER)",
        "CREATE TABLE IF NOT EXISTS minutes(t INTEGER PRIMARY KEY, up INTEGER, down INTEGER, "
        "gap INTEGER)",
        "CREATE TABLE IF NOT EXISTS hours(t INTEGER PRIMARY KEY, up INTEGER, down INTEGER, "
        "gap INTEGER)",
        "CREATE TABLE IF NOT EXISTS days(day TEXT PRIMARY KEY, up INTEGER, down INTEGER, "
        "gap INTEGER)",
        "CREATE TABLE IF NOT EXISTS torrent_days(day TEXT, hash TEXT, up INTEGER, "
        "PRIMARY KEY(day, hash))",
        "CREATE TABLE IF NOT EXISTS baselines(hash TEXT PRIMARY KEY, uploaded INTEGER, "
        "downloaded INTEGER, added INTEGER)",
    };
    for (const char *statement : schema) {
        if (!q.exec(QString::fromLatin1(statement))) {
            qWarning("SeedLedger: %s", qPrintable(q.lastError().text()));
            // SQLITE_NOTADB, SQLITE_CORRUPT: the file is no ledger. Anything else (a lock, a
            // read-only disk) is no reason to set a good file aside.
            const QString code = q.lastError().nativeErrorCode();
            if (code == QLatin1String("26") || code == QLatin1String("11")) {
                *broken = true;
                return false;
            }
        }
    }
    // A ledger from before baselines knew when a torrent was added.
    bool hasAdded = false;
    if (q.exec(QStringLiteral("PRAGMA table_info(baselines)"))) {
        while (q.next())
            hasAdded = hasAdded || q.value(1).toString() == QLatin1String("added");
    }
    if (!hasAdded && !q.exec(QStringLiteral("ALTER TABLE baselines ADD COLUMN added INTEGER")))
        qWarning("SeedLedger: %s", qPrintable(q.lastError().text()));
    return true;
}

void SeedLedger::close()
{
    if (!m_open)
        return;
    flush(m_lastTime > 0 ? m_lastTime : QDateTime::currentSecsSinceEpoch());
    QSqlDatabase::database(m_connection, false).close();
    m_open = false;
    dropDayTotals();
}

bool SeedLedger::isOpen() const
{
    return m_open;
}

void SeedLedger::loadState()
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT key, value FROM meta"))) {
        while (q.next()) {
            const QString key = q.value(0).toString();
            const qint64 value = q.value(1).toLongLong();
            if (key == QLatin1String("start"))
                m_start = value;
            else if (key == QLatin1String("last_t"))
                m_lastTime = value;
            else if (key == QLatin1String("last_up"))
                m_lastUp = value;
            else if (key == QLatin1String("last_down"))
                m_lastDown = value;
        }
    }
    if (q.exec(QStringLiteral("SELECT hash, uploaded, downloaded, added FROM baselines"))) {
        while (q.next())
            m_baselines.insert(q.value(0).toString(),
                               {q.value(1).toLongLong(), q.value(2).toLongLong(),
                                q.value(3).toLongLong(), -1});
    }
    m_lastFlush = m_lastTime;
}

void SeedLedger::addTo(QMap<qint64, Bucket> &map, qint64 key, qint64 up, qint64 down, bool gap)
{
    Bucket &b = map[key];
    b.up += up;
    b.down += down;
    b.gap = b.gap || gap;
}

void SeedLedger::observe(qint64 now,
                         qint64 allTimeUp,
                         qint64 allTimeDown,
                         const QList<TorrentTotals> &torrents)
{
    if (now <= 0)
        return;
    if (m_start == 0) {
        m_start = now;
        m_metaDirty = true;
    }

    const bool haveServer = m_lastUp >= 0 && m_lastDown >= 0 && m_lastTime > 0;

    // Per torrent: the difference since its own baseline. A torrent seen for the first time,
    // or whose totals went down, only takes a baseline.
    QHash<QString, qint64> perTorrent;
    QHash<QString, Baseline> next;
    next.reserve(torrents.size());
    for (const TorrentTotals &t : torrents) {
        next.insert(t.hash, {t.uploaded, t.downloaded, t.addedOn, now});
        const auto it = m_baselines.constFind(t.hash);
        if (it == m_baselines.constEnd() || it->uploaded != t.uploaded
            || it->downloaded != t.downloaded || it->addedOn != t.addedOn) {
            m_baselinesDirty.insert(t.hash);
            m_baselinesGone.remove(t.hash);
        }
        if (it == m_baselines.constEnd() || !haveServer)
            continue;
        // Added again since the baseline: its totals began from nothing then.
        const bool again = t.addedOn > 0 && it->addedOn > 0 && t.addedOn != it->addedOn;
        const qint64 delta = again ? t.uploaded : t.uploaded - it->uploaded;
        if (delta > 0)
            perTorrent.insert(t.hash, delta);
    }
    m_missing.clear();
    for (auto it = m_baselines.cbegin(); it != m_baselines.cend(); ++it) {
        if (next.contains(it.key()))
            continue;
        // Missing from this poll: kept a while (listed late, not removed), then let go.
        Baseline kept = *it;
        if (kept.seen < 0)
            kept.seen = now; // the grace starts with this run's first poll
        if (now - kept.seen < missingKept) {
            next.insert(it.key(), kept);
            m_missing.insert(it.key());
        } else {
            m_baselinesGone.insert(it.key());
            m_baselinesDirty.remove(it.key());
        }
    }
    m_baselines = next;
    m_wholeSeen = true;
    finishPoll(now, allTimeUp, allTimeDown, perTorrent);
}

void SeedLedger::observeChanges(qint64 now,
                                qint64 allTimeUp,
                                qint64 allTimeDown,
                                const QList<TorrentTotals> &changed,
                                const QStringList &removed)
{
    if (now <= 0)
        return;
    if (m_start == 0) {
        m_start = now;
        m_metaDirty = true;
    }

    const bool haveServer = m_lastUp >= 0 && m_lastDown >= 0 && m_lastTime > 0;

    // Gone since the last poll, which was the last to list them: their grace starts there,
    // as observe() would have it. One removed and added again in the same poll is listed.
    if (!removed.isEmpty()) {
        QSet<QString> listed;
        for (const TorrentTotals &t : changed)
            listed.insert(t.hash);
        for (const QString &hash : removed) {
            const auto it = m_baselines.find(hash);
            if (it == m_baselines.end() || listed.contains(hash) || m_missing.contains(hash))
                continue;
            it->seen = m_lastTime > 0 ? m_lastTime : now;
            m_missing.insert(hash);
        }
    }

    // The same per-torrent rule as observe(), only for the torrents that moved.
    QHash<QString, qint64> perTorrent;
    for (const TorrentTotals &t : changed) {
        const auto it = m_baselines.constFind(t.hash);
        const bool known = it != m_baselines.constEnd();
        if (!known || it->uploaded != t.uploaded || it->downloaded != t.downloaded
            || it->addedOn != t.addedOn) {
            m_baselinesDirty.insert(t.hash);
            m_baselinesGone.remove(t.hash);
        }
        if (known && haveServer) {
            const bool again = t.addedOn > 0 && it->addedOn > 0 && t.addedOn != it->addedOn;
            const qint64 delta = again ? t.uploaded : t.uploaded - it->uploaded;
            if (delta > 0)
                perTorrent.insert(t.hash, delta);
        }
        m_baselines.insert(t.hash, {t.uploaded, t.downloaded, t.addedOn, now});
        m_missing.remove(t.hash);
    }

    // The missing ones' grace, as observe() keeps it.
    for (auto it = m_missing.begin(); it != m_missing.end();) {
        const auto baseline = m_baselines.find(*it);
        if (baseline == m_baselines.end()) {
            it = m_missing.erase(it);
            continue;
        }
        if (baseline->seen < 0)
            baseline->seen = now;
        if (now - baseline->seen < missingKept) {
            ++it;
            continue;
        }
        m_baselinesGone.insert(*it);
        m_baselinesDirty.remove(*it);
        m_baselines.erase(baseline);
        it = m_missing.erase(it);
    }
    finishPoll(now, allTimeUp, allTimeDown, perTorrent);
}

void SeedLedger::finishPoll(qint64 now,
                            qint64 allTimeUp,
                            qint64 allTimeDown,
                            const QHash<QString, qint64> &perTorrent)
{
    const bool haveServer = m_lastUp >= 0 && m_lastDown >= 0 && m_lastTime > 0;
    const bool backwards = haveServer && (allTimeUp < m_lastUp || allTimeDown < m_lastDown);
    const qint64 from = m_lastTime;
    if (haveServer && !backwards) {
        const qint64 up = allTimeUp - m_lastUp;
        const qint64 down = allTimeDown - m_lastDown;
        // A clock that went back still books the poll, at its own (earlier) time.
        if (up > 0 || down > 0 || !perTorrent.isEmpty())
            book(std::min(from, now), now, up, down, perTorrent, now);
    }

    m_lastTime = now;
    m_lastUp = allTimeUp;
    m_lastDown = allTimeDown;
    m_metaDirty = true;

    // A clock that went back must not hold the next flush until it catches up.
    if (now - m_lastFlush >= flushInterval || now < m_lastFlush)
        flush(now);
}

void SeedLedger::book(qint64 from,
                      qint64 to,
                      qint64 up,
                      qint64 down,
                      const QHash<QString, qint64> &perTorrent,
                      qint64 now)
{
    const qint64 span = to - from;
    if (span <= gapSeconds) {
        // A normal poll: everything lands in the buckets that hold `to`.
        addTo(m_pendingMinutes, (to / kMinute) * kMinute, up, down, false);
        addTo(m_pendingHours, (to / kHour) * kHour, up, down, false);
        Bucket &d = m_pendingDays[localDate(to)];
        d.up += up;
        d.down += down;
        const QDate day = localDate(to);
        for (auto it = perTorrent.cbegin(); it != perTorrent.cend(); ++it) {
            m_pendingTorrentDays[{day, it.key()}] += it.value();
            bookDayTotal(day, it.key(), it.value());
        }
        if (!perTorrent.isEmpty())
            ++m_revision;
        return;
    }

    // A gap: spread evenly. Days get every byte (the remainder goes to the last piece); the
    // finer tables only the pieces they still keep, rounded.
    const double total = double(span);
    auto spreadFine = [&](QMap<qint64, Bucket> &map, qint64 step, qint64 keep) {
        qint64 cursor = std::max(from, now - keep);
        cursor = std::max(cursor, from);
        while (cursor < to) {
            const qint64 key = (cursor / step) * step;
            const qint64 end = std::min(key + step, to);
            const double share = double(end - cursor) / total;
            addTo(map, key, qint64(std::llround(double(up) * share)),
                  qint64(std::llround(double(down) * share)), true);
            cursor = end;
        }
    };
    spreadFine(m_pendingMinutes, kMinute, minutesKept);
    spreadFine(m_pendingHours, kHour, hoursKept);

    qint64 cursor = from;
    qint64 upLeft = up;
    qint64 downLeft = down;
    QHash<QString, qint64> torrentLeft = perTorrent;
    while (cursor < to) {
        const QDate day = localDate(cursor);
        const qint64 end = std::min(startOfDay(day.addDays(1)), to);
        const bool last = end >= to;
        const double share = double(end - cursor) / total;
        const qint64 u = last ? upLeft : qint64(std::llround(double(up) * share));
        const qint64 d = last ? downLeft : qint64(std::llround(double(down) * share));
        upLeft -= u;
        downLeft -= d;
        Bucket &b = m_pendingDays[day];
        b.up += u;
        b.down += d;
        b.gap = true;
        for (auto it = perTorrent.cbegin(); it != perTorrent.cend(); ++it) {
            qint64 &left = torrentLeft[it.key()];
            const qint64 part = last ? left : qint64(std::llround(double(it.value()) * share));
            left -= part;
            if (part > 0) {
                m_pendingTorrentDays[{day, it.key()}] += part;
                bookDayTotal(day, it.key(), part);
            }
        }
        if (end <= cursor)
            break;
        cursor = end;
    }
    if (!perTorrent.isEmpty())
        ++m_revision;
}

void SeedLedger::bookDayTotal(const QDate &day, const QString &hash, qint64 up)
{
    if (m_dayTotalsValid && day == m_dayTotalsDate)
        m_dayTotals[hash] += up;
}

void SeedLedger::dropDayTotals()
{
    m_dayTotalsValid = false;
    m_dayTotals.clear();
    ++m_revision;
}

void SeedLedger::sampleSpeed(qint64 now, qint64 up, qint64 down)
{
    if (!m_speeds.isEmpty() && m_speeds.constLast().t == now) {
        m_speeds.last().up = up;
        m_speeds.last().down = down;
    } else {
        m_speeds.append({now, up, down});
    }
    while (!m_speeds.isEmpty() && m_speeds.constFirst().t <= now - kHour)
        m_speeds.removeFirst();
}

bool SeedLedger::saveMeta()
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QSqlQuery q(db);
    bool ok = q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)"));
    const QList<QPair<QString, qint64>> values{{QStringLiteral("start"), m_start},
                                               {QStringLiteral("last_t"), m_lastTime},
                                               {QStringLiteral("last_up"), m_lastUp},
                                               {QStringLiteral("last_down"), m_lastDown},
                                               {QStringLiteral("version"), 1}};
    for (const auto &[key, value] : values) {
        q.addBindValue(key);
        q.addBindValue(value);
        ok = q.exec() && ok;
    }
    // Only the rows that moved: most of a big library sits still between two flushes.
    if (!m_baselinesGone.isEmpty()) {
        ok = q.prepare(QStringLiteral("DELETE FROM baselines WHERE hash = ?")) && ok;
        q.addBindValue(QVariantList(m_baselinesGone.cbegin(), m_baselinesGone.cend()));
        ok = q.execBatch() && ok;
    }
    if (!m_baselinesDirty.isEmpty()) {
        ok = q.prepare(QStringLiteral("INSERT OR REPLACE INTO baselines(hash, uploaded, downloaded, "
                                      "added) VALUES(?, ?, ?, ?)"))
             && ok;
        QVariantList hashes, uploaded, downloaded, added;
        for (const QString &hash : std::as_const(m_baselinesDirty)) {
            const Baseline b = m_baselines.value(hash);
            hashes << hash;
            uploaded << b.uploaded;
            downloaded << b.downloaded;
            added << b.addedOn;
        }
        q.addBindValue(hashes);
        q.addBindValue(uploaded);
        q.addBindValue(downloaded);
        q.addBindValue(added);
        ok = q.execBatch() && ok;
    }
    return ok;
}

void SeedLedger::flush(qint64 now)
{
    m_lastFlush = now;
    if (!m_open) {
        prunePending(now); // memory only: the pending maps are the whole ledger
        return;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    bool ok = db.transaction();
    QSqlQuery q(db);
    auto upsert = [&](const QString &table, const QMap<qint64, Bucket> &map) {
        ok = q.prepare(QStringLiteral("INSERT INTO %1(t, up, down, gap) VALUES(?, ?, ?, ?) "
                                      "ON CONFLICT(t) DO UPDATE SET up = up + excluded.up, "
                                      "down = down + excluded.down, gap = max(gap, excluded.gap)")
                           .arg(table))
             && ok;
        for (auto it = map.cbegin(); ok && it != map.cend(); ++it) {
            q.addBindValue(it.key());
            q.addBindValue(it->up);
            q.addBindValue(it->down);
            q.addBindValue(it->gap ? 1 : 0);
            ok = q.exec();
        }
    };
    upsert(QStringLiteral("minutes"), m_pendingMinutes);
    upsert(QStringLiteral("hours"), m_pendingHours);
    ok = ok
         && q.prepare(QStringLiteral("INSERT INTO days(day, up, down, gap) VALUES(?, ?, ?, ?) "
                                     "ON CONFLICT(day) DO UPDATE SET up = up + excluded.up, "
                                     "down = down + excluded.down, gap = max(gap, excluded.gap)"));
    for (auto it = m_pendingDays.cbegin(); ok && it != m_pendingDays.cend(); ++it) {
        q.addBindValue(dayKey(it.key()));
        q.addBindValue(it->up);
        q.addBindValue(it->down);
        q.addBindValue(it->gap ? 1 : 0);
        ok = q.exec();
    }
    ok = ok
         && q.prepare(QStringLiteral("INSERT INTO torrent_days(day, hash, up) VALUES(?, ?, ?) "
                                     "ON CONFLICT(day, hash) DO UPDATE SET up = up + excluded.up"));
    for (auto it = m_pendingTorrentDays.cbegin(); ok && it != m_pendingTorrentDays.cend(); ++it) {
        q.addBindValue(dayKey(it.key().first));
        q.addBindValue(it.key().second);
        q.addBindValue(it.value());
        ok = q.exec();
    }
    if (ok && m_metaDirty)
        ok = saveMeta();
    ok = ok && db.commit();
    if (!ok) {
        // A full disk, a read-only file: nothing of this flush reached the file, so keep it
        // all buffered (queries still see it) and try again at the next flush.
        const QString error = q.lastError().isValid() ? q.lastError().text() : db.lastError().text();
        db.rollback();
        if (!m_flushFailed)
            qWarning("SeedLedger: cannot write %s: %s", qPrintable(m_path), qPrintable(error));
        m_flushFailed = true;
        prunePending(now);
        return;
    }
    if (m_flushFailed)
        qInfo("SeedLedger: writing %s again", qPrintable(m_path));
    m_flushFailed = false;
    m_pendingMinutes.clear();
    m_pendingHours.clear();
    m_pendingDays.clear();
    m_pendingTorrentDays.clear();
    m_baselinesDirty.clear();
    m_baselinesGone.clear();
    m_metaDirty = false;
    prune(now);
}

void SeedLedger::prunePending(qint64 now)
{
    while (!m_pendingMinutes.isEmpty() && m_pendingMinutes.firstKey() < now - minutesKept)
        m_pendingMinutes.erase(m_pendingMinutes.begin());
    while (!m_pendingHours.isEmpty() && m_pendingHours.firstKey() < now - hoursKept)
        m_pendingHours.erase(m_pendingHours.begin());
}

void SeedLedger::prune(qint64 now)
{
    if (!m_open)
        return;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.prepare(QStringLiteral("DELETE FROM minutes WHERE t < ?"));
    q.addBindValue(now - minutesKept);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM hours WHERE t < ?"));
    q.addBindValue(now - hoursKept);
    q.exec();
}

QMap<qint64, SeedLedger::Bucket> SeedLedger::readBuckets(const QString &table,
                                                         qint64 from,
                                                         qint64 to) const
{
    QMap<qint64, Bucket> out;
    if (m_open) {
        QSqlQuery q(QSqlDatabase::database(m_connection, false));
        q.prepare(QStringLiteral("SELECT t, up, down, gap FROM %1 WHERE t >= ? AND t < ?")
                      .arg(table));
        q.addBindValue(from);
        q.addBindValue(to);
        if (q.exec()) {
            while (q.next())
                out.insert(q.value(0).toLongLong(),
                           {q.value(1).toLongLong(), q.value(2).toLongLong(),
                            q.value(3).toInt() != 0});
        }
    }
    const QMap<qint64, Bucket> &pending = table == QLatin1String("minutes") ? m_pendingMinutes
                                                                             : m_pendingHours;
    for (auto it = pending.lowerBound(from); it != pending.cend() && it.key() < to; ++it) {
        Bucket &b = out[it.key()];
        b.up += it->up;
        b.down += it->down;
        b.gap = b.gap || it->gap;
    }
    return out;
}

QMap<QDate, SeedLedger::Bucket> SeedLedger::readDays(const QDate &from, const QDate &to) const
{
    QMap<QDate, Bucket> out;
    if (m_open) {
        QSqlQuery q(QSqlDatabase::database(m_connection, false));
        q.prepare(QStringLiteral("SELECT day, up, down, gap FROM days WHERE day >= ? AND day <= ?"));
        q.addBindValue(dayKey(from));
        q.addBindValue(dayKey(to));
        if (q.exec()) {
            while (q.next())
                out.insert(QDate::fromString(q.value(0).toString(), Qt::ISODate),
                           {q.value(1).toLongLong(), q.value(2).toLongLong(),
                            q.value(3).toInt() != 0});
        }
    }
    for (auto it = m_pendingDays.lowerBound(from); it != m_pendingDays.cend() && it.key() <= to;
         ++it) {
        Bucket &b = out[it.key()];
        b.up += it->up;
        b.down += it->down;
        b.gap = b.gap || it->gap;
    }
    return out;
}

QList<SeedLedger::Point> SeedLedger::series(qint64 now, qint64 bucketSeconds, int count) const
{
    QList<Point> points;
    if (bucketSeconds <= 0 || count <= 0)
        return points;
    points.reserve(count);

    if (bucketSeconds >= kDay) {
        const QDate today = localDate(now);
        const QDate first = today.addDays(-(count - 1));
        const QMap<QDate, Bucket> days = readDays(first, today);
        const QDate startDate = m_start > 0 ? localDate(m_start) : today.addDays(1);
        for (int i = 0; i < count; ++i) {
            const QDate day = first.addDays(i);
            const Bucket b = days.value(day);
            points.append({startOfDay(day), double(b.up) / double(kDay),
                           double(b.down) / double(kDay), b.gap, day >= startDate});
        }
        return points;
    }

    const qint64 end = (now / bucketSeconds) * bucketSeconds + bucketSeconds;
    const qint64 first = end - bucketSeconds * count;

    if (bucketSeconds < kMinute) {
        const qint64 firstSample = m_speeds.isEmpty() ? now + 1 : m_speeds.constFirst().t;
        QList<double> up(count), down(count);
        QList<int> n(count);
        for (const Speed &s : m_speeds) {
            const qint64 i = (s.t - first) / bucketSeconds;
            if (i < 0 || i >= count)
                continue;
            up[i] += double(s.up);
            down[i] += double(s.down);
            ++n[i];
        }
        for (int i = 0; i < count; ++i) {
            const qint64 start = first + i * bucketSeconds;
            points.append({start, n[i] ? up[i] / n[i] : 0.0, n[i] ? down[i] / n[i] : 0.0, false,
                           start + bucketSeconds > firstSample});
        }
        return points;
    }

    const bool fromMinutes = bucketSeconds < kHour;
    const qint64 step = fromMinutes ? kMinute : kHour;
    const QMap<qint64, Bucket> source = readBuckets(fromMinutes ? QStringLiteral("minutes")
                                                                : QStringLiteral("hours"),
                                                    (first / step) * step,
                                                    end);
    QList<Bucket> sums(count);
    for (auto it = source.cbegin(); it != source.cend(); ++it) {
        const qint64 i = (it.key() - first) / bucketSeconds;
        if (i < 0 || i >= count)
            continue;
        sums[i].up += it->up;
        sums[i].down += it->down;
        sums[i].gap = sums[i].gap || it->gap;
    }
    for (int i = 0; i < count; ++i) {
        const qint64 start = first + i * bucketSeconds;
        points.append({start, double(sums[i].up) / double(bucketSeconds),
                       double(sums[i].down) / double(bucketSeconds), sums[i].gap,
                       m_start > 0 && start + bucketSeconds > m_start});
    }
    return points;
}

QList<qint64> SeedLedger::recentUpload(qint64 now, int count) const
{
    QList<qint64> out(count, 0);
    for (const Speed &s : m_speeds) {
        const qint64 i = count - 1 - (now - s.t);
        if (i >= 0 && i < count)
            out[i] = s.up;
    }
    return out;
}

QList<qint64> SeedLedger::recentDownload(qint64 now, int count) const
{
    QList<qint64> out(count, 0);
    for (const Speed &s : m_speeds) {
        const qint64 i = count - 1 - (now - s.t);
        if (i >= 0 && i < count)
            out[i] = s.down;
    }
    return out;
}

QList<SeedLedger::Day> SeedLedger::days(const QDate &from, const QDate &to) const
{
    QList<Day> out;
    if (!from.isValid() || !to.isValid() || from > to)
        return out;
    const QMap<QDate, Bucket> days = readDays(from, to);
    const QDate startDate = m_start > 0 ? localDate(m_start) : to.addDays(1);
    for (QDate d = from; d <= to; d = d.addDays(1))
        out.append({d, d < startDate ? -1 : days.value(d).up});
    return out;
}

QHash<QString, qint64> SeedLedger::torrentTotals(const QDate &from, const QDate &to) const
{
    QHash<QString, qint64> out;
    if (m_open) {
        QSqlQuery q(QSqlDatabase::database(m_connection, false));
        q.prepare(QStringLiteral("SELECT hash, SUM(up) FROM torrent_days WHERE day >= ? AND "
                                 "day <= ? GROUP BY hash"));
        q.addBindValue(dayKey(from));
        q.addBindValue(dayKey(to));
        if (q.exec()) {
            while (q.next())
                out[q.value(0).toString()] += q.value(1).toLongLong();
        }
    }
    for (auto it = m_pendingTorrentDays.cbegin(); it != m_pendingTorrentDays.cend(); ++it) {
        if (it.key().first >= from && it.key().first <= to)
            out[it.key().second] += it.value();
    }
    return out;
}

const QHash<QString, qint64> &SeedLedger::dayTotals(const QDate &day) const
{
    if (!m_dayTotalsValid || day != m_dayTotalsDate) {
        m_dayTotals = torrentTotals(day, day);
        m_dayTotalsDate = day;
        m_dayTotalsValid = true;
    }
    return m_dayTotals;
}

QHash<QString, QList<qint64>> SeedLedger::torrentDays(const QDate &from, const QDate &to) const
{
    QHash<QString, QList<qint64>> out;
    const qint64 n = from.daysTo(to) + 1;
    if (n <= 0)
        return out;
    auto add = [&](const QDate &day, const QString &hash, qint64 up) {
        auto it = out.find(hash);
        if (it == out.end())
            it = out.insert(hash, QList<qint64>(n, 0));
        (*it)[from.daysTo(day)] += up;
    };
    if (m_open) {
        QSqlQuery q(QSqlDatabase::database(m_connection, false));
        q.prepare(QStringLiteral("SELECT day, hash, up FROM torrent_days WHERE day >= ? AND day <= ?"));
        q.addBindValue(dayKey(from));
        q.addBindValue(dayKey(to));
        if (q.exec()) {
            while (q.next())
                add(QDate::fromString(q.value(0).toString(), Qt::ISODate), q.value(1).toString(),
                    q.value(2).toLongLong());
        }
    }
    for (auto it = m_pendingTorrentDays.cbegin(); it != m_pendingTorrentDays.cend(); ++it) {
        if (it.key().first >= from && it.key().first <= to)
            add(it.key().first, it.key().second, it.value());
    }
    return out;
}

int SeedLedger::streak(const QDate &today) const
{
    if (m_start == 0)
        return 0;
    const QDate startDate = localDate(m_start);
    const QMap<QDate, Bucket> days = readDays(startDate, today);
    QDate d = today;
    if (days.value(d).up <= 0)
        d = d.addDays(-1);
    int run = 0;
    while (d >= startDate && days.value(d).up > 0) {
        ++run;
        d = d.addDays(-1);
    }
    return run;
}

int SeedLedger::longestStreak() const
{
    if (m_start == 0)
        return 0;
    const QDate startDate = localDate(m_start);
    const QDate end = localDate(std::max(m_lastTime, m_start));
    const QMap<QDate, Bucket> days = readDays(startDate, end);
    int best = 0;
    int run = 0;
    for (QDate d = startDate; d <= end; d = d.addDays(1)) {
        run = days.value(d).up > 0 ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

void SeedLedger::fillDemo(qint64 now, const QStringList &hashes, qint64 dayBytes, quint32 seed)
{
    if (m_start != 0 && now - m_start > 2 * kDay)
        return;
    QRandomGenerator rnd(seed);
    auto between = [&](double a, double b) { return a + (b - a) * rnd.generateDouble(); };
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    const double unit = dayBytes > 0 ? double(dayBytes) / 10.0 : kGB;

    const QDate today = localDate(now);
    const int span = 365;
    const qint64 end = (now / kMinute) * kMinute - kMinute; // leave this minute to the live data
    m_start = startOfDay(today.addDays(-span));

    // Each torrent's share of a day: a third carry most of it, the rest a little or nothing.
    QList<double> weight;
    double weightSum = 0;
    for (int i = 0; i < hashes.size(); ++i) {
        const double w = rnd.generateDouble() < 0.33 ? between(2, 10) : rnd.generateDouble() < 0.5 ? between(0.1, 1) : 0.0;
        weight.append(w);
        weightSum += w;
    }

    // A day's total, then spread over its hours by a diurnal curve (evenings busiest).
    auto diurnal = [](int hour) {
        const double s = std::sin((hour - 14) / 24.0 * 2 * M_PI);
        return 0.55 + 0.45 * s * s + (hour > 18 ? 0.35 : 0.0);
    };
    double diurnalSum = 0;
    for (int h = 0; h < 24; ++h)
        diurnalSum += diurnal(h);

    for (int back = span; back >= 0; --back) {
        const QDate day = today.addDays(-back);
        const int i = span - back;
        const int wd = day.dayOfWeek();
        const double season = 0.75 + 0.35 * std::sin(double(i) / span * 2 * M_PI + 1.2);
        // The board's numbers were made for a ~10 GB day; `unit` rescales them.
        double v = (wd >= 6 ? 1.35 : 1.0) * season * between(3, 22) * unit;
        if (rnd.generateDouble() < 0.06)
            v *= between(2.2, 4);
        if (rnd.generateDouble() < 0.11)
            v = between(0, 0.2) * unit;
        if (back < 41)
            v = std::max(v, between(1.5, 4) * unit);
        if (back == 41)
            v = 0;
        if (i >= 130 && i < 136)
            v = 0; // a week away
        const double down = rnd.generateDouble() < 0.12 ? between(0.2, 4) * unit : 0.0;

        const qint64 dayStart = startOfDay(day);
        double booked = 0, bookedDown = 0;
        for (int h = 0; h < 24; ++h) {
            const qint64 hourStart = dayStart + h * kHour;
            if (hourStart >= end)
                break;
            const double share = diurnal(h) / diurnalSum * between(0.7, 1.3);
            const double up = v * share;
            const double dn = (h >= 9 && h < 12) ? down / 3.0 : 0.0;
            const bool gap = back == 1 && h >= 2 && h < 7;
            booked += up;
            bookedDown += dn;
            if (now - hourStart <= hoursKept)
                addTo(m_pendingHours, hourStart, qint64(up), qint64(dn), gap);
            if (now - hourStart <= minutesKept) {
                for (int m = 0; m < 60; ++m) {
                    const qint64 t = hourStart + m * kMinute;
                    if (t >= end)
                        break;
                    const double f = gap ? 1.0 / 60 : between(0.2, 1.8) / 60;
                    addTo(m_pendingMinutes, t, qint64(up * f), qint64(dn / 60), gap);
                }
            }
        }
        Bucket &b = m_pendingDays[day];
        b.up += qint64(booked);
        b.down += qint64(bookedDown);
        b.gap = b.gap || back == 1;
        if (weightSum > 0) {
            for (int k = 0; k < hashes.size(); ++k) {
                const qint64 part = qint64(booked * weight[k] / weightSum * between(0.3, 1.7));
                if (part > 0)
                    m_pendingTorrentDays[{day, hashes[k]}] += part;
            }
        }
    }
    // The last hour of speeds, for the 1 h tide and the hero.
    m_speeds.clear();
    for (qint64 t = now - kHour + 1; t <= now; ++t) {
        const double up = (1.6 + std::sin(double(t) / 400.0) * 0.6 + between(0, 0.5)) * unit / kGB * 1024 * 1024;
        m_speeds.append({t, qint64(up), 0});
    }
    m_metaDirty = true;
    dropDayTotals();
    flush(now);
}

} // namespace omaseed::core
