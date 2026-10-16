#pragma once

// TorrentStatus — what a torrent is doing, in omaseed's terms rather than one engine's
// (docs/36 §4.1, docs/DECISIONS.md C14).
//
// A phase says what the engine is busy with; flags say how (waiting for peers, waiting in
// the queue, started past the queue, on the finished side). The engine's own word for the
// state is kept beside them, for details and logs: "stalledUP" from qBittorrent. Everything
// the views derive from a state (the rail's groups, the Status column's word and colour,
// whether an ETA means anything) is worked out from phase and flags in one place,
// Torrent.cpp, so each engine only maps its states here. For qBittorrent that map is
// engines/qbt/QbtTranslate.cpp, and Q16's rules come out word for word.

#include <QFlags>
#include <QString>

namespace omaseed::core {

enum class TorrentPhase {
    Unknown,
    Metadata,    ///< a magnet fetching its metadata
    Downloading, ///< wants data (also while stalled or queued, see the flags)
    Seeding,     ///< has what it wants and offers it (also stalled or queued)
    Checking,    ///< hashing its data
    Loading,     ///< reading its saved state at startup (qBittorrent's checkingResumeData)
    Allocating,  ///< making room on disk
    Moving,      ///< its data is being moved
    Stopped,
    Errored,
    Missing, ///< its files are gone from disk
};

enum class TorrentFlag {
    NoFlag = 0x0,
    Stalled = 0x1,  ///< nothing moving: no peer to get from or give to
    Queued = 0x2,   ///< waiting for a slot in the queue
    Forced = 0x4,   ///< started past the queue
    Finished = 0x8, ///< on the finished side: checking, stopped or queued after completing
};
Q_DECLARE_FLAGS(TorrentFlags, TorrentFlag)

struct TorrentStatus
{
    TorrentPhase phase = TorrentPhase::Unknown;
    TorrentFlags flags;
    /// The engine's own word for the state ("stalledUP"), for details; never compared.
    QString word;

    bool has(TorrentFlag flag) const { return flags.testFlag(flag); }
    bool isStopped() const { return phase == TorrentPhase::Stopped; }

    bool operator==(const TorrentStatus &other) const = default;
};

} // namespace omaseed::core

Q_DECLARE_OPERATORS_FOR_FLAGS(omaseed::core::TorrentFlags)
