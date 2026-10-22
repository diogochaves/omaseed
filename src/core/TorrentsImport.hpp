#pragma once

// TorrentsImport — a TorrentsBackup file laid onto a qBittorrent (docs/24 §5 "Import torrents",
// §7; D6 a, D7 a, D8 a; #33).
//
// What the sheet shows comes from the pure half, and nothing changes until `start`:
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
//      missing", not downloading from zero). "leave them stopped" skips the check.
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

#include "core/PathMap.hpp"
#include "core/TorrentsBackup.hpp"

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

class TorrentsImport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = 20;

    // ---- save paths (D7 a) -----------------------------------------------------------------

    struct PathRow
    {
        QString from;
        QString to;
        QString source; ///< the server whose path map made the row; empty for one typed in
        bool operator==(const PathRow &other) const = default;
    };
    /// `path` with the longest row `from` it starts with (at a folder boundary) replaced by
    /// that row's `to`; unchanged when no row with both ends filled covers it.
    static QString mapPath(const QString &path, const QList<PathRow> &rows);
    static bool covers(const QList<PathRow> &rows, const QString &path);
    /// The rows the sheet starts with: the exporting server's path map (when omaseed knows that
    /// server: `source`), each remote folder to where the target sees that same folder, and
    /// the target's own map read backwards, for a file whose paths are this machine's. Only
    /// rows that change a path some torrent has.
    static QList<PathRow> prefill(const TorrentsBackup::Contents &contents,
                                  const std::optional<PathMap> &source,
                                  const QString &sourceName,
                                  const PathMap &target,
                                  const QString &targetName);

    // ---- the journal (§7) ------------------------------------------------------------------

    struct Journal
    {
        QString key;                    ///< which export it is about (`keyFor`)
        QHash<QString, QString> stages; ///< id -> "added" (steps left) or "check"

        static QString keyFor(const TorrentsManifest &manifest);
        /// `$XDG_DATA_HOME/omaseed/imports/<server id>.json`.
        static QString pathFor(const QString &serverId);
        /// An empty journal when there is none (or it is damaged).
        static Journal load(const QString &path);
        /// Writes it, or removes the file when nothing is left in it.
        bool save(const QString &path) const;
    };

    // ---- the plan --------------------------------------------------------------------------

    /// What the server has now.
    struct Here
    {
        QSet<QString> ids; ///< every torrent id, v1 and v2 hash, lower case
        QStringList categories;
        QStringList tags;
    };
    struct Plan
    {
        QList<int> add;     ///< record indexes: new, with metadata
        QList<int> magnets; ///< new links without metadata
        QList<int> finish;  ///< here, left unfinished by an earlier run of this file
        QList<int> skipped; ///< here already (D8 a)
        qint64 addBytes = 0;
        qint64 skippedBytes = 0;
        QStringList categories; ///< to create first
        QStringList tags;       ///< to create first

        int count() const { return int(add.size() + magnets.size() + finish.size()); }
    };
    static Plan plan(const TorrentsBackup::Contents &contents,
                     const Here &here,
                     const Journal &journal = Journal());
    /// How many of the torrents the plan sends keep their save path because no row covers it.
    static int uncovered(const TorrentsBackup::Contents &contents,
                         const Plan &plan,
                         const QList<PathRow> &rows);

    /// A rename from `from` to `to` (relative to `localBase`, the torrent's folder as this
    /// machine sees it) would replace a file that is there: both names are on disk. An empty
    /// `localBase` sees nothing and allows it.
    static bool
    renameWouldReplace(const QString &localBase, const QString &from, const QString &to);

    // ---- the run ---------------------------------------------------------------------------

    struct Options
    {
        QList<PathRow> rows;
        bool check = true; ///< D6 a; false: "leave them stopped"
        QString journalPath;
        /// The folder on this machine for a server path, "" where it can't see it (App's
        /// FreeSpaceProbe::localPathFor).
        std::function<QString(const QString &)> localPathFor;
        int batch = kBatch;
        int pollMs = 2000;
    };
    struct Outcome
    {
        int added = 0;    ///< with metadata
        int magnets = 0;  ///< links
        int finished = 0; ///< from an earlier run
        int started = 0;
        int leftStopped = 0; ///< checked, and not complete (or kept stopped)
        int categories = 0;  ///< created
        int tags = 0;        ///< created
        int keptManual = 0;  ///< exported under automatic management, kept manual here
        QStringList refused; ///< torrents whose renames would have replaced a file on disk
        QStringList errors;
        bool cancelled = false;
        bool complete = false; ///< every step ran
    };

    explicit TorrentsImport(QbtClient *client, QObject *parent = nullptr);
    ~TorrentsImport() override;

    void start(const TorrentsBackup::Contents &contents, const Plan &plan, const Options &options);
    /// Stops after the request on its way. What was added stays; the journal keeps the rest.
    void cancel();
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// `phase` "adding" or "checking"; `done` of `total`.
    void progress(const QString &phase, int done, int total);
    void finished(const omaseed::core::TorrentsImport::Outcome &outcome);

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
    int m_polls = 0;
    int m_checkTotal = 0;
    int m_generation = 0;
    bool m_busy = false;
    bool m_adding = false;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::TorrentsImport::Outcome)
