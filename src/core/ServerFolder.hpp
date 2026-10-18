#pragma once

// ServerFolder — what qBittorrent listed of one folder on its own disks
// (app/getDirectoryContent), for a server whose disks omaseed can't see (DECISIONS C16 L13,
// #102). A move checks a torrent's target against it (C15 M8), and lists the files no torrent
// owns by name.
//
// On 5.2.3: a folder's entries come as full paths; a folder that isn't there (or a file's path)
// answers 404 "Directory does not exist"; a relative or empty path 400 "Invalid directory
// path"; a qBittorrent without the endpoint 404 "Endpoint does not exist". A folder qBittorrent
// can't read lists empty, the same as an empty one.

#include "core/QbtReply.hpp"

#include <QJsonArray>
#include <QSet>
#include <QString>

namespace omaseed::core {

struct ServerFolder
{
    enum class State {
        Listed,      ///< `names` is what is in it
        Missing,     ///< not there: nothing in it either
        Failed,      ///< no answer, or one omaseed can't read: can't tell
        Unsupported, ///< this qBittorrent can't list folders
    };

    State state = State::Failed;
    /// The entries' last parts ("disc1.iso", "Short Film 01").
    QSet<QString> names;

    bool has(const QString &name) const { return state == State::Listed && names.contains(name); }

    static ServerFolder fromReply(const QbtResult<QJsonArray> &result);
};

} // namespace omaseed::core
