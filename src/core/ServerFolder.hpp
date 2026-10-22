#pragma once

// ServerFolder — what a server listed of one folder on its own disks
// (EngineClient::folderContent()), for a server whose disks omaseed can't see (DECISIONS C16
// L13, #102). A move checks a torrent's target against it (C15 M8), and lists the files no
// torrent owns by name. qBittorrent's answer (app/getDirectoryContent) is read into it by
// QbtTranslate::serverFolder().
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
};

} // namespace omaseed::core
