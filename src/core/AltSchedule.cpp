#include "core/AltSchedule.hpp"

#include <QCoreApplication>
#include <QLocale>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AltSchedule", text);
}

QString clock(int minute)
{
    return QStringLiteral("%1:%2")
        .arg(minute / 60, 2, 10, QLatin1Char('0'))
        .arg(minute % 60, 2, 10, QLatin1Char('0'));
}

QString dayName(int days)
{
    // scheduler_days 3–9 are Monday to Sunday.
    return QLocale::c().dayName(days - 2, QLocale::LongFormat);
}

QString capitalised(const QString &word)
{
    return word.isEmpty() ? word : word.left(1).toUpper() + word.mid(1);
}

} // namespace

AltSchedule::AltSchedule(bool enabled, int fromMinute, int toMinute, int days)
    : m_known(true)
    , m_enabled(enabled)
    , m_from(fromMinute)
    , m_to(toMinute)
    , m_days(days)
{}

AltSchedule AltSchedule::fromPreferences(const QVariantMap &preferences)
{
    const auto value = [&preferences](const char *key) {
        return preferences.value(QLatin1String(key));
    };
    AltSchedule schedule;
    for (const char *key : {"schedule_from_hour",
                            "schedule_from_min",
                            "schedule_to_hour",
                            "schedule_to_min",
                            "scheduler_days"}) {
        if (!preferences.contains(QLatin1String(key)))
            return schedule;
    }
    schedule.m_known = true;
    schedule.m_enabled = value("scheduler_enabled").toBool();
    schedule.m_from = value("schedule_from_hour").toInt() * 60 + value("schedule_from_min").toInt();
    schedule.m_to = value("schedule_to_hour").toInt() * 60 + value("schedule_to_min").toInt();
    schedule.m_days = value("scheduler_days").toInt();
    return schedule;
}

bool AltSchedule::dayPicked(int dayOfWeek) const
{
    switch (m_days) {
    case EveryDay:
        return true;
    case Weekdays:
        return dayOfWeek != 6 && dayOfWeek != 7;
    case Weekends:
        return dayOfWeek == 6 || dayOfWeek == 7;
    default:
        return dayOfWeek == m_days - 2;
    }
}

bool AltSchedule::onAt(const QDateTime &when) const
{
    if (!m_known || never())
        return false;
    const QTime time = when.time();
    const int minute = time.hour() * 60 + time.minute();
    const bool picked = dayPicked(when.date().dayOfWeek());
    if (m_from < m_to)
        return picked && m_from <= minute && minute < m_to;
    // Past midnight: on, except in the daytime between them on a picked day.
    return !(picked && m_to <= minute && minute < m_from);
}

QDateTime AltSchedule::nextChange(const QDateTime &from, bool state, int days) const
{
    if (!m_known || !from.isValid())
        return {};
    bool last = onAt(from);
    // The next whole minute, then one minute at a time (the local clock's minutes, so a
    // daylight-saving change is walked as the clock shows it).
    const QTime time = from.time();
    QDateTime at = from.addMSecs(-qint64(time.second()) * 1000 - time.msec()).addSecs(60);
    const QDateTime end = from.addDays(days);
    for (; at <= end; at = at.addSecs(60)) {
        const bool now = onAt(at);
        if (now == last)
            continue;
        last = now;
        if (now != state)
            return at;
    }
    return {};
}

AltSchedule::Reading AltSchedule::read(bool on, const QDateTime &now) const
{
    Reading reading;
    if (!m_known || !m_enabled)
        return reading;
    if (onAt(now) == on) {
        reading.until = nextChange(now, on);
        return reading;
    }
    // The schedule changed in the last 30 s and qBittorrent has not looked yet.
    const QDateTime look = now.addSecs(-lookSeconds);
    if (onAt(look) == on) {
        reading.until = nextChange(look, on);
        return reading;
    }
    reading.byHand = true;
    reading.until = nextChange(now, on);
    return reading;
}

QString AltSchedule::untilText(const QDateTime &until, const QDateTime &now)
{
    if (!until.isValid())
        return {};
    if (now.secsTo(until) > 24 * 60 * 60)
        return QLocale::c().toString(until, QStringLiteral("ddd HH:mm"));
    return QLocale::c().toString(until, QStringLiteral("HH:mm"));
}

bool AltSchedule::isTomorrow(const QDateTime &until, const QDateTime &now)
{
    return until.isValid() && now.secsTo(until) <= 24 * 60 * 60 &&
           until.date() == now.date().addDays(1);
}

QString AltSchedule::whenText(const QDateTime &until, const QDateTime &now)
{
    const QString text = untilText(until, now);
    return isTomorrow(until, now) ? tr("%1 tomorrow").arg(text) : text;
}

AltSchedule::Summary AltSchedule::summary(const QString &word, bool on, const QDateTime &now) const
{
    Summary summary;
    if (!m_known)
        return summary;
    if (never()) {
        summary.text = tr("Never on: from and to are the same time.");
        return summary;
    }
    const QString name = capitalised(word);
    const QString from = clock(m_from);
    const QString to = clock(m_to);
    if (backwards()) {
        // docs/31 P2: say what will happen, and why it is not what the days say.
        if (m_days == Weekdays)
            summary.text =
                tr("%1 all day Saturday and Sunday, and Monday to Friday nights.").arg(name);
        else if (m_days == Weekends)
            summary.text =
                tr("%1 all day Monday to Friday, and Saturday and Sunday nights.").arg(name);
        else
            summary.text =
                tr("%1 all day every day but %2, and %2 nights.").arg(name, dayName(m_days));
        summary.note =
            tr("For a time that passes midnight, qBittorrent reads the days the other "
               "way round: the days you pick are the ones it holds back in the daytime.");
        summary.warn = true;
        return summary;
    }
    QString when;
    if (overnight())
        when = tr("%1 every night from %2 to %3").arg(name, from, to);
    else if (m_days == EveryDay)
        when = tr("%1 every day from %2 to %3").arg(name, from, to);
    else if (m_days == Weekdays)
        when = tr("%1 Monday to Friday from %2 to %3").arg(name, from, to);
    else if (m_days == Weekends)
        when = tr("%1 on Saturday and Sunday from %2 to %3").arg(name, from, to);
    else
        when = tr("%1 on %2s from %3 to %4").arg(name, dayName(m_days), from, to);
    summary.text = tr("%1, by the server's clock.").arg(when);
    const bool scheduled = onAt(now);
    const QDateTime next = nextChange(now, scheduled);
    if (next.isValid())
        summary.text +=
            QLatin1Char(' ') +
            (scheduled ? tr("Next: off at %1.") : tr("Next: on at %1.")).arg(whenText(next, now));
    if (m_enabled)
        summary.note = tr("A switches it by hand. The schedule takes over at its next change.");
    else if (scheduled && !on)
        summary.note = tr("Ticked now, it starts %1 at once.").arg(word);
    else if (!scheduled && on)
        summary.note = tr("Ticked now, it ends %1 at once.").arg(word);
    return summary;
}

} // namespace omaseed::core
