#pragma once

// DelugeTorrentsImport — a TorrentsBackup file laid onto a Deluge (docs/57 §2, §5;
// docs/DECISIONS.md C40 X4–X7 and C9; #88), in TorrentsImport's shape: the same plan, choice,
// start, cancel, progress, Outcome and journal (TorrentsImportPlan), so whoever drives an
// import needs to know the engine only to make one.
//
// The plan is TorrentsImportPlan's: new, already there by any of its hashes, a link, or left
// unfinished by an earlier run. Its `categories` are the labels the sent torrents use and the
// daemon lacks (`Here::categories`: its labels), in Deluge's spelling; it has no tags.
//
// `start` then runs, one step at a time:
//   1. whether the Label plugin is on, and its labels;
//   2. the torrents, in the exported queue order, one core.add_torrent_file per torrent and
//      one at a time (C40 X6: the batch call breaks on a duplicate, and the daemon is not
//      hurried). Each is added paused with its .torrent (its
//      piece layers put right first), its folder through the path rows, its file priorities
//      (in the add, and never after: see below), its speed limits, its ratio limit and what
//      it does there, sequential download and first and last pieces first. Never `seed_mode`.
//      "Torrent already in session" is a torrent already there. Then, per torrent: its label,
//      its names, and its check;
//   3. the queue: those unfinished at export and still queued go to the end one by one, in
//      their exported order;
//   4. the check (C40 X6). Deluge never checks a paused add: core.force_recheck is asked for
//      each one, after its names are right. A torrent is settled once it is no longer checking
//      and was seen checking, or is finished, or after a few polls with none of the import's
//      torrents checking. A recheck leaves it paused; it is resumed when it is complete and
//      was started at export. "leave them stopped" asks for no check and resumes nothing.
//
// The label (C40 X4). Deluge keeps one, LibraryCrossing's (the category, or the first label,
// in its spelling). With the Label plugin on, a label the daemon lacks is made first
// (label.add, once per label per run), then label.set_torrent. A label made here applies
// nothing of its own (no folder for finished torrents, no limits); one the daemon already
// had applies whatever its owner gave it. With the plugin off no label is set, and
// `Outcome::labelsNotSet` counts the torrents that had one.
//
// Names (C40 X6). Deluge adds a torrent under the .torrent's own names. core.rename_folder
// gives its top folder another name and core.rename_files gives a file a whole new path, so
// every layout a record can hold is reached: a torrent without its top folder, a single file
// in a folder of its own, files moved between folders. Deluge renames in the background: each
// rename is waited for (the daemon's `files` read until they say the new names) before the
// next and before the check. One that never shows is left as it is and the torrent stays
// stopped, named in `Outcome::misplaced`.
//
// Renames never replace a file on the target's disk (C9; #64, #67): where this machine sees
// the daemon's folders (`localPathFor`), a torrent with a rename whose old and new names are
// both there gets none of its renames and is never started; `Outcome::refused` names it.
// Elsewhere the renames are sent.
//
// File priorities and the trap (C40 X6; scripts/dev/research/88-engines-backup). On a
// finished torrent, a skipped file wanted again leaves a torrent Deluge can't remove until it
// restarts. So priorities go in the add, and nothing changes them after a check.
//
// Links (C40 X7). A record with a magnet is added by it (core.add_torrent_magnet), paused,
// with its folder, limits and label, and counted apart. Its file priorities and renames can't
// be set before its details are there, so the journal keeps them (`Journal::links`), beyond
// the run and the file: `finishLinks`, called at a later start or poll, does them for every
// link whose details arrived (its priorities first, before its check, and not at all when it
// is finished and they would want a skipped file again), asks for its check and resumes it
// if it is complete and was started.
//
// What a Deluge can't keep (a seeding-time or idle limit, a limit's action other than stop
// or remove, all but one label, automatic management, an incomplete folder of its own) is not
// sent; `Outcome::losses` counts it as the sheet did beforehand (C40 X5).
//
// A journal (TorrentsImportPlan::Journal, one file per server) records each torrent between
// its add and its last step, so a run that fails (the daemon gone) or is stopped leaves what
// was added, and the next run of the same file finishes those and adds the rest. A .torrent
// the daemon refuses is not such a failure: it is named in `errors` and the run goes on.

