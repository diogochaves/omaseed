#include "core/LocalDay.hpp"

#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <chrono>

namespace omaseed::core {

LocalDay::LocalDay(QObject *parent)
    : QObject(parent)
    , m_clock([]() { return QDateTime::currentDateTime(); })
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &LocalDay::check);
    const QDateTime current = now();
    take(current.date());
    arm(current);
}

void LocalDay::setClock(Clock clock)
{
    m_clock = clock ? std::move(clock) : Clock([]() { return QDateTime::currentDateTime(); });
    const QDateTime current = now();
    take(current.date());
    arm(current);
}

QDateTime LocalDay::now() const
{
    return m_clock();
}

QDateTime LocalDay::start() const
{
    return QDateTime(m_date, QTime(0, 0));
}

void LocalDay::check()
{
    const QDateTime current = now();
    const bool changed = current.date() != m_date;
    if (changed)
        take(current.date());
    arm(current);
    if (changed)
        Q_EMIT dayChanged();
}

qint64 LocalDay::msecsToNextMidnight(const QDateTime &now)
{
    const QDateTime midnight(now.date().addDays(1), QTime(0, 0), now.timeZone());
    return std::max<qint64>(1000, now.msecsTo(midnight) + 1000);
}

void LocalDay::take(const QDate &date)
{
    m_date = date;
    // A zone whose clocks skip midnight starts the day at the first time that exists.
    m_startMSecs = start().toMSecsSinceEpoch();
}

void LocalDay::arm(const QDateTime &now)
{
    m_timer.start(std::chrono::milliseconds(msecsToNextMidnight(now)));
}

} // namespace omaseed::core
