#include "core/AltPresets.hpp"

#include "core/AltSchedule.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/ServerPreferences.hpp"

#include <QEventLoop>
#include <QJsonValue>

#include <memory>

namespace omaseed::core {

namespace {

const QString kTurtleDl = QStringLiteral("alt_dl_limit");
const QString kTurtleUp = QStringLiteral("alt_up_limit");
const QString kBurstDl = QStringLiteral("burst_dl_limit");
const QString kBurstUp = QStringLiteral("burst_up_limit");
const QString kBurstMinutes = QStringLiteral("burst_minutes");

const QString kRunning = QStringLiteral("running");

qint64 limitOf(const QVariant &value)
{
    // qBittorrent sends -1 or 0 for no limit; both read as 0 here.
    return qMax<qint64>(0, value.toLongLong());
}

int minutesStep(int minutes)
{
    return minutes == 30 || minutes == 0 ? minutes : AltPresets::kDefaultBurstMinutes;
}

} // namespace

AltPresets::AltPresets(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!burstOn())
            return;
        const QDateTime until = burstUntil();
        if (until.isValid() && until > now())
            armTimer(); // a long burst: the timer waits a day at most
        else
            endBurst();
    });
}

void AltPresets::setClient(QbtClient *client)
{
    m_client = client;
}

void AltPresets::setPreferences(ServerPreferences *preferences)
{
    if (m_preferences == preferences)
        return;
    if (m_preferences)
        disconnect(m_preferences, nullptr, this, nullptr);
    m_preferences = preferences;
    if (m_preferences) {
        connect(m_preferences, &ServerPreferences::valuesChanged, this, [this]() {
            recover();
            Q_EMIT changed();
        });
        connect(m_preferences, &ServerPreferences::stateChanged, this, [this]() {
            recover();
            Q_EMIT changed();
        });
        connect(m_preferences, &ServerPreferences::pendingChanged, this, &AltPresets::changed);
    }
    Q_EMIT changed();
}

void AltPresets::setGlobalState(GlobalState *state)
{
    if (m_state == state)
        return;
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
    m_state = state;
    if (m_state)
        connect(m_state, &GlobalState::useAltSpeedLimitsChanged, this, &AltPresets::changed);
    Q_EMIT changed();
}

void AltPresets::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void AltPresets::setServer(const QString &id)
{
    if (m_server == id)
        return;
    m_server = id;
    m_recovered = false;
    m_timer.stop();
    Q_EMIT changed();
    recover();
}

void AltPresets::setStore(const QJsonObject &store)
{
    m_store = store;
    m_recovered = false;
    Q_EMIT changed();
    recover();
}

QDateTime AltPresets::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

QJsonObject AltPresets::entry() const
{
    return m_server.isEmpty() ? QJsonObject() : m_store.value(m_server).toObject();
}

void AltPresets::setEntry(const QJsonObject &entry)
{
    if (m_server.isEmpty())
        return;
    m_store.insert(m_server, entry);
    Q_EMIT storeChanged();
    Q_EMIT changed();
}

QJsonObject AltPresets::running() const
{
    return entry().value(kRunning).toObject();
}

QString AltPresets::current() const
{
    if (!m_state || !m_state->useAltSpeedLimits())
        return QStringLiteral("off");
    return burstOn() ? QStringLiteral("burst") : QStringLiteral("turtle");
}

bool AltPresets::burstOn() const
{
    return !running().isEmpty();
}

QDateTime AltPresets::burstUntil() const
{
    const QString until = running().value(QStringLiteral("until")).toString();
    return until.isEmpty() ? QDateTime() : QDateTime::fromString(until, Qt::ISODate);
}

QString AltPresets::lastPick() const
{
    return entry().value(QStringLiteral("lastPick")).toString() == QLatin1String("burst")
               ? QStringLiteral("burst")
               : QStringLiteral("turtle");
}

int AltPresets::burstMinutes() const
{
    return minutesStep(entry().value(QStringLiteral("burstMinutes")).toInt(kDefaultBurstMinutes));
}

void AltPresets::setBurstMinutes(int minutes)
{
    if (m_server.isEmpty() || burstMinutes() == minutesStep(minutes))
        return;
    QJsonObject e = entry();
    e.insert(QStringLiteral("burstMinutes"), minutesStep(minutes));
    setEntry(e);
}

qint64 AltPresets::serverLimit(const char *key) const
{
    if (!m_preferences || !m_preferences->isLoaded())
        return -1;
    const QVariant value = m_preferences->value(QString::fromLatin1(key));
    return value.isValid() ? limitOf(value) : -1;
}

qint64 AltPresets::turtleDl() const
{
    const QJsonObject run = running();
    return run.isEmpty() ? qMax<qint64>(0, serverLimit("alt_dl_limit"))
                         : run.value(QStringLiteral("turtleDl")).toInteger();
}

