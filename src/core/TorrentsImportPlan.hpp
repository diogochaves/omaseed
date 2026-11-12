#pragma once

// TorrentsImportPlan — the half of a torrents import that no engine owns (docs/24 §5, §7;
// docs/DECISIONS.md C9, C40 X6; #33, #88): what the sheet shows before anything changes, the
// journal a run keeps, and the choice and the result every engine's importer shares, so
// whoever drives an import needs to know the engine only to make one.
//
//   * `plan`: each torrent is new (added), already on the server by any of its hashes (skipped,
//     D8 a), a magnet without metadata (added as a link), or one an earlier run added and did
//     not finish (the journal says so: finished, not skipped, §7 "running it again"). The
//     names the sent torrents use and the server lacks are listed where the engine creates
//     them first (a qBittorrent's categories and tags); `grouping` says each record's.
//   * Save paths go through "from → to" rows (D7 a): the longest `from` a path starts with, at a
//     folder boundary, is replaced by its `to`; a path no row covers is kept as it is, and
//     `uncovered` counts those for the sheet's "!" line. `prefill` makes the first rows from
//     the path maps omaseed already has (DECISIONS C9).
//   * `Journal`, one file per server, records each torrent between its add and its last step
//     (§7), and the links still waiting for their details (C40 X7).
//
// The importers: TorrentsImport (a qBittorrent), TransmissionTorrentsImport.

#include "core/LibraryCrossing.hpp"
#include "core/PathMap.hpp"
#include "core/TorrentsBackup.hpp"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace omaseed::core {

class TorrentsImportPlan
{
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
        QHash<QString, QString> stages; ///< id -> "added" (steps left), "check" or "hold"
        /// Links added and still waiting for their details (C40 X7), by id: what is left to
        /// do once they arrive, kept here as the file may be gone by then. They outlive the
        /// run and its `key`. Only an importer that finishes links later writes any.
        QHash<QString, QJsonObject> links = {};

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
    /// One record's grouping as the target takes it.
    using GroupingOf = std::function<LibraryCrossing::Grouping(const TorrentRecord &)>;
    /// `grouping` names what the sent torrents use, for `categories` and `tags` (what `here`
    /// lacks); none for an engine with nothing to create first.
    static Plan plan(const TorrentsBackup::Contents &contents,
                     const Here &here,
                     const Journal &journal,
                     const GroupingOf &grouping = {});
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
        /// Torrents whose files are not where the file says, because the target can't name
        /// them so (a Transmission keeps a torrent's top folder): added, left stopped.
        QStringList misplaced;
        int linksWaiting = 0; ///< links whose details have not arrived yet (C40 X7)
        /// What the target can't keep of the torrents sent (C40 X5), for the result to repeat.
        QList<LibraryCrossing::Loss> losses;
        QStringList errors;
        bool cancelled = false;
        bool complete = false; ///< every step ran
    };
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::TorrentsImportPlan::Outcome)
