#pragma once

// The two runs behind "Free the space…" (docs/44 §2 F2, §7; DECISIONS C17 D3, #118).
//
// FreeSkippedJob: stop the torrent so nothing writes while files go, read its skipped files
// again (the files' priorities, its pieces and what is open now, so a file wanted again since
// the sheet opened, or opened by a player, stays), delete those SkippedOnDisk::plan() says go,
// start it again if it was running, and recheck only when a file holding downloaded pieces
// went on a v1-only or v2-only torrent. Never a recheck on a hybrid (#115).
//
// AddAgainJob: what frees a hybrid's leftover files, and clears libtorrent's
// torrent_inconsistent_hashes on a hybrid already in it (board 28): remove the torrent keeping
// its files, delete the leftovers, add the same .torrent (torrents/export) stopped with its
// save path, layout, category, tags, limits, file names and priorities, then start it if it was
// started. Its ratio, seeding time and added date are lost; the sheet says so before it runs.
//
// Each step is logged ("free space: …"), the way omaseed logs what it does to a server.

#include "core/SkippedOnDisk.hpp"
#include "core/TorrentsBackup.hpp"

#include "core/QbtClient.hpp"

#include <QFile>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace omaseed::core {

class FreeSkippedJob : public QObject
{
    Q_OBJECT

public:
    struct Input
    {
        QString hash;
        QString name;
        bool running = false; ///< started before: stopped first, started after
        bool hybrid = false;
        qint64 torrentSize = 0;
        SkippedOnDisk::LocalPathOf localPathOf;
        QString contentLocal;
        QStringList sharedRoots;
        QString procRoot = QStringLiteral("/proc");
        SkippedOnDisk::AllocatedReader reader = SkippedOnDisk::readAllocated;
        std::function<bool(const QString &)> remove = [](const QString &path) {
            return QFile::remove(path);
        };
        int pollMs = 500;
        int stopTimeoutMs = 20000;
    };
    struct Outcome
    {
        bool ok = false;
        QString error;
        int deleted = 0;
        qint64 freed = 0;
        int kept = 0;       ///< files that held space and stayed
        QStringList failed; ///< deletes the disk refused
        bool started = false;
        bool rechecked = false;
    };

    explicit FreeSkippedJob(QbtClient *client, QObject *parent = nullptr);

    void start(const Input &input);
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// "stopping", "deleting", "starting", "rechecking".
    void step(const QString &what);
    void finished(const omaseed::core::FreeSkippedJob::Outcome &outcome);

private:
    void waitStopped(int waited);
    void readAndDelete();
    void startAgain();
    void recheck();
    void finish(const QString &error = QString());

    QPointer<QbtClient> m_client;
    Input m_input;
    Outcome m_outcome;
    FreePlan m_plan;
    bool m_busy = false;
};

class AddAgainJob : public QObject
{
    Q_OBJECT

public:
    struct Input
    {
        QString hash;
        /// Local paths deleted once the torrent is gone and before it comes back: a hybrid's
        /// skipped files that hold downloaded pieces. Empty for an errored hybrid (board 28).
        QStringList deletePaths;
        std::function<bool(const QString &)> remove = [](const QString &path) {
            return QFile::remove(path);
        };
        int pollMs = 500;
        int waitMs = 30000;
    };
    struct Outcome
    {
        bool ok = false;
        QString error;
        QString hash; ///< the torrent as it came back
        int deleted = 0;
        QStringList warnings; ///< steps after the add that the server refused
    };

    explicit AddAgainJob(QbtClient *client, QObject *parent = nullptr);

    /// The add's options for `record`: stopped, at its save path and incomplete folder, in its
    /// layout, category and tags, with its speed limits and download options.
    static QbtClient::AddOptions addOptions(const TorrentRecord &record);
    /// File ids by priority, for the non-normal priorities of `record` (one filePrio each).
    static QMap<int, QList<int>> priorityGroups(const TorrentRecord &record);

    void start(const Input &input);
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// "reading", "removing", "deleting", "adding", "restoring", "starting".
    void step(const QString &what);
    void finished(const omaseed::core::AddAgainJob::Outcome &outcome);

private:
    void readRecord();
    void waitGone(int waited);
    void add();
    void waitAdded(int waited);
    void restore();
    void finish(const QString &error = QString());

    QPointer<QbtClient> m_client;
    Input m_input;
    Outcome m_outcome;
    TorrentRecord m_record;
    QByteArray m_torrent;
    QString m_apiVersion;
    std::shared_ptr<std::function<void()>> m_chain;
    bool m_busy = false;
};

} // namespace omaseed::core