qint64 AltPresets::turtleUp() const
{
    const QJsonObject run = running();
    return run.isEmpty() ? qMax<qint64>(0, serverLimit("alt_up_limit"))
                         : run.value(QStringLiteral("turtleUp")).toInteger();
}

qint64 AltPresets::burstDl() const
{
    return qMax<qint64>(0, entry().value(QStringLiteral("burstDl")).toInteger(0));
}

qint64 AltPresets::burstUp() const
{
    return qMax<qint64>(0, entry().value(QStringLiteral("burstUp")).toInteger(kDefaultBurstUp));
}

QVariantMap AltPresets::values() const
{
    if (!isLoaded())
        return {};
    return {{kTurtleDl, turtleDl()},
            {kTurtleUp, turtleUp()},
            {kBurstDl, burstDl()},
            {kBurstUp, burstUp()},
            {kBurstMinutes, burstMinutes()}};
}

bool AltPresets::isLoaded() const
{
    return !m_server.isEmpty() && m_preferences && m_preferences->isLoaded();
}

QStringList AltPresets::pendingKeys() const
{
    if (!m_preferences || burstOn())
        return {};
    QStringList keys;
    for (const QString &key : m_preferences->pendingKeys())
        if (key == kTurtleDl || key == kTurtleUp)
            keys << key;
    return keys;
}

bool AltPresets::has(const QString &key) const
{
    return isLoaded() && values().contains(key);
}

bool AltPresets::owns(const QVariantMap &changes) const
{
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
        if (it.key() == kTurtleDl || it.key() == kTurtleUp || it.key() == kBurstDl ||
            it.key() == kBurstUp || it.key() == kBurstMinutes)
            return true;
    return false;
}

void AltPresets::apply(const QVariantMap &changes)
{
    QVariantMap rest;
    QJsonObject e = entry();
    QJsonObject run = e.value(kRunning).toObject();
    bool burstMoved = false;
    bool touched = false;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const QString &key = it.key();
        if (key == kBurstDl || key == kBurstUp) {
            e.insert(key == kBurstDl ? QStringLiteral("burstDl") : QStringLiteral("burstUp"),
                     limitOf(it.value()));
            burstMoved = true;
            touched = true;
        } else if (key == kBurstMinutes) {
            e.insert(QStringLiteral("burstMinutes"), minutesStep(it.value().toInt()));
            touched = true;
        } else if ((key == kTurtleDl || key == kTurtleUp) && !run.isEmpty()) {
            // While a burst runs the set holds burst's limits: the turtle is the saved copy,
            // written back when the burst ends.
            run.insert(key == kTurtleDl ? QStringLiteral("turtleDl") : QStringLiteral("turtleUp"),
                       limitOf(it.value()));
            touched = true;
        } else {
            rest.insert(key, it.value());
        }
    }
    if (burstMoved && !run.isEmpty()) {
        // A running burst takes its new limits at once.
        run.insert(QStringLiteral("dl"), e.value(QStringLiteral("burstDl")).toInteger(0));
        run.insert(QStringLiteral("up"),
                   e.value(QStringLiteral("burstUp")).toInteger(kDefaultBurstUp));
        write(run.value(QStringLiteral("dl")).toInteger(),
              run.value(QStringLiteral("up")).toInteger(),
              true,
              {});
    }
    if (touched) {
        if (!run.isEmpty())
            e.insert(kRunning, run);
        setEntry(e);
    }
    if (!rest.isEmpty() && m_preferences)
        m_preferences->apply(rest);
}

bool AltPresets::scheduleOn() const
{
    if (!m_preferences || !m_preferences->isLoaded())
        return false;
    const AltSchedule schedule = AltSchedule::fromPreferences(m_preferences->values());
    return schedule.known() && schedule.enabled() && schedule.onAt(now());
}

void AltPresets::write(qint64 dl, qint64 up, bool on, std::function<void(bool)> done)
{
    if (!m_client) {
        if (done)
            done(false);
        return;
    }
    // Both go out at once: whichever lands first, the server ends on the same set and mode.
    struct Pair
    {
        int left = 2;
        bool ok = true;
    };
    auto pair = std::make_shared<Pair>();
    m_writes += 2;
    const auto landed = [this, pair, done](bool ok) {
        --m_writes;
        pair->ok = pair->ok && ok;
        if (--pair->left > 0)
            return;
        if (m_preferences)
            m_preferences->reload();
        if (done)
            done(pair->ok);
    };
    QJsonObject limits{{QStringLiteral("alt_dl_limit"), dl}, {QStringLiteral("alt_up_limit"), up}};
    m_client->appSetPreferences(limits)->then(this, [landed](const QbtResult<QByteArray> &result) {
        if (result.isError())
            qWarning().noquote() << QStringLiteral("alternative limits not written: %1")
                                        .arg(result.error().toString());
        landed(result.isValid());
    });
    m_client->transferSetSpeedLimitsMode(on)->then(
        this, [this, on, landed](const QbtResult<QByteArray> &result) {
            // A server without setSpeedLimitsMode: flip it when it is not where it should be.
            if (result.isError() && m_client && m_state && m_state->useAltSpeedLimits() != on)
                m_client->transferToggleSpeedLimitsMode();
            landed(true);
        });
}

