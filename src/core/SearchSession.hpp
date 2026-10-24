#pragma once

// SearchSession — one search, asked of each plugin on its own (#145, board 48, DECISIONS C26
// SR1, SR2, SR12, SR13).
//
// qBittorrent's search/status is per job, never per plugin, and a job ends only when its
// slowest plugin does (or at qBittorrent's 3 min). So omaseed starts one job per plugin that
// is on, at most four at once (one of qBittorrent's five left for other clients), the rest
// queued (SR1). While jobs run it asks each for its new results once a poll (search/results
// from the offset it has read), which also says whether the job has stopped.
//
// A plugin has answered when its job stops. A job that stopped with nothing may have failed:
// qBittorrent says so only in its log ("Error occurred in search engine… Engines: "NAME"…"),
// so omaseed reads log/main's warnings since the search began and, when one names the plugin,
// calls it failed with the traceback's last line as its reason; otherwise it found "0
// results", which is not a failure (SR2). Past the deadline (30 s from its job's start) a
// plugin still running is "slow": the search counts as done without it, and what it sends
// before qBittorrent ends it still joins the rows. "stop asking" stops its job.
//
// SR12: the jobs are this session's; it deletes them when a new search replaces them and on
// shutdown(), and never touches any job it didn't start. SR13: a 409 "Python must be
// installed" at search/start is the server without Python.
//
// Plugin rows are QVariantMaps for QML: {name, fullName, state, count, seconds, reason,
// logTime}. `state` is "queued", "asking", "answered", "failed", "slow", "stopped" or
// "skipped" (a type the plugin doesn't have, SR6).

#include "core/SearchResults.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>

namespace omaseed::core {

class QbtClient;

class SearchSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pattern READ pattern NOTIFY changed)
    Q_PROPERTY(QString category READ category NOTIFY changed)
    /// "idle" (nothing asked yet), "asking", "done", "nopython", "error".
    Q_PROPERTY(QString state READ stateName NOTIFY changed)
    Q_PROPERTY(QVariantList plugins READ pluginRows NOTIFY changed)
    /// Plugins asked (skipped ones aside), those that answered, and those that didn't (failed,
    /// slow, stopped).
    Q_PROPERTY(int asked READ asked NOTIFY changed)
    Q_PROPERTY(int answered READ answered NOTIFY changed)
    Q_PROPERTY(int didntAnswer READ didntAnswer NOTIFY changed)
    /// Results as qBittorrent gave them, before the fold.
    Q_PROPERTY(int resultCount READ resultCount NOTIFY hitsChanged)
    /// What went wrong at the start ("error"), as qBittorrent or the network said it.
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    enum class State { Idle, Asking, Done, NoPython, Error };

    /// One plugin to ask: its name (what search/start takes), its own name, and the types it
    /// supports (qBittorrent's category ids).
    struct Plugin
    {
        QString name;
        QString fullName;
        QStringList categories;
    };

    explicit SearchSession(QObject *parent = nullptr);

    void setClient(QbtClient *client);
    /// Test seams: the poll, the deadline (30 s, SR2) and how many jobs run at once (4, SR1).
    void setTimings(int pollMs, int deadlineMs, int maxRunning);

    QString pattern() const { return m_pattern; }
    QString category() const { return m_category; }
    State state() const { return m_state; }
    QString stateName() const;
    QVariantList pluginRows() const;
    int asked() const;
    int answered() const;
    int didntAnswer() const;
    int resultCount() const { return int(m_hits.size()); }
    QString error() const { return m_error; }
    const QList<SearchHit> &hits() const { return m_hits; }
    /// The jobs this session started that may still be on the server.
    QList<int> jobIds() const;

    /// Asks `plugins` for `pattern` in `category` ("all" or a type's id). Replaces the last
    /// search: its jobs are deleted.
    void start(const QString &pattern, const QString &category, const QList<Plugin> &plugins);
    /// "search it again" (SR2): `plugin` alone, its rows replaced.
    Q_INVOKABLE void askAgain(const QString &plugin);
    /// "stop asking": a slow plugin's job is stopped; what it sent stays.
    Q_INVOKABLE void stopAsking(const QString &plugin);
    /// Deletes every job this session started (a quit, SR12); the rows stay.
    void shutdown();
    /// Lets go of everything without a word to the server (its client now points at another
    /// one); jobIds() first, to delete them when back.
    void forget();
    /// Deletes jobs a forgotten session left on this server (SR12).
    void deleteJobs(const QList<int> &ids);

    /// What a session holds, to show a server's last search again after a switch (SR12).
    struct Snapshot
    {
        QString pattern;
        QString category;
        State state = State::Idle;
        QList<SearchHit> hits;
        QVariantList plugins;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &snapshot);

    /// A plugin's failure in qBittorrent's log: the newest warning since `sinceSecs` that names
    /// `plugin` among its engines. `{reason, time}` (the traceback's last line, "HH:MM"), or
    /// empty.
    static QVariantMap
    failureFromLog(const QJsonArray &lines, const QString &plugin, qint64 sinceSecs);

Q_SIGNALS:
    void changed();
    void hitsChanged();

private:
    struct Job
    {
        Plugin plugin;
        int id = -1;
        QString state; // see the header
        int offset = 0;
        int count = 0;
        qint64 startedMs = -1;
        qint64 answeredMs = -1;
        QString reason;
        QString logTime;
        bool polling = false;
        bool deleted = false;
        /// Stopped with nothing: shown as asking until the log says failed or "0 results".
        bool pendingLog = false;
        /// Read against the log once more as the search ends (its warning may come late).
        bool recheckPending = false;
        bool logRechecked = false;
    };

    void pump();
    void poll();
    void startJob(int index);
    void finishJob(int index, int total);
    void checkFailures();
    void updateDone();
    Job *jobFor(const QString &plugin);
    int running() const;
    qint64 now() const { return m_clock.elapsed(); }

    QPointer<QbtClient> m_client;
    QTimer m_poll;
    QElapsedTimer m_clock;
    int m_pollMs = 1000;
    int m_deadlineMs = 30000;
    int m_maxRunning = 4;
    quint64 m_generation = 0;

    QString m_pattern;
    QString m_category = QStringLiteral("all");
    State m_state = State::Idle;
    QString m_error;
    QList<Job> m_jobs;
    QList<SearchHit> m_hits;
    qint64 m_startedSecs = 0;
    /// Jobs ended with nothing, waiting for the log to say whether they failed.
    bool m_logPending = false;
    bool m_logChecking = false;
    /// A restored snapshot's plugin rows (its jobs are gone).
    QVariantList m_restoredPlugins;
};

} // namespace omaseed::core
