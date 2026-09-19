#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

/// One file described by a .torrent, with its path relative to the torrent's root ('/'
/// separated, even for a single-file torrent where it is just the torrent's name).
struct TorrentFileEntry
{
    QString path;
    qint64 size = 0;

    bool operator==(const TorrentFileEntry &other) const = default;
};

/// A parsed .torrent file: metadata plus the exact infohash(es) needed to talk to trackers
/// and qBittorrent about it. Parsing is synchronous and pure (no I/O); callers doing this for
/// large files off the UI thread should run parse() on QtConcurrent themselves.
class TorrentFile
{
public:
    /// Parses a .torrent file's raw bytes. On failure returns a TorrentFile with isValid()
    /// false; when errorMessage is non-null it is filled with why.
    static TorrentFile parse(const QByteArray &data, QString *errorMessage = nullptr);

    bool isValid() const { return m_valid; }

    QString name;
    qint64 totalSize = 0;
    QList<TorrentFileEntry> files;

    /// Lowercase hex SHA-1 of the original "info" dict bytes. Empty only when parsing failed.
    QString infoHashV1;
    /// Lowercase hex SHA-256 of the original "info" dict bytes, present only when the torrent
    /// declares "meta version" 2 (BEP-52 v2-only or v1+v2 hybrid). Empty for v1-only torrents.
    QString infoHashV2;

    QString announce;
    /// BEP-12 announce-list: each entry is one tier of tracker URLs, tried in order.
    QList<QStringList> announceList;

    QString comment;
    QString createdBy;
    /// Unix seconds from the "creation date" field, or 0 when absent.
    qint64 creationDate = 0;

    bool isPrivate = false;
    qint64 pieceLength = 0;
    qint64 pieceCount = 0;

    /// Builds a magnet link (`magnet:?xt=urn:btih:...&dn=...&tr=...`) for this torrent, using
    /// infoHashV1 and every tracker from announce/announce-list, deduplicated in order.
    QString magnetLink() const;

private:
    bool m_valid = false;
};

} // namespace omaseed::core
