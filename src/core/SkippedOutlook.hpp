#pragma once

// SkippedOutlook — what "Free the space…" can do for a torrent, decided from the Files tab's
// reading alone (board 50 §3, DECISIONS C30 F12–F16), and the size line that says a skipped
// file still on disk (§4, F17–F18).
//
// The reading (SkippedOnDisk) already has what decides it: the hash version from the list, each
// skipped file's space on this machine's disk and whether it holds downloaded pieces (its
// progress, and pieceStates where the reading had them). On a hybrid or a v2-only torrent a
// skipped file with pieces is freed only by adding the torrent again (C17 D3, C29); when every
// file that holds space is one of those, the band, shift+f and the palette say "add it again"
// up front (F12–F13). The plan at delete time reads again and has the last word (F16). Pure Qt
// Core; `tst_skippedoutlook`.

#include "core/SkippedOnDisk.hpp"

#include <QString>

namespace omaseed::core {

struct SkippedOutlook
{
    enum class Kind {
        None,  ///< no skipped file holds space here (or the disk can't be seen)
        Free,  ///< every one that holds space can go
        Mixed, ///< some can go, some only by adding it again (F15)
        Readd, ///< only adding it again frees any of it (F12)
    };
    Kind kind = Kind::None;
    HashVersion version = HashVersion::V1;
    qint64 freeable = 0;  ///< what the free itself could give back
    qint64 readdOnly = 0; ///< what only adding it again frees
    int readdFiles = 0;   ///< the files behind `readdOnly`

    /// The outlook for `reading` on a torrent of `version`.
    static SkippedOutlook of(const SkippedReading &reading, HashVersion version);
    /// "none", "free", "mixed", "readd", for QML.
    static QString kindName(Kind kind);

    /// F17 a: what the skipped files still hold, after the size and the files head's count:
    /// "16.0 MB still on disk".
    static QString stillOnDisk(qint64 held);
    /// F17–F18: the size fact's tail, after "32.0 MB · ". On a disk omaseed sees and with space
    /// held, "2 skipped, 16.0 MB still on disk"; on one it can't see, with skipped files that
    /// were downloaded to, "1.03 GB of it in skipped files" (`downloaded`: each skipped file's
    /// size × progress, torrents/files' alone); otherwise "".
    static QString sizeTail(int skipped, qint64 held, bool seen, qint64 downloaded);
};

} // namespace omaseed::core
