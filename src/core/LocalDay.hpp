#pragma once

// LocalDay — today, by the local clock: when it began, and a signal when it ends.
//
// The rail's "landed today" (DECISIONS C24 H2) is the torrents finished since local midnight,
// so its count and its rows change at midnight with no poll. FilterCounts and
// TorrentFilterModel read start() on every pass and re-evaluate on dayChanged(). A coarse
// timer fires just after each local midnight; a woken laptop's late timer still finds the new
// date, since check() compares dates rather than counting time.
//
// The clock is injectable, so a test can stand at 23:59 and step past midnight without
// waiting: setClock(), then check().

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>

namespace omaseed::core {

class LocalDay : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;

    explicit LocalDay(QObject *parent = nullptr);

    /// Where now() comes from; the default is QDateTime::currentDateTime(). Takes the clock's
    /// date as today without a dayChanged().
    void setClock(Clock clock);
    QDateTime now() const;
    /// Today's local midnight.
    QDateTime start() const;
    /// start() in milliseconds since the epoch, for comparisons in a tight loop.
    qint64 startMSecs() const { return m_startMSecs; }

    /// Reads the clock: on a new date, takes it and emits dayChanged(). Arms the timer for
    /// the next midnight either way. The timer calls it.
    void check();

    /// Milliseconds from `now` to just after the next local midnight (a second late, so the
    /// new date is current when it fires).
    static qint64 msecsToNextMidnight(const QDateTime &now);

Q_SIGNALS:
    void dayChanged();

private:
    void take(const QDate &date);
    void arm(const QDateTime &now);

    Clock m_clock;
    QDate m_date;
    qint64 m_startMSecs = 0;
    QTimer m_timer;
};

} // namespace omaseed::core
