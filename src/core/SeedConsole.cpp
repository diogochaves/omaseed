#include "core/SeedConsole.hpp"

#include "core/Format.hpp"
#include "core/GlobalState.hpp"
#include "core/SeedLedger.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TrackerModel.hpp"

#include <QLocale>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace omaseed::core {

namespace {

constexpr qint64 kDay = 86400;
constexpr int kQuietDays = 30;
constexpr int kRows = 5;
constexpr int kAirRows = 6;

QLocale english()
{
    return QLocale(QLocale::English, QLocale::UnitedKingdom);
}

QString ratioText(double given, double taken)
{
    if (taken <= 0)
        return given > 0 ? QStringLiteral("∞") : QStringLiteral("—");
    const double r = given / taken;
    return r >= 100 ? QString::number(r, 'f', 0) : r >= 10 ? QString::number(r, 'f', 1)
                                                            : QString::number(r, 'f', 2);
}

QString timesText(double x)
{
    return (x >= 100 ? QString::number(x, 'f', 0)
            : x >= 10 ? QString::number(x, 'f', 1)
                      : QString::number(x, 'f', 2))
           + QStringLiteral("×");
}

QString signedSize(qint64 bytes)
{
    return (bytes < 0 ? QStringLiteral("−") : QStringLiteral("+")) + Format::size(std::abs(bytes));
}

QString countText(int n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(Format::count(n), n == 1 ? one : many);
}

int quietDays(const Torrent &t, const QDateTime &now)
{
    const QDateTime last = t.lastActivity.isValid() && t.lastActivity.toSecsSinceEpoch() > 0
                               ? t.lastActivity
                               : t.addedOn;
    if (!last.isValid())
        return 0;
    return int(last.secsTo(now) / kDay);
}

QString ago(const QDateTime &when, const QDateTime &now)
{
    const qint64 s = when.secsTo(now);
    if (s < 3600)
        return QStringLiteral("%1 min ago").arg(std::max<qint64>(1, s / 60));
    if (when.date() == now.date())
        return QStringLiteral("%1 h ago").arg(s / 3600);
    if (when.date() == now.date().addDays(-1))
        return QStringLiteral("yesterday");
    if (when.daysTo(now) < 7)
        return english().toString(when, QStringLiteral("ddd"));
    return english().toString(when, QStringLiteral("d MMM"));
}

} // namespace

SeedConsole::SeedConsole(QObject *parent)
    : QObject(parent)
{
    m_refreshTimer.setInterval(2000);
    m_slowTimer.setInterval(30000);
    m_heroTimer.setInterval(1000);
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, &SeedConsole::refreshFast);
    connect(&m_slowTimer, &QTimer::timeout, this, &SeedConsole::refreshSlow);
    connect(&m_heroTimer, &QTimer::timeout, this, &SeedConsole::refreshHero);
    connect(&m_coalesce, &QTimer::timeout, this, &SeedConsole::refreshFast);
}

SeedConsole::~SeedConsole() = default;

void SeedConsole::setSources(TorrentModel *torrents,
                             TorrentFilterModel *scope,
                             TrackerModel *trackers,
                             GlobalState *state,
                             SeedLedger *ledger)
{
    m_torrents = torrents;
    m_scope = scope;
    m_trackerModel = trackers;
    m_state = state;
    m_ledger = ledger;
    if (m_scope) {
        // The rail moved: the scope is a new one, so the ledger's blocks too.
        connect(m_scope, &TorrentFilterModel::countChanged, this, [this] {
            if (m_active && m_scopeHashes.isEmpty())
                refresh();
        });
        connect(m_scope, &QAbstractItemModel::modelReset, this, [this] {
            if (m_active && m_scopeHashes.isEmpty())
                refresh();
        });
    }
    if (m_ledger)
        m_heroTimer.start();
}

void SeedConsole::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void SeedConsole::setRecentProvider(std::function<QVariantList()> provider)
{
    m_recent = std::move(provider);
}

void SeedConsole::setIntervals(int refreshMs, int slowMs)
{
    m_refreshTimer.setInterval(refreshMs);
    m_slowTimer.setInterval(slowMs);
}

QDateTime SeedConsole::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void SeedConsole::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        refresh();
        m_refreshTimer.start();
        m_slowTimer.start();
    } else {
        m_refreshTimer.stop();
        m_slowTimer.stop();
    }
    Q_EMIT activeChanged();
}

QStringList SeedConsole::ranges()
{
    return {QStringLiteral("1h"), QStringLiteral("24h"), QStringLiteral("7d"),
            QStringLiteral("30d"), QStringLiteral("1y")};
}

void SeedConsole::setRange(const QString &range)
{
    if (range == m_range || !ranges().contains(range))
        return;
    m_range = range;
    Q_EMIT rangeChanged();
    if (m_active)
        refreshSlow();
}

