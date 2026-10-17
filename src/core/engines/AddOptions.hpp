#pragma once

// What the add sheet, the bar widget and a batch ask of an engine when they add torrents, in
// omaseed's terms (docs/36 §4, docs/DECISIONS.md C14). An unset field is left to the server
// (or, for the fields automatic management decides, to the category). The qBittorrent engine
// turns these into torrents/add's form; another engine sends what it can.

#include "core/engines/DetailsData.hpp"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

/// How the files of a new torrent are laid out under its save path.
enum class ContentLayout {
    Original,    ///< as the metainfo has them
    Subfolder,   ///< always in a folder named after the torrent
    NoSubfolder, ///< never in one
};

/// When a torrent that is not added stopped stops all the same (docs/29 D5).
enum class StopCondition {
    MetadataReceived, ///< as soon as its metadata is in, so its files can be chosen
    FilesChecked,     ///< once its files are checked
};

struct AddOptions
{
    std::optional<QString> savePath;
    std::optional<QString> category;
    QStringList tags;
    std::optional<bool> stopped;
    std::optional<bool> skipChecking;
    std::optional<ContentLayout> contentLayout;
    std::optional<QString> rename;
    std::optional<qint64> upLimit; ///< bytes/s
    std::optional<qint64> dlLimit; ///< bytes/s
    std::optional<bool> autoManagement;
    std::optional<bool> sequentialDownload;
    std::optional<bool> firstLastPiece;
    /// One per file, in file order. Only for one magnet or link whose metadata the engine
    /// already looked up (EngineClient::fetchMetadata, docs/29 §2); empty leaves it off.
    QList<FilePriority> filePriorities;
    std::optional<StopCondition> stopCondition;
};

/// One `.torrent` of an add: the name it is uploaded under and its bytes.
struct AddFile
{
    QString fileName;
    QByteArray bytes;
};

/// What an add answered when it went through.
struct AddResult
{
    /// The server took the request and refused the torrents in it (qBittorrent 4.x answers
    /// "Fails." with a 200 for that; 5.x answers 409, which is an error).
    bool refused = false;
    /// The ids of the torrents it added, when the server says (qBittorrent 5.x).
    QStringList addedIds;
};

} // namespace omaseed::core
