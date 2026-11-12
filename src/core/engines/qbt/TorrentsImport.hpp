#pragma once

// TorrentsImport — a TorrentsBackup file laid onto a qBittorrent (docs/24 §5 "Import torrents",
// §7; D6 a, D7 a, D8 a; #33).
//
// What the sheet shows comes from the pure half (TorrentsImportPlan, shared with the other
// engines' importers), and nothing changes until `start`:
//   * `plan`: each torrent is new (added), already on the server by any of its hashes (skipped,
//     D8 a), a magnet without metadata (added as a link), or one an earlier run added and did
//     not finish (the journal says so: finished, not skipped, §7 "running it again"). The
//     categories and tags the sent torrents use and the server lacks are created first.
//   * Save paths go through "from → to" rows (D7 a): the longest `from` a path starts with, at a
//     folder boundary, is replaced by its `to`; a path no row covers is kept as it is, and
//     `uncovered` counts those for the sheet's "!" line. `prefill` makes the first rows from
//     the path maps omaseed already has (DECISIONS C9).
//
// `start` then runs, one step at a time:
//   1. categories (their folders through the same rows), then tags;
//   2. the torrents, `batch` (20) at a time and in the exported queue order, each added
//      stopped, with its content layout, folders, category, tags and speed limits; torrents
//      whose options agree share one request. After each batch: the renames, file
//      priorities, trackers the .torrent lacks, share limits and automatic management;
//   3. the queue order, by moving the queued ones to the bottom in their exported order;
//   4. (D6 a) a check of every added torrent; once qBittorrent has checked one, it starts if
//      it is complete and was started at export, and stays stopped otherwise ("files
//      missing", not downloading from zero). "leave them stopped" skips the check. A torrent
//      waiting for its turn (qBittorrent checks one at a time) shows stopped at 0 %, as one
//      already checked with nothing on disk does: such a one is settled only after a few
//      polls in which none of the import's torrents was checking (#208).
//
// Renames never replace a file on the target's disk (#64, #67): where this
// machine sees the server's folders (`localPathFor`), a rename whose old and new names are both
// files there is not sent, and its torrent is never started. Elsewhere the renames are sent,
// as the rename sheets do (#64: where the disk can't be seen, nothing new is said).
//
// A journal (`Journal`, one file per server) records each torrent between its add and its
// last step, saved after every batch, so a run that fails (the server gone, the disk full) or
// is stopped leaves what was added, and the next run of the same file finishes those and adds
// the rest (§7). A .torrent the server can't read (415) is not such a failure: its batch goes
// again one torrent at a time, that one is named in `errors` and the run goes on (#71). Each
// .torrent is sent with its piece layers put right first (`TorrentFile::repairPieceLayers`).
//
// A file another engine wrote (docs/57 §5, docs/DECISIONS.md C40 X4; #88) holds labels, not a
// category and tags: `groupingHere` is each record's grouping as this qBittorrent takes it
// (LibraryCrossing), and the plan and the run use that. A category made from a label has no
// folder of its own, so it is created without one and automatic management stays off for
// those torrents. A file a qBittorrent wrote goes in exactly as before.

#include "core/LibraryCrossing.hpp"
#include "core/PathMap.hpp"
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

class QbtClient;
class QbtEngine;