bool AltPresets::pick(const QString &kind)
{
    if (m_server.isEmpty() || !m_client)
        return false;
    if (kind == QLatin1String("burst")) {
        if (!isLoaded())
            return false;
        QJsonObject e = entry();
        QJsonObject run = e.value(kRunning).toObject();
        if (run.isEmpty()) {
            // The turtle is saved before anything is sent: a crash from here on still finds it.
            run.insert(QStringLiteral("turtleDl"), qMax<qint64>(0, serverLimit("alt_dl_limit")));
            run.insert(QStringLiteral("turtleUp"), qMax<qint64>(0, serverLimit("alt_up_limit")));
        }
        run.insert(QStringLiteral("dl"), burstDl());
        run.insert(QStringLiteral("up"), burstUp());
        const int minutes = burstMinutes();
        run.insert(QStringLiteral("until"),
                   minutes > 0 ? now().addSecs(qint64(minutes) * 60).toString(Qt::ISODate)
                               : QString());
        e.insert(kRunning, run);
        e.insert(QStringLiteral("lastPick"), QStringLiteral("burst"));
        m_recovered = true; // what runs is this run's own
        setEntry(e);
        armTimer();
        write(burstDl(), burstUp(), true, {});
        return true;
    }
    const bool on = kind == QLatin1String("turtle");
    if (!on && kind != QLatin1String("off"))
        return false;
    if (on) {
        QJsonObject e = entry();
        e.insert(QStringLiteral("lastPick"), QStringLiteral("turtle"));
        setEntry(e);
    }
    if (burstOn()) {
        finishBurst(on);
        return true;
    }
    m_client->transferSetSpeedLimitsMode(on)->then(
        this, [this, on](const QbtResult<QByteArray> &result) {
            if (result.isError() && m_client && m_state && m_state->useAltSpeedLimits() != on)
                m_client->transferToggleSpeedLimitsMode();
        });
    return true;
}

bool AltPresets::toggle()
{
    const bool on = m_state && m_state->useAltSpeedLimits();
    return pick(on ? QStringLiteral("off") : lastPick());
}

void AltPresets::endBurst()
{
    if (burstOn())
        finishBurst(scheduleOn());
}

void AltPresets::finishBurst(bool mode)
{
    const QJsonObject run = running();
    if (run.isEmpty())
        return;
    m_timer.stop();
    const QString server = m_server;
    write(run.value(QStringLiteral("turtleDl")).toInteger(),
          run.value(QStringLiteral("turtleUp")).toInteger(),
          mode,
          [this, server, run](bool ok) {
              // Kept on a failure: the next start (or the next visit) writes it back again.
              if (!ok)
                  return;
              QJsonObject e = m_store.value(server).toObject();
              if (e.value(kRunning).toObject() != run)
                  return; // a new burst meanwhile
              e.remove(kRunning);
              m_store.insert(server, e);
              Q_EMIT storeChanged();
              Q_EMIT changed();
          });
}

void AltPresets::endBurstAndWait(int ms)
{
    if (!burstOn() || !m_client)
        return;
    endBurst();
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer check;
    connect(&check, &QTimer::timeout, &loop, [this, &loop]() {
        if (m_writes == 0)
            loop.quit();
    });
    deadline.start(ms);
    check.start(20);
    loop.exec();
}

void AltPresets::recover()
{
    if (m_recovered || !isLoaded() || !m_state || !m_state->connected())
        return;
    m_recovered = true;
    const QJsonObject run = running();
    if (run.isEmpty())
        return;
    // Someone else wrote the set meanwhile: it is theirs now, and the saved turtle is stale.
    if (serverLimit("alt_dl_limit") != run.value(QStringLiteral("dl")).toInteger() ||
        serverLimit("alt_up_limit") != run.value(QStringLiteral("up")).toInteger()) {
        QJsonObject e = entry();
        e.remove(kRunning);
        setEntry(e);
        return;
    }
    const QDateTime until = burstUntil();
    if (until.isValid() && until > now())
        armTimer();
    else
        endBurst();
}

void AltPresets::armTimer()
{
    const QDateTime until = burstUntil();
    if (!until.isValid()) {
        m_timer.stop();
        return;
    }
    const qint64 ms = qBound<qint64>(0, now().msecsTo(until), 24LL * 3600 * 1000);
    m_timer.start(int(ms));
}

} // namespace omaseed::core
