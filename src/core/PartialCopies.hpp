#pragma once

#include <QString>

namespace omaseed::core {

/// docs/44 D13 (#121): what a failed move left in the destination. When qBittorrent can't move
/// a finished torrent out of its incomplete folder (the destination full), libtorrent stops
/// part way: the files it had copied stay there, and nothing cleans them up (#117: 699 of
/// them). Read on this machine, only where omaseed can see the destination; board 42's loose
/// files are the same idea for a folder.
struct PartialCopies
{
    /// The destination's folder is visible here, so what is (or isn't) in it is known.
    bool seen = false;
    int files = 0;
    qint64 bytes = 0;

    /// What is at `localPath` (the torrent's content as it would be in the destination: a
    /// file, or its top folder). Not seen when `localPath` is empty or its folder isn't there.
    static PartialCopies read(const QString &localPath);
};

} // namespace omaseed::core