#include "core/LibraryCrossing.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/TorrentsImportPlan.hpp"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>

namespace omaseed::core {

class ApiError;
class DelugeEngine;
class DelugeRpc;

class DelugeTorrentsImport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = TorrentsImportPlan::kBatch;

    using PathRow = TorrentsImportPlan::PathRow;
    using Journal = TorrentsImportPlan::Journal;
    using Here = TorrentsImportPlan::Here;
    using Plan = TorrentsImportPlan::Plan;
    using Options = TorrentsImportPlan::Options;
    using Outcome = TorrentsImportPlan::Outcome;

    // ---- the pure half ---------------------------------------------------------------------

    /// The plan for a Deluge: TorrentsImportPlan's, its `categories` the labels to make
    /// (`here.categories` are the daemon's labels).
    static Plan plan(const TorrentsBackup::Contents &contents,
                     const Here &here,
                     const Journal &journal = Journal());
    /// `record`'s one label as a Deluge takes it (C40 X4): its own for a Deluge's file, a
    /// qBittorrent's category (never a tag), a Transmission's first label, in Deluge's
    /// spelling. Empty for none.
    static QString labelHere(const TorrentsManifest &manifest, const TorrentRecord &record);
    /// What a Deluge can't keep of the torrents `plan` sends or finishes (C40 X5).
    static QList<LibraryCrossing::Loss> losses(const TorrentsBackup::Contents &contents,
                                               const Plan &plan);

    /// The options core.add_torrent_file and core.add_torrent_magnet take for `record`:
    /// paused, into `savePath`, its speed limits in KiB/s (-1 none), its ratio limit
    /// (`stop_at_ratio` with `stop_ratio`, and `remove_at_ratio` when its action is "Remove";
    /// none: `stop_at_ratio` false; the server's: not sent), sequential download, first and
    /// last pieces first, and, `withFiles`, its file priorities. Never `seed_mode`.
    static QJsonObject
    addOptions(const TorrentRecord &record, const QString &savePath, bool withFiles = true);
    /// A record's priorities (qBittorrent's: 0 not wanted, 1 normal, 2 low, 6 and 7 high) in
    /// Deluge's numbers (0, 4, 1, 7), one per file.
    static QJsonArray filePriorities(const QList<int> &priorities);

    /// The renames that take a torrent's files from `current` to `wanted`.
    struct Renames
    {
        QString folderFrom; ///< core.rename_folder's old folder, "Top/"; empty for none
        QString folderTo;
        QStringList afterFolder;          ///< the names once the folder is renamed
        QList<QPair<int, QString>> files; ///< core.rename_files: index and whole new path
        bool isEmpty() const { return folderFrom.isEmpty() && files.isEmpty(); }
        bool operator==(const Renames &other) const = default;
    };
    /// From `current` to `wanted` (the same files, in index order): the top folder by
    /// core.rename_folder when every file is in one and stays in one, then each file still
    /// elsewhere by its whole path. Nothing when `wanted` can't be: another count of files,
    /// an empty name or part, one name twice.
    static std::optional<Renames> renamesFor(const QStringList &current, const QStringList &wanted);

    /// How many links `journalPath`'s journal keeps for finishLinks().
    static int linksWaiting(const QString &journalPath);

    // ---- the run ---------------------------------------------------------------------------

    /// Talks to `engine`'s wire; null for none.
    explicit DelugeTorrentsImport(DelugeEngine *engine, QObject *parent = nullptr);
    ~DelugeTorrentsImport() override;

