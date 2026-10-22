#pragma once

// The run behind "Free the space…" (docs/44 §2 F2, §7; DECISIONS C17 D3, #118).
//
// FreeSkippedJob: stop the torrent so nothing writes while files go, read its skipped files
// again (the files' priorities, its pieces and what is open now, so a file wanted again since
// the sheet opened, or opened by a player, stays), delete those SkippedOnDisk::plan() says go,
// start it again if it was running, and recheck only when a file holding downloaded pieces
// went on a v1-only or v2-only torrent. Never a recheck on a hybrid (#115).
//
// It talks to the server through EngineClient. Adding a hybrid again, which frees what this
// can't, is qBittorrent's: engines/qbt/AddAgainJob.
//
// Each step is logged ("free space: …"), the way omaseed logs what it does to a server.

#include "core/SkippedOnDisk.hpp"
#include "core/engines/EngineClient.hpp"

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

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
        QStringList openBy; ///< the programs that held a file open, so it stayed
        bool started = false;
        bool rechecked = false;
    };

    explicit FreeSkippedJob(EngineClient *client, QObject *parent = nullptr);

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

    QPointer<EngineClient> m_client;
    Input m_input;
    Outcome m_outcome;
    FreePlan m_plan;
    bool m_cycled = false; ///< started and stopped once so qBittorrent let go of the files
    bool m_busy = false;
};

} // namespace omaseed::core
