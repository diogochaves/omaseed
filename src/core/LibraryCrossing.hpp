#pragma once

// LibraryCrossing — what a torrents file written by one engine becomes on another
// (docs/57 §5, docs/DECISIONS.md C40 X4, X5; #88).
//
// Pure data: the file's records and the two engines in, the answers the import sheet shows
// out. Nothing here talks to a server or holds words: the kinds are enums and the UI names
// them.
//
// A record holds its grouping in qBittorrent's two fields whatever engine wrote it
// (TorrentsBackup.hpp): a Transmission's labels are its `tags`, a Deluge's one label is its
// `category`. The grouping this file answers with is in that same shape for the target, so
// an importer sends `tags` as a Transmission's labels and `category` as a Deluge's label.
//
// An engine this build doesn't know (EngineKind::Unknown) is read as qBittorrent, as a file
// without an engine is (C40 X1).

#include "core/TorrentsBackup.hpp"
#include "core/engines/EngineKind.hpp"

#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

class LibraryCrossing
{
public:
    /// What a grouping name is on its engine: qBittorrent has a category and tags,
    /// Transmission and Deluge have labels.
    enum class NameKind {
        Category,
        Tag,
        Label,
    };

    /// One record's grouping as the target takes it (C40 X4), in a record's own shape.
    struct Grouping
    {
        QString category;       ///< qBittorrent's category, Deluge's one label; empty: none
        QStringList tags;       ///< qBittorrent's tags, Transmission's labels, in order
        QStringList leftBehind; ///< the source's names the target has no place for
        bool operator==(const Grouping &other) const = default;
    };

    /// What one source name becomes for some of the torrents that carry it.
    struct Outcome
    {
        bool kept = false; ///< false: left behind, `kind` and `name` unset
        NameKind kind = NameKind::Label;
        QString name;     ///< as the target spells it
        int torrents = 0; ///< how many torrents it goes this way for
        bool operator==(const Outcome &other) const = default;
    };

    /// One name of the source, for the sheet's names block. A category and a tag of the same
    /// name are two entries. `outcomes` has one entry per distinct thing the name becomes, in
    /// first-seen order: usually one, two when a Transmission label is the first label of
    /// some torrents (a category on qBittorrent) and a later one of others (a tag).
    struct Name
    {
        NameKind kind = NameKind::Label; ///< on the source
        QString name;
        QList<Outcome> outcomes;
        bool operator==(const Name &other) const = default;
    };

    /// What the target has no counterpart for (C40 X5), in the order the block lists them.
    enum class LossKind {
        SeedingTimeLimit, ///< a seeding-time limit: Transmission and Deluge have none
        IdleLimit,        ///< an idle limit: Deluge has none
        ShareLimitAction, ///< the limit's action: Transmission only stops, Deluge stops or removes
        AutoManagement,   ///< automatic management: neither has it
        IncompleteFolder, ///< an incomplete folder of its own: neither has it
        GroupingNames,    ///< names left behind: Deluge keeps one label
    };

    struct Loss
    {
        LossKind kind = LossKind::SeedingTimeLimit;
        int torrents = 0;
        bool operator==(const Loss &other) const = default;
    };

    /// `name` as a Deluge label: lower-case, every run of characters outside `a-z 0-9 _ -`
    /// one `-`, no `-` at either end. Empty when nothing is left ("Field Recordings" →
    /// "field-recordings").
    static QString delugeLabel(const QString &name);

    /// The grouping of `record`, from a file `from` wrote, on a server running `to` (X4).
    /// The same engine both sides: unchanged, nothing left behind.
    static Grouping grouping(EngineKind from, EngineKind to, const TorrentRecord &record);

    /// Every distinct source name in `records`, in first-seen order, with what it becomes.
    static QList<Name> names(EngineKind from, EngineKind to, const QList<TorrentRecord> &records);

    /// What `to` can't keep of `records`, counted in torrents (X5): only the kinds that
    /// count something, in LossKind's order. Nothing when the engine is the same.
    static QList<Loss> losses(EngineKind from, EngineKind to, const QList<TorrentRecord> &records);
};

} // namespace omaseed::core