class TorrentsImport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = TorrentsImportPlan::kBatch;

    // The plan's data, the journal, the choice and the result are every engine's
    // (TorrentsImportPlan); they keep their names here.
    using PathRow = TorrentsImportPlan::PathRow;
    using Journal = TorrentsImportPlan::Journal;
    using Here = TorrentsImportPlan::Here;
    using Plan = TorrentsImportPlan::Plan;
    using Options = TorrentsImportPlan::Options;
    using Outcome = TorrentsImportPlan::Outcome;

    static QString mapPath(const QString &path, const QList<PathRow> &rows)
    {
        return TorrentsImportPlan::mapPath(path, rows);
    }
    static bool covers(const QList<PathRow> &rows, const QString &path)
    {
        return TorrentsImportPlan::covers(rows, path);
    }
    static QList<PathRow> prefill(const TorrentsBackup::Contents &contents,
                                  const std::optional<PathMap> &source,
                                  const QString &sourceName,
                                  const PathMap &target,
                                  const QString &targetName)
    {
        return TorrentsImportPlan::prefill(contents, source, sourceName, target, targetName);
    }

    /// The file was written by another engine (C40 X1): its grouping is crossed (X4).
    static bool crosses(const TorrentsManifest &manifest);
    /// `record`'s category and tags as a qBittorrent takes them (C40 X4): its own for a
    /// qBittorrent's file; for another engine's, the first label the category and the rest
    /// tags. qBittorrent separates tags with commas and so refuses one inside a tag: a label
    /// with a comma becomes a tag with a space in its place.
    static LibraryCrossing::Grouping groupingHere(const TorrentsManifest &manifest,
                                                  const TorrentRecord &record);
    static Plan plan(const TorrentsBackup::Contents &contents,
                     const Here &here,
                     const Journal &journal = Journal());
    static int uncovered(const TorrentsBackup::Contents &contents,
                         const Plan &plan,
                         const QList<PathRow> &rows)
    {
        return TorrentsImportPlan::uncovered(contents, plan, rows);
    }
    static bool renameWouldReplace(const QString &localBase, const QString &from, const QString &to)
    {
        return TorrentsImportPlan::renameWouldReplace(localBase, from, to);
    }

    /// Talks to `engine`'s transport (docs/36 §4.4: a qBittorrent surface); null for none.
    explicit TorrentsImport(QbtEngine *engine, QObject *parent = nullptr);
    ~TorrentsImport() override;

    void start(const TorrentsBackup::Contents &contents, const Plan &plan, const Options &options);
    /// Stops after the request on its way. What was added stays; the journal keeps the rest.
    void cancel();
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// `phase` "adding" or "checking"; `done` of `total`.
    void progress(const QString &phase, int done, int total);
    void finished(const omaseed::core::TorrentsImportPlan::Outcome &outcome);

private:
    struct Item
    {
        int index = -1; ///< into m_contents.records
        QString savePath;
        QString downloadPath;
        QStringList extraTrackers;
        bool keepStopped = false;
        bool autoTmm = false;
    };

    void next();
    struct AddGroup;
    void addBatch();
    void sendAdd(const std::shared_ptr<AddGroup> &group, const std::shared_ptr<int> &waiting);
    void acceptAdded(const QList<Item> &items, const QByteArray &answer);
    void addAnswered(const std::shared_ptr<int> &waiting);
    void awaitAdded(int attempt);
    void followUps(int at);
    void moveQueue(int at);
    void startCheck();
    void poll();
    void done();
    void fail(const QString &error);
    void runSteps(QList<std::function<void(std::function<void()>)>> steps,
                  std::function<void()> then);
    bool live(int generation) const { return m_busy && generation == m_generation; }
    const TorrentRecord &record(int index) const { return m_contents.records.at(index); }
    void saveJournal();
    QString categoryPathHere(const QString &category) const;

    QPointer<QbtClient> m_client;
    TorrentsBackup::Contents m_contents;
    Plan m_plan;
    Options m_options;
    Outcome m_outcome;
    Journal m_journal;
    std::unique_ptr<QFile> m_file;
    QList<int> m_order;  ///< plan.add + plan.magnets, queue order first
    QList<Item> m_items; ///< the batch on its way
    QList<int> m_queued; ///< indexes to move to the bottom of the queue, in order
    QHash<QString, Item> m_byId;
    QStringList m_checking; ///< ids the check waits for
    QSet<QString> m_seenChecking;
    QTimer m_pollTimer;
    QJsonObject m_categoriesHere;
    QString m_savePathHere;
    QString m_apiVersion;
    int m_step = 0;
    int m_next = 0;
    int m_quietPolls = 0; ///< polls in a row with none of `m_checking` checking
    int m_checkTotal = 0;
    int m_generation = 0;
    bool m_busy = false;
    bool m_adding = false;
};

} // namespace omaseed::core
