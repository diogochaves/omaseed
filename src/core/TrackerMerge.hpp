#pragma once

#include <QString>
#include <QStringList>

namespace omaseed::core {

/// docs/25 §2 (#26): which of a duplicate source's trackers the torrent already in the
/// library lacks. Trackers are compared the way a tracker is reached: scheme and host
/// without regard to case, the port spelled out (`http://t.example/announce` is
/// `http://t.example:80/announce`), the path as written, a trailing slash ignored.
/// qBittorrent's own pseudo rows (`** [DHT] **` and the like) never match anything.
namespace TrackerMerge {

/// The comparison key for one tracker URL, or "" when it is not a URL a tracker could be.
QString key(const QString &url);

/// `incoming` minus everything in `existing`, deduplicated, in `incoming`'s order, each as
/// written in `incoming`.
QStringList missing(const QStringList &incoming, const QStringList &existing);

} // namespace TrackerMerge

} // namespace omaseed::core
