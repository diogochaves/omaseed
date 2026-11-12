#pragma once

// TransmissionTorrentsImport — a TorrentsBackup file laid onto a Transmission (docs/57 §2, §5;
// docs/DECISIONS.md C40 X4–X7 and C9; #88), in TorrentsImport's shape: the same plan, choice,
// start, cancel, progress, Outcome and journal (TorrentsImportPlan), so whoever drives an
// import needs to know the engine only to make one.
//
// The plan is TorrentsImportPlan's: new, already there by any of its hashes, a link, or left
// unfinished by an earlier run. A Transmission has nothing to create first: a label exists
// once a torrent carries it, so the plan's `categories` and `tags` stay empty.
//
// `start` then runs, one step at a time:
//   1. the session, for the unit its speed limits are in;
//   2. the torrents, `batch` (20) at a time and in the exported queue order: one torrent_add
//      each (it takes one), up to `kInFlight` on their way. Each is added paused with its
//      .torrent (`metainfo`, its piece layers put right first), its folder through the path
//      rows, its labels (C40 X4: LibraryCrossing's, the category first for a qBittorrent's
//      file), its file choices and sequential download. A `torrent_duplicate` answer is a
//      torrent already there. Then, per torrent, what the add ignores without a word
//      (torrent_set: speed, ratio and idle limits), and its names;
//   3. the queue: the unfinished ones go to the end one by one, in their exported order;
//   4. the check (C40 X6). Transmission checks a paused add by itself; one whose files were
//      renamed is asked again (torrent_verify) once the renames are in. A torrent is settled
//      once it is no longer checking and was seen checking, or is complete, or after a few
//      polls with none of the import's torrents checking. It starts (torrent_start) when it
//      is complete and was started at export, and stays paused otherwise. "leave them
//      stopped" asks for no check, waits for none and starts nothing.
//
// Names (C40 X6). Transmission adds a torrent under the .torrent's own names, and
// torrent_rename_path gives one part of a path a new name: the top folder first, then
// folders and files below it. That reaches every name that keeps its depth. What it can't
// reach is left as it is and the torrent stays stopped, named in `Outcome::misplaced`: a
// multi-file torrent without its top folder (qBittorrent's "NoSubfolder": Transmission has no
// such layout), a single file put in a folder of its own ("Subfolder"), a file moved to
// another folder, two folders made one.
//
// Renames never replace a file on the target's disk (C9; #64, #67): where this machine sees
// the daemon's folders (`localPathFor`), a rename whose old and new names are both there is
// not sent (Transmission would answer success and point at the other file), the torrent's
// later renames are dropped with it, and it is never started; `Outcome::refused` names it.
// Elsewhere the renames are sent.
//
// Links (C40 X7). A record with a magnet is added by it (`filename`), paused, with its folder,
// labels and limits, and counted apart. Its file choices and renames can't be set before its
// details are there, so the journal keeps them (`Journal::links`), beyond the run and the
// file: `finishLinks`, called at a later start or poll, does them for every link whose
// details arrived, asks for its check and starts it if it is complete and was started.
//
// What a Transmission can't keep (a seeding-time limit, the limit's action, automatic
// management, an incomplete folder of its own) is not sent; `Outcome::losses` counts it as
// the sheet did beforehand (C40 X5).
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
#include <QJsonObject>
#include <QList>
#include <QObject>
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
class TorrentFile;
class TransmissionEngine;
class TransmissionRpc;

