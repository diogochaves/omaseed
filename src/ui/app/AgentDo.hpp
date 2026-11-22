#pragma once

// AgentDo — the app's side of `omaseed do` (DECISIONS C45 V14, V15; C50 GT3, GT5, GT8): the
// options read, every check made before anything runs, then the act through App as the window
// runs it, answered once the server has.
//
// The order (each a whole-call failure, nothing run):
// 1. the options (usage); `list` answers the gate's lines as they stand, whatever it says;
// 2. a demo acts on nothing (not-allowed; PLAN Q15);
// 3. the gate (AgentGate::decide): never, not-allowed (reads off, or the kind's line off);
// 4. an action not in this build yet (usage);
// 5. the server, as the reads check it: --server, not-current, first-sync, unreachable;
// 6. every torrent matched (unknown-torrent, ambiguous) and checked for its action
//    (start-here, dismiss, resume: refused, with why);
// 7. --dry-run answers what would happen; ask opens the `omaseed · agent` window (AgentAsk,
//    V17) and, once allowed there, makes every check again before it runs (a setting turned
//    off meanwhile wins); allow runs it.
//
// Moves (#293, C45 V14, V15): `move --to FOLDER` and `move-back MOVE` plan as the window's move
// sheet does (MoveController, C15/C16: room, names there) and wait for the plan's every check;
// one that blocks is refused with the sheet's words, else they ask (never allow, C50 GT3) with
// where to, how much and the room. Allowed, the job starts (or queues, L12) and the word
// answers at its start with the job's id; the job, its record and the story say "by an agent".
//
// Locate (#294, C50 GT2), a fix: `locate --to FOLDER` re-points torrents whose files are
// missing at a folder this machine sees, once each wanted file is found there by its relative
// path and size (LocateCheck), then rechecks them; a short find is not-found, nothing run.
//
// The fixes here: retry, start-here (a start, for a torrent the engine stopped after a failed
// move; on an engine that pauses on error, recheck first), recheck, reannounce, dismiss, and
// pause and resume (C36 M10's hold, the whole server).
//
// Remove (#295, C50 GT1, GT6): `remove <hash…>`, the torrent only, never its files (the CLI
// takes no --delete-files), and only one whose files are gone: the engine says so, and where
// omaseed sees the disk none of its files is at its save path (LocateCheck::present); else
// not-removable. It always asks. Allowed, each torrent is kept first (App::keepRemoved, 30
// days to add back); a keep that fails refuses the remove.
//
// A plan (#296, C50 GT4, GT5): `{plan: TEXT}`, the file's text as the CLI read it; see
// AgentDoPlan.cpp. Its lines run through proceed with two keys only the app itself sets
// (`probe`: answer the ask's map instead of opening it; `inPlan`: a line Settings allowed that
// would ask now is refused, not asked); the bus refuses both.

#include "core/ProcParent.hpp"
#include "core/engines/DetailsData.hpp"

#include <QHash>
#include <QList>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace omaseed::app {

class App;
class AgentService;

class AgentDo : public QObject
{
    Q_OBJECT

public:
    using Reply = std::function<void(const QString &json)>;

    AgentDo(App *app, AgentService *service);

    /// `options` as the Agent interface's Do got them; `reply` is called exactly once.
    void run(const QString &options, uint callerPid, const Reply &reply);