void SeedConsole::stepRange(int delta)
{
    const QStringList all = ranges();
    const int i = std::clamp(int(all.indexOf(m_range)) + delta, 0, int(all.size()) - 1);
    setRange(all.at(i));
}

void SeedConsole::setRareSeeds(int seeds)
{
    seeds = std::max(1, seeds);
    if (seeds == m_rareSeeds)
        return;
    m_rareSeeds = seeds;
    Q_EMIT rareSeedsChanged();
    if (m_active)
        refreshFast();
}

void SeedConsole::setScopeHashes(const QStringList &hashes)
{
    if (hashes == m_scopeHashes)
        return;
    m_scopeHashes = hashes;
    Q_EMIT scopeChanged();
    if (m_active)
        refresh();
}

void SeedConsole::refresh()
{
    refreshSlow();
    refreshFast();
}

QPair<qint64, int> SeedConsole::rangeShape(const QString &range)
{
    if (range == QLatin1String("1h"))
        return {30, 120};
    if (range == QLatin1String("7d"))
        return {3600, 168};
    if (range == QLatin1String("30d"))
        return {6 * 3600, 120};
    if (range == QLatin1String("1y"))
        return {kDay, 365};
    return {600, 144};
}

int SeedConsole::knownSeeds(const Torrent &t)
{
    if (t.hasTrackerError || t.numComplete <= 0)
        return -1;
    return t.numComplete;
}

QString SeedConsole::swarmText(int numComplete)
{
    if (numComplete <= 0)
        return QString();
    if (numComplete == 1)
        return QStringLiteral("only you");
    return Format::count(numComplete);
}

QString SeedConsole::ledgerSince() const
{
    if (!m_ledger || m_ledger->start() <= 0)
        return QString();
    const QDateTime start = QDateTime::fromSecsSinceEpoch(m_ledger->start());
    if (start.secsTo(now()) < kDay)
        return QStringLiteral("ledger began %1").arg(Format::relativeTime(start));
    return QStringLiteral("ledger since %1").arg(english().toString(start.date(), QStringLiteral("d MMM yyyy")));
}

bool SeedConsole::ledgerNew() const
{
    return !m_ledger || m_ledger->start() <= 0 || m_ledger->start() > now().addDays(-1).toSecsSinceEpoch();
}

QList<const Torrent *> SeedConsole::scopeTorrents() const
{
    QList<const Torrent *> out;
    if (!m_torrents)
        return out;
    if (!m_scopeHashes.isEmpty()) {
        for (const QString &hash : m_scopeHashes) {
            if (m_torrents->indexOfHash(hash) >= 0)
                out.append(&m_torrents->torrent(hash));
        }
        return out;
    }
    if (!m_scope) {
        for (int i = 0; i < m_torrents->count(); ++i)
            out.append(&m_torrents->torrentAt(i));
        return out;
    }
    const int rows = m_scope->rowCount();
    out.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        const QString hash = m_scope->data(m_scope->index(i, 0), TorrentModel::HashRole).toString();
        if (m_torrents->indexOfHash(hash) >= 0)
            out.append(&m_torrents->torrent(hash));
    }
    return out;
}

