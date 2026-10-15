#include "core/AltSpeed.hpp"

#include "core/GlobalState.hpp"
#include "core/ServerPreferences.hpp"

namespace omaseed::core {

namespace {

qint64 limit(const QVariantMap &values, const char *key)
{
    return values.value(QLatin1String(key)).toLongLong();
}

bool hasSets(const QVariantMap &values)
{
    for (const char *key : {"dl_limit", "up_limit", "alt_dl_limit", "alt_up_limit"}) {
        if (!values.contains(QLatin1String(key)))
            return false;
    }
    return true;
}

} // namespace

AltSpeed::AltSpeed(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &AltSpeed::refresh);
    refresh();
}

void AltSpeed::setPreferences(ServerPreferences *preferences)
{
    if (m_preferences)
        disconnect(m_preferences, nullptr, this, nullptr);
    m_preferences = preferences;
    if (preferences) {
        connect(preferences, &ServerPreferences::valuesChanged, this, &AltSpeed::refresh);
        connect(preferences, &ServerPreferences::stateChanged, this, &AltSpeed::refresh);
    }
    refresh();
}

void AltSpeed::setGlobalState(GlobalState *state)
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
    m_state = state;
    if (state)
        connect(state, &GlobalState::useAltSpeedLimitsChanged, this, &AltSpeed::refresh);
    refresh();
}

void AltSpeed::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    refresh();
}

QDateTime AltSpeed::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void AltSpeed::refresh()
{
    const QStringList before{kind(),
                             setKind(),
                             QString::number(int(m_on)),
                             QString::number(int(isScheduled())),
                             QString::number(int(m_reading.byHand)),
                             m_reading.until.toString(Qt::ISODate),
                             chipTooltip(),
                             footerNote(),
                             trayLabel(),
                             m_summary.text,
                             m_summary.note,
                             QString::number(int(m_summary.warn))};

    m_now = now();
    m_on = m_state && m_state->useAltSpeedLimits();
    const bool loaded = m_preferences && m_preferences->isLoaded();
    const QVariantMap values = m_preferences ? m_preferences->values() : QVariantMap();
    m_kind = loaded && hasSets(values) ? SpeedSets::kind(limit(values, "dl_limit"),
                                                         limit(values, "up_limit"),
                                                         limit(values, "alt_dl_limit"),
                                                         limit(values, "alt_up_limit"))
                                       : SpeedSets::Kind::Unknown;
    m_schedule = loaded ? AltSchedule::fromPreferences(values) : AltSchedule();
    m_reading = m_schedule.read(m_on, m_now);
    m_summary = m_schedule.summary(word(), m_on, m_now);

    const QStringList after{kind(),
                            setKind(),
                            QString::number(int(m_on)),
                            QString::number(int(isScheduled())),
                            QString::number(int(m_reading.byHand)),
                            m_reading.until.toString(Qt::ISODate),
                            chipTooltip(),
                            footerNote(),
                            trayLabel(),
                            m_summary.text,
                            m_summary.note,
                            QString::number(int(m_summary.warn))};
    armTimer();
    if (before != after)
        Q_EMIT changed();
}

void AltSpeed::armTimer()
{
    // Half past and on the minute (and a second after), so the schedule's boundaries and the
    // 30 s qBittorrent takes to act on them are both seen soon after they pass.
    const QTime time = m_now.isValid() ? m_now.time() : QTime::currentTime();
    const int intoHalf = (time.second() % 30) * 1000 + time.msec();
    m_timer.start(30 * 1000 - intoHalf + 1000);
}

QString AltSpeed::setKind() const
{
    switch (m_kind) {
    case SpeedSets::Kind::Burst:
        return QStringLiteral("burst");
    case SpeedSets::Kind::Turtle:
        return QStringLiteral("turtle");
    case SpeedSets::Kind::Mixed:
        return QStringLiteral("mixed");
    case SpeedSets::Kind::Same:
        return QStringLiteral("same");
    case SpeedSets::Kind::Unknown:
        break;
    }
    return {};
}

QString AltSpeed::when() const
{
    return AltSchedule::whenText(m_reading.until, m_now);
}

QString AltSpeed::untilText() const
{
    if (!m_on)
        return {};
    return AltSchedule::untilText(m_reading.until, m_now);
}

QString AltSpeed::stateUntilText() const
{
    return AltSchedule::untilText(m_reading.until, m_now);
}

QString AltSpeed::chipTooltip() const
{
    const QString name = word();
    const bool alt = m_kind != SpeedSets::Kind::Burst && m_kind != SpeedSets::Kind::Turtle;
    const bool scheduled = isScheduled() && m_reading.until.isValid();
    if (!m_on) {
        if (scheduled && m_reading.byHand)
            return tr("off by hand · the schedule starts %1 at %2 · A").arg(name, when());
        if (scheduled)
            return tr("%1 at %2, by the schedule · A starts it now").arg(name, when());
        return tr("%1 · A").arg(name);
    }
    if (scheduled && m_reading.byHand) {
        if (alt)
            return tr("alt-speed on by hand · until %1 · A").arg(when());
        return tr("%1 by hand · the schedule ends it at %2 · A").arg(name, when());
    }
    if (scheduled)
        return alt ? tr("alt-speed on until %1, by the schedule · A").arg(when())
                   : tr("%1 until %2, by the schedule · A").arg(name, when());
    return tr("%1 on · A").arg(name);
}

QString AltSpeed::footerNote() const
{
    if (!isScheduled() || !m_reading.byHand || !m_reading.until.isValid())
        return {};
    if (m_on)
        return tr("%1 on by hand · the schedule ends it at %2").arg(word(), when());
    return tr("%1 off by hand · back at %2 by the schedule").arg(word(), when());
}

QString AltSpeed::trayLabel() const
{
    if (m_kind != SpeedSets::Kind::Burst && m_kind != SpeedSets::Kind::Turtle)
        return tr("alternative speed limits");
    const QString end = untilText();
    return end.isEmpty() ? word() : tr("%1 · until %2").arg(word(), end);
}

QString AltSpeed::setLine() const
{
    switch (m_kind) {
    case SpeedSets::Kind::Burst:
        return tr("Faster than your limits above, so omaseed calls this set burst.");
    case SpeedSets::Kind::Turtle:
        return tr("Slower than your limits above, so omaseed calls this set turtle.");
    case SpeedSets::Kind::Mixed:
        return tr("Faster one way and slower the other, so omaseed calls it alt-speed.");
    case SpeedSets::Kind::Same:
        return tr("The same as your limits above: switching changes nothing.");
    case SpeedSets::Kind::Unknown:
        break;
    }
    return {};
}

QString AltSpeed::setGlyph() const
{
    switch (m_kind) {
    case SpeedSets::Kind::Burst:
        return QStringLiteral("▲");
    case SpeedSets::Kind::Turtle:
        return QStringLiteral("▼");
    case SpeedSets::Kind::Mixed:
        return QStringLiteral("⇅");
    case SpeedSets::Kind::Same:
        return QStringLiteral("=");
    case SpeedSets::Kind::Unknown:
        break;
    }
    return {};
}

} // namespace omaseed::core