class TransmissionTorrentsImport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = TorrentsImportPlan::kBatch;
    /// torrent_add requests on their way at once.
    static constexpr int kInFlight = 4;

    using PathRow = TorrentsImportPlan::PathRow;
    using Journal = TorrentsImportPlan::Journal;
    using Here = TorrentsImportPlan::Here;
    using Plan = TorrentsImportPlan::Plan;
    using Options = TorrentsImportPlan::Options;
    using Outcome = TorrentsImportPlan::Outcome;

    // ---- the pure half ---------------------------------------------------------------------

    /// The plan for a Transmission: TorrentsImportPlan's, with nothing to create first.
    static Plan plan(const TorrentsBackup::Contents &contents,
                     const Here &here,
                     const Journal &journal = Journal());
    /// `record`'s labels as a Transmission takes them (C40 X4): its own for a Transmission's
    /// file; for a qBittorrent's the category, then the tags; a Deluge's one label.
    /// Transmission refuses a comma in a label, so one goes as a space; no label twice.
    static QStringList labelsHere(const TorrentsManifest &manifest, const TorrentRecord &record);
    /// What a Transmission can't keep of the torrents `plan` sends or finishes (C40 X5).
    static QList<LibraryCrossing::Loss> losses(const TorrentsBackup::Contents &contents,
                                               const Plan &plan);

    /// torrent_add's params for `record`: paused, into `savePath`, with `labels`; its
    /// .torrent (`metainfo`) with its file choices (a record's priorities are qBittorrent's:
    /// 0 not wanted, 6 and 7 high, 2 low, 1 normal), or its link when `metainfo` is empty.
    static QJsonObject addParams(const TorrentRecord &record,
                                 const QString &savePath,
                                 const QStringList &labels,
                                 const QByteArray &metainfo);
    /// The file choices of `priorities` as torrent_add and torrent_set take them:
    /// `files_unwanted`, `priority_high`, `priority_low`, only the lists with files in them.
    static QJsonObject fileChoices(const QList<int> &priorities);
    /// What torrent_set takes of `record` that torrent_add ignores: its speed limits in the
    /// daemon's unit (`speedBytes` bytes), its ratio and idle limits (-2 the session's: not
    /// sent; -1 none; else its own). Empty when the record has none of them.
    static QJsonObject limitParams(const TorrentRecord &record, qint64 speedBytes);

    /// One torrent_rename_path: `path` as the daemon names it now, `name` its last part's new
    /// name.
    struct RenameStep
    {
        QString path;
        QString name;
        bool operator==(const RenameStep &other) const = default;
    };
    /// The renames that take a torrent's files from `current` to `wanted` (the same files,
    /// in the same order), folders before what is in them. Nothing when
    /// torrent_rename_path can't do it: a file at another depth or in another folder, two
    /// folders made one, a name already another's.
    static std::optional<QList<RenameStep>> renameSteps(const QStringList &current,
                                                        const QStringList &wanted);
    /// The names `record` wants its files to have, given the daemon's `current` ones:
    /// TorrentsBackup::namesUnder its layout with its renames laid on. `parsed` is its
    /// .torrent, or null for a link, whose names are then the daemon's with the record's
    /// renames (nothing when its layout isn't the .torrent's own). Files the daemon lists and
    /// the record doesn't know keep their names.
    static std::optional<QStringList>
    namesWanted(const TorrentRecord &record, const TorrentFile *parsed, const QStringList &current);

    /// How many links `journalPath`'s journal keeps for finishLinks().
    static int linksWaiting(const QString &journalPath);

    // ---- the run ---------------------------------------------------------------------------

    /// Talks to `engine`'s wire; null for none.
    explicit TransmissionTorrentsImport(TransmissionEngine *engine, QObject *parent = nullptr);
    ~TransmissionTorrentsImport() override;

    void start(const TorrentsBackup::Contents &contents, const Plan &plan, const Options &options);
    /// C40 X7: finishes the links an earlier import added (the journal at
    /// `options.journalPath`) whose details have arrived since: their file choices and
    /// renames, a check, and a start for one that is complete and was started. Those still
    /// without details stay in the journal (`Outcome::linksWaiting`); those no longer on the
    /// daemon leave it. `finished` is emitted as for a run, `Outcome::finished` counting the
    /// links done. Uses `options`' journalPath, localPathFor, check and pollMs.
    void finishLinks(const Options &options);
    /// Stops after the requests on their way. What was added stays; the journal keeps the rest.
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
        QString hash;             ///< the daemon's (its v1 info hash)
        QString savePath;         ///< through the rows
        QStringList current;      ///< a link's files as the daemon names them (finishLinks)
        bool link = false;        ///< added by its link: its journal entry is in `links`
        bool namesAsked = false;  ///< the record renames something, or has another layout
        bool keepStopped = false; ///< a rename refused or its names misplaced: never started
        bool refused = false;     ///< a rename was refused: the later ones are dropped
        bool verify = false;      ///< a check is asked: a rename was sent, or it is resumed
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
    void addBatch();
    void pumpAdds();
    void sendAdd(int index);
    void followUps(int at);
    void applyNames(const std::shared_ptr<Item> &item, std::function<void()> then);
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

    QPointer<TransmissionRpc> m_rpc;
    TorrentsBackup::Contents m_contents;
    Plan m_plan;
    Options m_options;
    Outcome m_outcome;
    Journal m_journal;
    std::unique_ptr<QFile> m_file;
    QList<int> m_order;  ///< plan.add + plan.magnets, queue order first
    QList<int> m_batch;  ///< record indexes of the batch on its way, not yet sent
    QList<Item> m_items; ///< the batch's torrents that were added: their steps are next
    QList<QPair<int, QString>> m_queued; ///< exported place and hash: to the queue's end, in order
    QHash<QString, Tracked> m_byHash;
    QStringList m_checking; ///< hashes the check waits for
    QSet<QString> m_seenChecking;
    QTimer m_pollTimer;
    qint64 m_speedBytes = 1000;
    int m_step = 0;
    int m_next = 0;
    int m_inFlight = 0;
    int m_quietPolls = 0; ///< polls in a row with none of `m_checking` checking
    int m_checkTotal = 0;
    int m_generation = 0;
    bool m_busy = false;
    bool m_adding = false;
    bool m_linksOnly = false; ///< a finishLinks() run
};

} // namespace omaseed::core