void SeedConsole::refreshFast()
{
    const QDateTime t = now();
    const QList<const Torrent *> scope = scopeTorrents();
    const int total = m_torrents ? m_torrents->count() : 0;
    m_scopeCount = int(scope.size());
    m_scoped = !m_scopeHashes.isEmpty() || m_scopeCount < total;

    // --- attention -------------------------------------------------------------------------
    {
        int errors = 0;
        QHash<QString, int> byHost;
        for (const Torrent *x : scope) {
            if (x->hasTrackerError && x->inGroup(StatusGroup::Seeding)) {
                ++errors;
                ++byHost[x->trackerHostKey()];
            }
        }
        QString host;
        int most = 0;
        for (auto it = byHost.cbegin(); it != byHost.cend(); ++it) {
            if (it.value() > most) {
                most = it.value();
                host = it.key();
            }
        }
        const bool firewalled = m_state && m_state->connectionStatus() == QLatin1String("firewalled");
        QVariantMap a;
        a[QStringLiteral("errors")] = errors;
        a[QStringLiteral("host")] = host;
        a[QStringLiteral("firewalled")] = firewalled;
        if (errors > 0) {
            a[QStringLiteral("title")] = QStringLiteral("%1 earn nothing")
                                             .arg(countText(errors, QStringLiteral("seeding torrent"),
                                                            QStringLiteral("seeding torrents")));
            a[QStringLiteral("detail")] = byHost.size() == 1
                                              ? QStringLiteral("%1 reports an error").arg(host)
                                              : QStringLiteral("trackers report errors, %1 the most").arg(host);
        } else if (firewalled) {
            a[QStringLiteral("title")] = QStringLiteral("Peers cannot reach you");
            a[QStringLiteral("detail")] = QStringLiteral("the port is closed: forward it or turn on UPnP");
        }
        a[QStringLiteral("visible")] = errors > 0 || firewalled;
        m_attention = a;
    }

    // --- totals ----------------------------------------------------------------------------
    {
        qint64 given = 0, taken = 0, offer = 0;
        int seeding = 0;
        QSet<QString> offered;
        for (const Torrent *x : scope) {
            given += x->uploaded;
            taken += x->downloaded;
            if (x->isComplete()) {
                // Cross-seeds share their files: count a content path once (qui does too).
                const QString key = x->contentPath.isEmpty() ? x->hash : x->contentPath;
                if (!offered.contains(key)) {
                    offered.insert(key);
                    offer += x->size;
                }
            }
            if (x->inGroup(StatusGroup::Seeding))
                ++seeding;
        }
        const bool server = !m_scoped && m_state && m_state->allTimeUpload() > 0;
        if (server) {
            given = m_state->allTimeUpload();
            taken = m_state->allTimeDownload();
        }
        QVariantMap m = m_totals;
        m[QStringLiteral("given")] = given;
        m[QStringLiteral("givenText")] = Format::size(given);
        m[QStringLiteral("takenText")] = Format::size(taken);
        m[QStringLiteral("takenNote")] = server ? QStringLiteral("all time") : QStringLiteral("by these torrents");
        m[QStringLiteral("ratioText")] = ratioText(double(given), double(taken));
        m[QStringLiteral("ratioNote")] = server ? QStringLiteral("all time") : QStringLiteral("given ÷ taken");
        m[QStringLiteral("bufferText")] = signedSize(given - taken);
        m[QStringLiteral("bufferNegative")] = given < taken;
        m[QStringLiteral("offerText")] = Format::size(offer);
        m[QStringLiteral("offerNote")] = QStringLiteral("in %1 seeding").arg(Format::count(seeding));
        m_totals = m;
    }

    // --- keeping alive ---------------------------------------------------------------------
    {
        struct B
        {
            QString label;
            int count = 0;
            bool rare = false;
            QString role;
        };
        QList<B> b{{QStringLiteral("only you"), 0, true, QStringLiteral("only")},
                   {QStringLiteral("2 seeds"), 0, m_rareSeeds >= 2, QStringLiteral("two")},
                   {QStringLiteral("3–5"), 0, m_rareSeeds >= 3, QStringLiteral("few")},
                   {QStringLiteral("6–20"), 0, false, QStringLiteral("some")},
                   {QStringLiteral("21+"), 0, false, QStringLiteral("many")},
                   {QStringLiteral("unknown"), 0, false, QStringLiteral("unknown")}};
        QList<const Torrent *> rareRows;
        int rare = 0, only = 0;
        for (const Torrent *x : scope) {
            if (!x->isComplete() || !x->inGroup(StatusGroup::Seeding))
                continue;
            const int s = knownSeeds(*x);
            const int i = s < 0 ? 5 : s == 1 ? 0 : s == 2 ? 1 : s <= 5 ? 2 : s <= 20 ? 3 : 4;
            ++b[i].count;
            if (s > 0 && s <= m_rareSeeds)
                ++rare;
            if (s == 1)
                ++only;
            if (s > 0 && s <= 5)
                rareRows.append(x);
        }
        std::sort(rareRows.begin(), rareRows.end(), [](const Torrent *l, const Torrent *r) {
            if (l->numComplete != r->numComplete)
                return l->numComplete < r->numComplete;
            if (l->numIncomplete != r->numIncomplete)
                return l->numIncomplete > r->numIncomplete;
            return l->name.localeAwareCompare(r->name) < 0;
        });
        QVariantList buckets, rows;
        for (const B &x : b)
            buckets.append(QVariantMap{{QStringLiteral("label"), x.label},
                                       {QStringLiteral("count"), x.count},
                                       {QStringLiteral("rare"), x.rare},
                                       {QStringLiteral("kind"), x.role}});
        for (int i = 0; i < std::min<qsizetype>(kRows, rareRows.size()); ++i) {
            const Torrent *x = rareRows.at(i);
            const int waiting = std::max(0, x->numIncomplete);
            const int quiet = quietDays(*x, t);
            rows.append(QVariantMap{
                {QStringLiteral("hash"), x->hash},
                {QStringLiteral("name"), x->name},
                {QStringLiteral("seedsText"), x->numComplete == 1 ? QStringLiteral("only you")
                                                                  : QStringLiteral("%1 seeds").arg(x->numComplete)},
                {QStringLiteral("rare"), x->numComplete <= m_rareSeeds},
                {QStringLiteral("waitingText"), waiting > 0 ? QStringLiteral("%1 waiting").arg(waiting)
                                                 : quiet >= kQuietDays ? QStringLiteral("quiet %1 d").arg(quiet)
                                                                       : QStringLiteral("0 waiting")},
            });
        }
        QString summary;
        if (rare > 0)
            summary = QStringLiteral("%1 with %2 or fewer · %3 only you")
                          .arg(Format::count(rare),
                               m_rareSeeds == 1 ? QStringLiteral("1 seed")
                                                : QStringLiteral("%1 seeds").arg(m_rareSeeds),
                               Format::count(only));
        m_alive = {{QStringLiteral("buckets"), buckets},
                   {QStringLiteral("rows"), rows},
                   {QStringLiteral("rare"), rare},
                   {QStringLiteral("summary"), summary.isEmpty() ? QStringLiteral("nothing rare here") : summary}};
    }

    // --- ratio ladder ----------------------------------------------------------------------
    {
        QVariantList points;
        QList<double> ratios;
        int below = 0;
        qint64 owed = 0;
        for (const Torrent *x : scope) {
            if (!x->isComplete())
                continue;
            const double r = std::max(0.0, x->ratio);
            ratios.append(r);
            points.append(QVariantMap{{QStringLiteral("ratio"), r},
                                      {QStringLiteral("size"), double(x->size)},
                                      {QStringLiteral("name"), x->name},
                                      {QStringLiteral("ratioText"), Format::ratio(r)},
                                      {QStringLiteral("sizeText"), Format::size(x->size)}});
            if (r < 1.0) {
                ++below;
                owed += std::max<qint64>(0, std::max(x->downloaded, x->size) - x->uploaded);
            }
        }
        std::sort(ratios.begin(), ratios.end());
        QString summary;
        if (!ratios.isEmpty())
            summary = QStringLiteral("%1 below 1.0 · %2 still to give back · median ratio %3")
                          .arg(Format::count(below), Format::size(owed),
                               Format::ratio(ratios.at(ratios.size() / 2)));
        m_ladder = {{QStringLiteral("points"), points},
                    {QStringLiteral("summary"), summary.isEmpty() ? QStringLiteral("nothing complete here") : summary}};
    }

    // --- trackers --------------------------------------------------------------------------
    {
        QSet<QString> inScope;
        for (const Torrent *x : scope)
            inScope.insert(x->hash);
        QHash<QString, QStringList> byHost;
        if (m_trackerModel && m_trackerModel->rowCount() > 0) {
            for (const QString &host : m_trackerModel->hosts())
                byHost.insert(host, m_trackerModel->hashesFor(host));
        } else {
            for (const Torrent *x : scope)
                byHost[x->trackerHostKey()].append(x->hash);
        }
        struct Row
        {
            QVariantMap map;
            qint64 given = 0;
        };
        QList<Row> rows;
        qint64 maxGiven = 1;
        int notWorking = 0;
        const QVariantMap sparks = m_trackers.value(QStringLiteral("sparks")).toMap();
        for (auto it = byHost.cbegin(); it != byHost.cend(); ++it) {
            int n = 0, rare = 0, errors = 0;
            qint64 given = 0, taken = 0, offer = 0;
            for (const QString &hash : it.value()) {
                if (!inScope.contains(hash) || !m_torrents || m_torrents->indexOfHash(hash) < 0)
                    continue;
                const Torrent &x = m_torrents->torrent(hash);
                ++n;
                given += x.uploaded;
                taken += x.downloaded;
                if (x.isComplete())
                    offer += x.size;
                const int s = knownSeeds(x);
                if (x.isComplete() && s > 0 && s <= m_rareSeeds)
                    ++rare;
                if (x.hasTrackerError)
                    ++errors;
            }
            if (n == 0)
                continue;
            QString status = QStringLiteral("working");
            QString role = QStringLiteral("dim");
            if (errors == n) {
                status = QStringLiteral("not working");
                role = QStringLiteral("error");
                ++notWorking;
            } else if (errors > 0) {
                status = QStringLiteral("errors on %1").arg(errors);
                role = QStringLiteral("warn");
            }
            const double seeded = offer > 0 ? double(given) / double(offer) : 0.0;
            Row r;
            r.given = given;
            r.map = {{QStringLiteral("host"), it.key()},
                     {QStringLiteral("torrents"), n},
                     {QStringLiteral("offerText"), Format::size(offer)},
                     {QStringLiteral("given"), given},
                     {QStringLiteral("givenText"), Format::size(given)},
                     {QStringLiteral("bufferText"), signedSize(given - taken)},
                     {QStringLiteral("bufferNegative"), given < taken},
                     {QStringLiteral("seededText"), offer > 0 ? timesText(seeded) : QStringLiteral("—")},
                     {QStringLiteral("seededGood"), seeded >= 1.0},
                     {QStringLiteral("rare"), rare},
                     {QStringLiteral("spark"), sparks.value(it.key())},
                     {QStringLiteral("status"), status},
                     {QStringLiteral("statusRole"), role}};
            maxGiven = std::max(maxGiven, given);
            rows.append(r);
        }
        std::sort(rows.begin(), rows.end(), [](const Row &l, const Row &r) { return l.given > r.given; });
        QVariantList list;
        for (Row &r : rows) {
            r.map[QStringLiteral("fraction")] = double(r.given) / double(maxGiven);
            list.append(r.map);
        }
        QString summary = countText(int(rows.size()), QStringLiteral("tracker"), QStringLiteral("trackers"));
        if (notWorking > 0)
            summary += QStringLiteral(" · %1 not working").arg(notWorking);
        m_trackers = {{QStringLiteral("rows"), list},
                      {QStringLiteral("summary"), summary},
                      {QStringLiteral("sparks"), sparks}};
    }

    // --- holding ---------------------------------------------------------------------------
    {
        QList<const Torrent *> idle;
        qint64 idleSize = 0;
        for (const Torrent *x : scope) {
            if (!x->isComplete())
                continue;
            const int s = knownSeeds(*x);
            if (s > 0 && s <= m_rareSeeds)
                continue; // a rare torrent is never dead weight
            if (quietDays(*x, t) < kQuietDays)
                continue;
            idle.append(x);
            idleSize += x->size;
        }
        std::sort(idle.begin(), idle.end(), [](const Torrent *l, const Torrent *r) { return l->size > r->size; });
        QVariantList rows;
        for (int i = 0; i < std::min<qsizetype>(kRows, idle.size()); ++i)
            rows.append(QVariantMap{{QStringLiteral("hash"), idle.at(i)->hash},
                                    {QStringLiteral("name"), idle.at(i)->name},
                                    {QStringLiteral("sizeText"), Format::size(idle.at(i)->size)},
                                    {QStringLiteral("quietText"),
                                     QStringLiteral("quiet %1 d").arg(quietDays(*idle.at(i), t))}});
        m_holding = {{QStringLiteral("rows"), rows},
                     {QStringLiteral("summary"),
                      idle.isEmpty() ? QStringLiteral("everything here gave something this month")
                                     : QStringLiteral("%1 in %2, quiet for 30 days or more")
                                           .arg(Format::size(idleSize), Format::count(int(idle.size())))}};
    }

    // --- on air ----------------------------------------------------------------------------
    {
        QList<const Torrent *> air;
        qint64 speed = 0;
        int peers = 0;
        for (const Torrent *x : scope) {
            if (x->upspeed > 0) {
                air.append(x);
                speed += x->upspeed;
                peers += std::max(0, x->numLeechs);
            }
        }
        std::sort(air.begin(), air.end(), [](const Torrent *l, const Torrent *r) { return l->upspeed > r->upspeed; });
        const qint64 top = air.isEmpty() ? 1 : std::max<qint64>(1, air.first()->upspeed);
        QVariantList rows;
        for (int i = 0; i < std::min<qsizetype>(kAirRows, air.size()); ++i) {
            const Torrent *x = air.at(i);
            rows.append(QVariantMap{{QStringLiteral("hash"), x->hash},
                                    {QStringLiteral("name"), x->name},
                                    {QStringLiteral("speedText"), Format::speed(x->upspeed)},
                                    {QStringLiteral("fraction"), double(x->upspeed) / double(top)},
                                    {QStringLiteral("peersText"),
                                     countText(std::max(0, x->numLeechs), QStringLiteral("peer"), QStringLiteral("peers"))}});
        }
        m_onAir = {{QStringLiteral("rows"), rows},
                   {QStringLiteral("speedText"), Format::speed(speed)},
                   {QStringLiteral("summary"),
                    air.isEmpty() ? QStringLiteral("quiet")
                                  : QStringLiteral("%1 to %2")
                                        .arg(countText(int(air.size()), QStringLiteral("torrent"), QStringLiteral("torrents")),
                                             countText(peers, QStringLiteral("peer"), QStringLiteral("peers")))}};
    }

    // --- lately ----------------------------------------------------------------------------
    {
        QVariantList out;
        const QVariantList recent = m_recent ? m_recent() : QVariantList();
        QSet<QString> inScope;
        for (const Torrent *x : scope)
            inScope.insert(x->hash);
        for (const QVariant &v : recent) {
            const QVariantMap e = v.toMap();
            const QString hash = e.value(QStringLiteral("hash")).toString();
            if (m_scoped && !inScope.contains(hash))
                continue;
            const QString kind = e.value(QStringLiteral("kind")).toString();
            const QDateTime at = QDateTime::fromSecsSinceEpoch(e.value(QStringLiteral("at")).toLongLong());
            QString what, glyph = QStringLiteral("◆"), role = QStringLiteral("seed");
            if (kind == QLatin1String("milestone")) {
                const int r = e.value(QStringLiteral("ratio")).toInt();
                what = r == 1 ? QStringLiteral("paid back · ratio 1.00") : QStringLiteral("%1× given").arg(r);
            } else if (kind == QLatin1String("completed")) {
                glyph = QStringLiteral("↓");
                role = QStringLiteral("down");
                what = QStringLiteral("finished · %1").arg(Format::size(e.value(QStringLiteral("size")).toLongLong()));
                const QString cat = e.value(QStringLiteral("category")).toString();
                if (!cat.isEmpty())
                    what += QStringLiteral(" · ") + cat;
            } else {
                continue;
            }
            out.append(QVariantMap{{QStringLiteral("hash"), hash},
                                   {QStringLiteral("glyph"), glyph},
                                   {QStringLiteral("role"), role},
                                   {QStringLiteral("name"), e.value(QStringLiteral("name"))},
                                   {QStringLiteral("what"), what},
                                   {QStringLiteral("when"), ago(at, t)}});
        }
        m_lately = out;
    }

    // Totals that come from the ledger (today, streak) were set by refreshSlow.
    Q_EMIT changed();
}

