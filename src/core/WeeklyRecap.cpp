#include "core/WeeklyRecap.hpp"

#include "core/Format.hpp"
#include "core/SeedConsole.hpp"
#include "core/SeedLedger.hpp"
#include "core/Torrent.hpp"

#include <QLocale>
#include <QStringList>

namespace omaseed::core {

WeeklyRecap WeeklyRecap::compute(const SeedLedger &ledger,
                                 const QList<const Torrent *> &library,
                                 const QDate &today,
                                 qint64 allTimeUp,
                                 qint64 allTimeDown,
                                 int rareSeeds)
{
    WeeklyRecap r;
    r.to = today;
    r.from = today.addDays(-6);
    r.ratioText = SeedConsole::ratioText(double(allTimeUp), double(allTimeDown));
    r.streak = ledger.streak(today);

    qint64 best = 0;
    for (const SeedLedger::Day &day : ledger.days(r.from, r.to)) {
        if (day.up <= 0)
            continue;
        r.given += day.up;
        if (day.up > best) {
            best = day.up;
            r.bestDay = day.date;
        }
    }

    const QHash<QString, qint64> week = ledger.torrentTotals(r.from, r.to);
    for (const Torrent *t : library) {
        // The console's keeping-alive block: complete, seeding, few seeds in the swarm.
        const int seeds = SeedConsole::knownSeeds(*t);
        if (t->isComplete() && t->inGroup(StatusGroup::Seeding) && seeds > 0 && seeds <= rareSeeds)
            ++r.rareAlive;
        // The ratio grows with what was uploaded (the downloaded side stays put while seeding),
        // so before this week's bytes it was ratio × (uploaded − week) ÷ uploaded.
        const qint64 gave = week.value(t->hash);
        if (gave > 0 && t->uploaded > 0 && t->ratio >= 1.0 &&
            t->ratio * double(t->uploaded - gave) / double(t->uploaded) < 1.0)
            ++r.paidBack;
    }
    return r;
}

bool WeeklyRecap::due(const QDateTime &now, const QDate &lastSent)
{
    const QDate today = now.date();
    return today.dayOfWeek() == Qt::Sunday && now.time().hour() >= eveningHour && lastSent != today;
}

QString WeeklyRecap::summary() const
{
    return QStringLiteral("Your week");
}

QString WeeklyRecap::body() const
{
    QStringList first{QStringLiteral("%1 given this week").arg(Format::size(given))};
    if (ratioText != QStringLiteral("—"))
        first << QStringLiteral("ratio %1").arg(ratioText);
    if (streak >= 2)
        first << QStringLiteral("a %1-day streak").arg(streak);

    QStringList second;
    if (rareAlive > 0)
        second
            << (rareAlive == 1
                    ? QStringLiteral("kept 1 rare torrent alive")
                    : QStringLiteral("kept %1 rare torrents alive").arg(Format::count(rareAlive)));
    if (paidBack > 0)
        second << QStringLiteral("%1 paid back").arg(Format::count(paidBack));
    if (bestDay.isValid())
        second << QStringLiteral("best day %1")
                      .arg(QLocale(QLocale::English).dayName(bestDay.dayOfWeek()));

    const QString dot = QStringLiteral(" · ");
    return second.isEmpty() ? first.join(dot) : first.join(dot) + u'\n' + second.join(dot);
}

} // namespace omaseed::core