    void start(const TorrentsBackup::Contents &contents, const Plan &plan, const Options &options);
    /// C40 X7: finishes the links an earlier import added (the journal at
    /// `options.journalPath`) whose details have arrived since: their file priorities and
    /// renames, a check, and a resume for one that is complete and was started. Those still
    /// without details stay in the journal (`Outcome::linksWaiting`); those no longer on the
    /// daemon leave it. `finished` is emitted as for a run, `Outcome::finished` counting the
    /// links done. Uses `options`' journalPath, localPathFor, check and pollMs.
    void finishLinks(const Options &options);
    /// Stops after the request on its way. What was added stays; the journal keeps the rest.
    void cancel();
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// `phase` "adding" or "checking"; `done` of `total`.
    void progress(const QString &phase, int done, int total);
    void finished(const omaseed::core::TorrentsImportPlan::Outcome &outcome);

private:
    /// One torrent between its add and its last step.
    struct Item
    {
        TorrentRecord record;
        QString hash;             ///< the daemon's id
        QString savePath;         ///< through the rows
        QString label;            ///< to set; empty for none
        QStringList current;      ///< a link's files as the daemon names them (finishLinks)
        QJsonArray priorities;    ///< a link's, to set before its check (finishLinks)
        bool link = false;        ///< added by its link: its journal entry is in `links`
        bool namesAsked = false;  ///< the record renames something, or has another layout
        bool keepStopped = false; ///< a rename refused or not applied: never started
    };
    /// What the check needs of a torrent once its steps are done.
    struct Tracked
    {
        QString id; ///< the record's, as the journal knows it
        bool started = false;
        bool keepStopped = false;
        bool link = false;
    };

    using Step = std::function<void(std::function<void()>)>;

    void next();
    void readLabels();
    void addBatch();
    void sendAdd();
    void followUps(int at);
    void setLabel(const std::shared_ptr<Item> &item, std::function<void()> then);
    void applyNames(const std::shared_ptr<Item> &item, std::function<void()> then);
    /// Reads the torrent's files until they are `expected` (then(true)), or give up.
    void awaitNames(const std::shared_ptr<Item> &item,
                    const QStringList &expected,
                    int polls,
                    std::function<void(bool)> then);
    void misplace(Item &item);
    void settleFollowUp(const Item &item);
    void track(const Item &item);
    void readLinks();
    void moveQueue();
    void startCheck();
    void poll();
    void done();
    void fail(const QString &error);
    /// A failed call: the daemon gone ends the run (true); anything else is `what`'s error.
    bool failed(const ApiError &error, const QString &what);
    void runSteps(QList<Step> steps, std::function<void()> then);
    bool live(int generation) const { return m_busy && generation == m_generation; }
    void saveJournal();
    void begin(const Options &options);
    Item itemFor(int index) const;
    void emitFinished();
    static QJsonObject linkEntry(const Item &item, const QString &stage);

    QPointer<DelugeRpc> m_rpc;
    TorrentsBackup::Contents m_contents;
    Plan m_plan;
    Options m_options;
    Outcome m_outcome;
    Journal m_journal;
    std::unique_ptr<QFile> m_file;
    QList<int> m_order;  ///< plan.add + plan.magnets, queue order first
    QList<int> m_batch;  ///< record indexes of the batch on its way, not yet sent
    QList<Item> m_items; ///< the batch's torrents that were added: their steps are next
    QList<QPair<int, QString>> m_queued; ///< exported place and id: to the queue's end, in order
    QHash<QString, Tracked> m_byHash;
    QStringList m_checking; ///< ids the check waits for
    QSet<QString> m_seenChecking;
    QSet<QString> m_labels; ///< the daemon's labels, and those made here
    QTimer m_pollTimer;
    int m_step = 0;
    int m_next = 0;
    int m_quietPolls = 0; ///< polls in a row with none of `m_checking` checking
    int m_checkTotal = 0;
    int m_generation = 0;
    bool m_busy = false;
    bool m_adding = false;
    bool m_labelPlugin = false;
    bool m_linksOnly = false; ///< a finishLinks() run
};

} // namespace omaseed::core
