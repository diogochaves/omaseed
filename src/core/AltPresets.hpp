#pragma once

// AltPresets — turtle and burst, two presets for qBittorrent's one set of alternative limits
// (DECISIONS C32 D8 a, amending C4 D1–D2 and C13's alternative-limits row).
//
// qBittorrent keeps one alternative set, and its scheduler turns on whatever set is stored.
// So turtle *is* that set at rest: the server's `alt_dl_limit`/`alt_up_limit`, which its
// schedule and every other client see. Burst is omaseed's own preset: picking it saves the
// turtle (in the store, before anything is sent, so a crash can't lose it), writes burst's
// limits over the set and turns the alternative limits on. When burst ends (picked off or
// turtle, its time up, omaseed quitting) the turtle is written back; the alternative limits
// then stay on for turtle, go off for off, and follow the schedule otherwise. A burst found
// running when omaseed starts (it crashed, or quit before the write landed) is ended at its
// time: at once if that has passed or it had none, else when it comes. A set someone else
// changed meanwhile (the limits are no longer burst's) is left as it is and forgotten.
//
// Per server: the presets, the burst's length and the last pick live in `store()` under the
// server's id; App keeps it in state.json. While a burst runs on a server omaseed has left,
// its record waits there and is ended the same way when omaseed is back on it (a switch
// cancels whatever the shared client still had in flight, so nothing is sent on the way out).
//
// Settings' turtle and burst rows read and write through it as they do a ServerPreferences
// (`values`, `has`, `pendingKeys`, `apply`): the turtle's keys go to the server at rest and to
// the saved copy while a burst runs; burst's keys stay here.

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>

namespace omaseed::core {

class GlobalState;
class QbtClient;
class ServerPreferences;

class AltPresets : public QObject
{
    Q_OBJECT
    /// "off", "turtle" or "burst": what the server is on now, as omaseed names it.
    Q_PROPERTY(QString current READ current NOTIFY changed)
    /// omaseed's burst is written over the set on this server.
    Q_PROPERTY(bool burstOn READ burstOn NOTIFY changed)
    /// When it ends; invalid for "until I stop it", or with no burst.
    Q_PROPERTY(QDateTime burstUntil READ burstUntil NOTIFY changed)
    /// "turtle" or "burst": what A turns on (it flips the last one picked).
    Q_PROPERTY(QString lastPick READ lastPick NOTIFY changed)
    /// How long a burst lasts once picked: 30, 60 (the default) or 0, until it is stopped.
    Q_PROPERTY(int burstMinutes READ burstMinutes WRITE setBurstMinutes NOTIFY changed)
    /// The presets' limits in bytes/s, 0 for none: the turtle (the server's set at rest, the
    /// saved copy while a burst runs) and burst (↓ none, ↑ 2 MB/s by default).
    Q_PROPERTY(qint64 turtleDl READ turtleDl NOTIFY changed)
    Q_PROPERTY(qint64 turtleUp READ turtleUp NOTIFY changed)
    Q_PROPERTY(qint64 burstDl READ burstDl NOTIFY changed)
    Q_PROPERTY(qint64 burstUp READ burstUp NOTIFY changed)
    /// Settings' rows (as ServerPreferences): `alt_dl_limit`, `alt_up_limit` (the turtle),
    /// `burst_dl_limit`, `burst_up_limit`, `burst_minutes`.
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY changed)
    Q_PROPERTY(QStringList pendingKeys READ pendingKeys NOTIFY changed)

public:
    static constexpr qint64 kDefaultBurstUp = 2 * 1024 * 1024;
    static constexpr int kDefaultBurstMinutes = 60;

    explicit AltPresets(QObject *parent = nullptr);

    void setClient(QbtClient *client);
    void setPreferences(ServerPreferences *preferences);
    void setGlobalState(GlobalState *state);
    /// The time it works with; the system clock until set (tests).
    void setClock(std::function<QDateTime()> clock);
    /// The server omaseed is on ("" for none, the demo, first run). A burst left running on
    /// it is looked at once its preferences have answered.
    void setServer(const QString &id);

    /// Every server's presets and running burst, as state.json keeps them.
    QJsonObject store() const { return m_store; }
    void setStore(const QJsonObject &store);

    QString current() const;
    bool burstOn() const;
    QDateTime burstUntil() const;
    QString lastPick() const;
    int burstMinutes() const;
    void setBurstMinutes(int minutes);
    qint64 turtleDl() const;
    qint64 turtleUp() const;
    qint64 burstDl() const;
    qint64 burstUp() const;
    QVariantMap values() const;
    bool isLoaded() const;
    QStringList pendingKeys() const;

    Q_INVOKABLE bool has(const QString &key) const;
    Q_INVOKABLE QVariant value(const QString &key) const { return values().value(key); }
    /// `changes` holds a key of the presets (Settings routes them here).
    Q_INVOKABLE bool owns(const QVariantMap &changes) const;
    /// The presets' keys as above; any other key goes to the server's preferences.
    Q_INVOKABLE void apply(const QVariantMap &changes);

    /// "off", "turtle" or "burst". False when it can't be done yet (no server, the
    /// preferences not read).
    Q_INVOKABLE bool pick(const QString &kind);
    /// A: off while the alternative limits are on, else the last pick on.
    Q_INVOKABLE bool toggle();
    /// Ends a running burst now: the turtle written back, the alternative limits as the
    /// schedule has them.
    Q_INVOKABLE void endBurst();
    /// At quit: endBurst(), and waits up to `ms` for the server to take it.
    void endBurstAndWait(int ms);

Q_SIGNALS:
    void changed();
    /// `store()` moved: App writes it to state.json at once.
    void storeChanged();

private:
    QDateTime now() const;
    QJsonObject entry() const;
    void setEntry(const QJsonObject &entry);
    QJsonObject running() const;
    /// The server's alternative set as it reads now (pending changes included); -1 when not
    /// read.
    qint64 serverLimit(const char *key) const;
    /// Writes the set and the mode, then reads the preferences again; `done(ok)` after both.
    void write(qint64 dl, qint64 up, bool on, std::function<void(bool)> done);
    void finishBurst(bool mode);
    /// Whether the schedule has the alternative limits on now.
    bool scheduleOn() const;
    /// A burst left running on this server, once its preferences answered.
    void recover();
    void armTimer();

    QPointer<QbtClient> m_client;
    QPointer<ServerPreferences> m_preferences;
    QPointer<GlobalState> m_state;
    std::function<QDateTime()> m_clock;
    QTimer m_timer;
    QJsonObject m_store;
    QString m_server;
    bool m_recovered = false;
    int m_writes = 0;
};

} // namespace omaseed::core