    /// The folder standing for /proc, where the caller's parent is read (V17); a test's own.
    void setProcRoot(const QString &root) { m_procRoot = root; }

private:
    /// The checks and the act; `asked` once the person allowed it in the window.
    void proceed(const QString &options,
                 const core::ProcParent::Caller &caller,
                 const Reply &reply,
                 bool asked);
    /// The person's answer in the `omaseed · agent` window (V17), then the act once allowed
    /// (proceed again, `asked`); refused, unanswered or another ask open, the answer says so.
    void askFirst(const QString &json,
                  const core::ProcParent::Caller &caller,
                  const Reply &reply,
                  const QString &action,
                  const QString &subject,
                  const QVariantMap &request);
    /// A move or a move back (#293): planned, checked, asked, started.
    void proceedMove(const QString &json,
                     const core::ProcParent::Caller &caller,
                     const Reply &reply,
                     bool asked,
                     bool dryRun,
                     const QString &action,
                     const QStringList &hashes,
                     const QString &to,
                     const QString &moveId);
    /// A locate (#294, C50 GT2): the checks that need no disk, then every torrent's file list
    /// read from the server, then locateFound.
    void proceedLocate(const QString &json,
                       const core::ProcParent::Caller &caller,
                       const Reply &reply,
                       bool asked,
                       bool dryRun,
                       bool asks,
                       const QStringList &hashes,
                       const QString &to);
    /// The find in `localTo` (LocateCheck) on each torrent's `files`, then the dry run's
    /// answer, the ask, or the re-point and the recheck.
    void locateFound(const QString &json,
                     const core::ProcParent::Caller &caller,
                     const Reply &reply,
                     bool asked,
                     bool dryRun,
                     bool asks,
                     const QStringList &hashes,
                     const QString &to,
                     const QString &localTo,
                     const QHash<QString, QList<core::ContentFile>> &files);
    /// A remove (#295, C50 GT1): the engine's word that the files are gone, then every
    /// torrent's file list read from the server, then removeChecked.
    void proceedRemove(const QString &json,
                       const core::ProcParent::Caller &caller,
                       const Reply &reply,
                       bool asked,
                       bool dryRun,
                       const QStringList &hashes);
    /// Where omaseed sees the disk, none of the files at the save path (LocateCheck::present);
    /// then the dry run's answer, the ask (always, GT1), or the keep (GT6) and the remove.
    void removeChecked(const QString &json,
                       const core::ProcParent::Caller &caller,
                       const Reply &reply,
                       bool asked,
                       bool dryRun,
                       const QStringList &hashes,
                       const QHash<QString, QList<core::ContentFile>> &files);
    /// Keeps `hashes` one after another (App::keepRemoved), then removes them; the first keep
    /// that fails refuses the whole remove, and what was kept for it goes.
    void keepThenRemove(const core::ProcParent::Caller &caller,
                        const Reply &reply,
                        bool asked,
                        const QStringList &hashes);
    struct KeepRun;
    void keepNext(const std::shared_ptr<KeepRun> &run);
    /// MoveController's plan (`make`), once it has every check in (`pending` false), or after
    /// a while with what it has: `done` gets it.
    void whenPlanned(const std::function<QVariantMap()> &make,
                     const std::function<void(const QVariantMap &)> &done);
    /// The ask's map for a move: its torrents, where, how much and the room (C45 V17).
    QVariantMap moveRequest(const QString &action,
                            const QVariantMap &words,
                            const QString &to,
                            const QString &moveId,
                            const core::ProcParent::Caller &caller) const;
    /// The caller's part of an ask's map (word, parent, when).
    static void addCaller(QVariantMap *request, const core::ProcParent::Caller &caller);

    /// The `omaseed · agent` window's map for one act (AgentAsk's `request`).
    QVariantMap askRequest(const QString &action,
                           const QStringList &hashes,
                           const QString &serverName,
                           const core::ProcParent::Caller &caller) const;

    /// A plan (#296, C50 GT4, GT5; AgentDoPlan.cpp): every line checked as a dry run, one ask
    /// for the lines that ask, then each line run in turn as a `do` of its own.
    void runPlan(const QString &text,
                 const QString &server,
                 bool dryRun,
                 const core::ProcParent::Caller &caller,
                 const Reply &reply);
    struct PlanRun;
    void planCheckNext(const std::shared_ptr<PlanRun> &run);
    void planChecked(const std::shared_ptr<PlanRun> &run);
    /// A refused line's every torrent and why (#303), before the plan's answer.
    void planRefusals(const std::shared_ptr<PlanRun> &run);
    void planRefusalNext(const std::shared_ptr<PlanRun> &run);
    /// The dry run's answer, or the failure of a plan with a line off or refused.
    QString planChecks(const std::shared_ptr<PlanRun> &run) const;
    void planProbeNext(const std::shared_ptr<PlanRun> &run);
    /// The `omaseed · agent` window's map for a plan (board 67 §4): its groups.
    QVariantMap planRequest(const std::shared_ptr<PlanRun> &run) const;
    void planAsk(const std::shared_ptr<PlanRun> &run);
    void planRunNext(const std::shared_ptr<PlanRun> &run);
    void planDone(const std::shared_ptr<PlanRun> &run);

    QString m_procRoot = QStringLiteral("/proc");
    App *m_app;
    AgentService *m_service;
};

} // namespace omaseed::app