void SeedConsole::refreshSlow()
{
    if (!m_ledger)
        return;
    const QDateTime t = now();
    const qint64 nowSecs = t.toSecsSinceEpoch();
    const QDate today = t.date();
    const QList<const Torrent *> scope = scopeTorrents();
    const int total = m_torrents ? m_torrents->count() : 0;
    const bool scoped = !m_scopeHashes.isEmpty() || int(scope.size()) < total;
    QSet<QString> inScope;
    for (const Torrent *x : scope)
        inScope.insert(x->hash);
    const QLocale en = english();

    // Per torrent per day for the last 371 days: the scoped year, today, streak, 30 days.
    const QDate yearFrom = today.addDays(-370);
    const QHash<QString, QList<qint64>> perTorrent = m_ledger->torrentDays(yearFrom, today);
    const QList<SeedLedger::Day> serverDays = m_ledger->days(yearFrom, today);
    QList<qint64> dayValues(serverDays.size(), 0);
    for (int i = 0; i < serverDays.size(); ++i)
        dayValues[i] = serverDays.at(i).up;
    if (scoped) {
        for (int i = 0; i < dayValues.size(); ++i)
            dayValues[i] = dayValues[i] < 0 ? -1 : 0;
        for (auto it = perTorrent.cbegin(); it != perTorrent.cend(); ++it) {
            if (!inScope.contains(it.key()))
                continue;
            for (int i = 0; i < it->size() && i < dayValues.size(); ++i) {
                if (dayValues[i] >= 0)
                    dayValues[i] += it->at(i);
            }
        }
    }

    // --- the year ----------------------------------------------------------------------
    {
        QVariantList days;
        qint64 yearTotal = 0, best = 0;
        int bestIndex = -1, quiet = 0, longest = 0, run = 0;
        const int first = int(dayValues.size()) - 365;
        for (int i = std::max(0, first); i < dayValues.size(); ++i) {
            const qint64 v = dayValues.at(i);
            days.append(QVariantMap{{QStringLiteral("date"), serverDays.at(i).date.toString(Qt::ISODate)},
                                    {QStringLiteral("value"), double(v)},
                                    {QStringLiteral("text"),
                                     en.toString(serverDays.at(i).date, QStringLiteral("ddd d MMM yyyy"))
                                         + QStringLiteral(" · ")
                                         + (v < 0 ? QStringLiteral("before the ledger")
                                                  : Format::size(v) + QStringLiteral(" given"))}});
            if (v < 0)
                continue;
            yearTotal += v;
            if (v > best) {
                best = v;
                bestIndex = i;
            }
            if (v == 0 && i < dayValues.size() - 1)
                ++quiet;
            run = v > 0 ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        int streak = 0;
        for (int i = int(dayValues.size()) - 1; i >= 0; --i) {
            if (dayValues.at(i) > 0)
                ++streak;
            else if (i == dayValues.size() - 1)
                continue; // today may still be 0
            else
                break;
        }
        const bool fresh = ledgerNew();
        QVariantList facts;
        if (fresh) {
            if (m_state && m_state->allTimeUpload() > 0)
                facts.append(QStringLiteral("before the ledger · <b>%1</b> given (qBittorrent's all-time count)")
                                 .arg(Format::size(m_state->allTimeUpload())));
            facts.append(QStringLiteral("today's cell fills in as you seed"));
        } else {
            facts.append(QStringLiteral("this year · <b>%1</b>").arg(Format::size(yearTotal)));
            if (bestIndex >= 0)
                facts.append(QStringLiteral("best day · <b>%1</b> %2")
                                 .arg(Format::size(best),
                                      en.toString(serverDays.at(bestIndex).date, QStringLiteral("d MMM"))));
            facts.append(QStringLiteral("streak · <b>%1</b> · longest %2")
                             .arg(countText(streak, QStringLiteral("day"), QStringLiteral("days")))
                             .arg(longest));
            facts.append(QStringLiteral("quiet days · <b>%1</b>").arg(quiet));
        }
        m_year = {{QStringLiteral("days"), days},
                  {QStringLiteral("facts"), facts},
                  {QStringLiteral("summary"), fresh ? QStringLiteral("fills in from today")
                                                    : QStringLiteral("a cell per day, the last 53 weeks")}};
        const qint64 todayGiven = std::max<qint64>(0, dayValues.isEmpty() ? 0 : dayValues.constLast());
        m_totals[QStringLiteral("todayText")] = QStringLiteral("+") + Format::size(todayGiven);
        m_totals[QStringLiteral("streak")] = streak;
        m_totals[QStringLiteral("streakText")] = QString::number(streak);
        m_totals[QStringLiteral("streakUnit")] = streak == 1 ? QStringLiteral("day") : QStringLiteral("days");
        m_totals[QStringLiteral("streakNote")] = fresh ? QStringLiteral("new ledger") : QStringLiteral("with upload");
    }

    // --- 30 days: earning and the trackers' sparks ---------------------------------------
    {
        const int from30 = int(dayValues.size()) - 30;
        QHash<QString, qint64> given30;
        for (auto it = perTorrent.cbegin(); it != perTorrent.cend(); ++it) {
            qint64 s = 0;
            for (int i = std::max(0, from30); i < it->size(); ++i)
                s += it->at(i);
            if (s > 0)
                given30.insert(it.key(), s);
        }
        struct E
        {
            const Torrent *t;
            double yield;
            qint64 given;
        };
        QList<E> earn;
        for (const Torrent *x : scope) {
            const qint64 g = given30.value(x->hash);
            if (g > 0 && x->size > 0)
                earn.append({x, double(g) / double(x->size), g});
        }
        std::sort(earn.begin(), earn.end(), [](const E &l, const E &r) { return l.yield > r.yield; });
        const double top = earn.isEmpty() ? 1.0 : std::max(1e-9, earn.first().yield);
        QVariantList rows;
        for (int i = 0; i < std::min<qsizetype>(kRows, earn.size()); ++i)
            rows.append(QVariantMap{{QStringLiteral("hash"), earn.at(i).t->hash},
                                    {QStringLiteral("name"), earn.at(i).t->name},
                                    {QStringLiteral("fraction"), earn.at(i).yield / top},
                                    {QStringLiteral("yieldText"),
                                     timesText(earn.at(i).yield) + QStringLiteral(" · ")
                                         + Format::size(earn.at(i).given)}});
        m_earning = {{QStringLiteral("rows"), rows},
                     {QStringLiteral("empty"),
                      ledgerNew() ? QStringLiteral("From tomorrow: it needs a day of the ledger.")
                                  : QStringLiteral("Nothing here gave anything in 30 days.")}};

        QVariantMap sparks;
        if (m_trackerModel && m_trackerModel->rowCount() > 0) {
            for (const QString &host : m_trackerModel->hosts()) {
                QVariantList s;
                QList<double> sum(30, 0.0);
                bool any = false;
                for (const QString &hash : m_trackerModel->hashesFor(host)) {
                    if (!inScope.contains(hash))
                        continue;
                    const auto it = perTorrent.constFind(hash);
                    if (it == perTorrent.cend())
                        continue;
                    for (int d = 0; d < 30; ++d) {
                        const int i = from30 + d;
                        if (i >= 0 && i < it->size())
                            sum[d] += double(it->at(i));
                    }
                    any = true;
                }
                for (double v : sum)
                    s.append(v);
                if (any)
                    sparks.insert(host, s);
            }
        }
        m_trackers[QStringLiteral("sparks")] = sparks;
    }

    // --- the tide ----------------------------------------------------------------------
    {
        const auto [bucket, count] = rangeShape(m_range);
        QList<SeedLedger::Point> points;
        bool wholeServer = false;
        if (scoped && bucket >= kDay / 4) {
            // A part of the library: only days are kept per torrent, so 30 d and 1 y read
            // those (30 d as days, not quarters); shorter ranges show the whole server.
            const int n = bucket >= kDay ? count : 30;
            for (int i = 0; i < n; ++i) {
                const int di = int(dayValues.size()) - n + i;
                const qint64 v = di >= 0 ? dayValues.at(di) : -1;
                SeedLedger::Point p;
                p.start = today.addDays(-(n - 1 - i)).startOfDay().toSecsSinceEpoch();
                p.known = v >= 0;
                p.up = v > 0 ? double(v) / double(kDay) : 0.0;
                points.append(p);
            }
        } else {
            points = m_ledger->series(nowSecs, bucket, count);
            wholeServer = scoped;
        }
        const qint64 step = (scoped && bucket >= kDay / 4) ? kDay : bucket;
        QVariantList list, labels;
        double upSum = 0, downSum = 0, peak = 0;
        int peakIndex = -1;
        const QString format = step >= kDay ? (m_range == QLatin1String("1y") ? QStringLiteral("MMM") : QStringLiteral("d MMM"))
                               : step >= 3600 ? (m_range == QLatin1String("7d") ? QStringLiteral("ddd") : QStringLiteral("d MMM"))
                                              : QStringLiteral("HH:mm");
        const QString readFormat = step >= kDay ? QStringLiteral("ddd d MMM") : QStringLiteral("ddd HH:mm");
        for (int i = 0; i < points.size(); ++i) {
            const SeedLedger::Point &p = points.at(i);
            const QDateTime when = QDateTime::fromSecsSinceEpoch(p.start);
            QVariantMap m{{QStringLiteral("gap"), p.gap},
                          {QStringLiteral("whenText"), en.toString(when, readFormat)}};
            if (p.known) {
                m[QStringLiteral("up")] = p.up;
                m[QStringLiteral("down")] = p.down;
                m[QStringLiteral("upText")] = Format::speed(qint64(p.up));
                m[QStringLiteral("downText")] = Format::speed(qint64(p.down));
                m[QStringLiteral("volumeText")] = Format::size(qint64(p.up * double(step)));
                upSum += p.up * double(step);
                downSum += p.down * double(step);
                if (p.up > peak) {
                    peak = p.up;
                    peakIndex = i;
                }
            } else {
                m[QStringLiteral("up")] = QVariant();
                m[QStringLiteral("down")] = QVariant();
            }
            list.append(m);
        }
        const int ticks = m_range == QLatin1String("7d") ? 7 : m_range == QLatin1String("1y") ? 12 : 6;
        for (int k = 0; k < ticks && !points.isEmpty(); ++k) {
            const int idx = int(std::lround(double(k) * double(points.size() - 1) / double(ticks)));
            labels.append(QVariantMap{{QStringLiteral("index"), idx},
                                      {QStringLiteral("text"),
                                       en.toString(QDateTime::fromSecsSinceEpoch(points.at(idx).start), format)}});
        }
        const bool any = std::any_of(points.cbegin(), points.cend(), [](const auto &p) { return p.known; });
        m_tide = {{QStringLiteral("points"), list},
                  {QStringLiteral("labels"), labels},
                  {QStringLiteral("step"), step},
                  {QStringLiteral("peakIndex"), peakIndex},
                  {QStringLiteral("wholeServer"), wholeServer},
                  {QStringLiteral("upText"), Format::size(qint64(upSum))},
                  {QStringLiteral("downText"), Format::size(qint64(downSum))},
                  {QStringLiteral("any"), any},
                  {QStringLiteral("range"), m_range}};
    }
    Q_EMIT slowChanged();
}

void SeedConsole::refreshHero()
{
    if (!m_ledger)
        return;
    const QDateTime t = now();
    const QList<qint64> recent = m_ledger->recentUpload(t.toSecsSinceEpoch(), 60);
    QVariantList spark;
    spark.reserve(recent.size());
    for (qint64 v : recent)
        spark.append(double(v));
    m_heroSpark = spark;
    if (m_todayText.isEmpty() || ++m_heroTick % 15 == 0) {
        const auto days = m_ledger->days(t.date(), t.date());
        const qint64 today = days.isEmpty() ? 0 : std::max<qint64>(0, days.first().up);
        m_todayText = Format::size(today) + QStringLiteral(" today");
    }
    Q_EMIT heroChanged();
}

QVariantMap SeedConsole::selectionSummary(const QStringList &hashes) const
{
    qint64 size = 0, given = 0, taken = 0;
    int n = 0;
    if (m_torrents) {
        for (const QString &hash : hashes) {
            if (m_torrents->indexOfHash(hash) < 0)
                continue;
            const Torrent &x = m_torrents->torrent(hash);
            ++n;
            size += x.size;
            given += x.uploaded;
            taken += x.downloaded;
        }
    }
    return {{QStringLiteral("count"), n},
            {QStringLiteral("sizeText"), Format::size(size)},
            {QStringLiteral("givenText"), Format::size(given)},
            {QStringLiteral("ratioText"), ratioText(double(given), double(taken))},
            {QStringLiteral("ratioGood"), taken > 0 && given >= taken}};
}

QVariantMap SeedConsole::rarity(const QStringList &hashes) const
{
    int only = 0, rare = 0, waiting = 0, seeds = -1;
    QString name;
    if (m_torrents) {
        for (const QString &hash : hashes) {
            if (m_torrents->indexOfHash(hash) < 0)
                continue;
            const Torrent &x = m_torrents->torrent(hash);
            if (!x.isComplete())
                continue;
            const int s = knownSeeds(x);
            if (hashes.size() == 1) {
                name = x.name;
                seeds = s;
            }
            if (s > 0 && s <= m_rareSeeds) {
                ++rare;
                waiting += std::max(0, x.numIncomplete);
            }
            if (s == 1)
                ++only;
        }
    }
    return {{QStringLiteral("onlyYou"), only},
            {QStringLiteral("rare"), rare},
            {QStringLiteral("waiting"), waiting},
            {QStringLiteral("name"), name},
            {QStringLiteral("seeds"), seeds}};
}

} // namespace omaseed::core
