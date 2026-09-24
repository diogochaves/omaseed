#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

/// One file described by a .torrent, with its path relative to the torrent's root ('/'
/// separated, even for a single-file torrent where it is just the torrent's name). BEP-47
/// pad files are left out, so the n-th entry is the file qBittorrent calls index n.
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

    /// The id qBittorrent keys this torrent by, and that every `hash=` parameter after the
    /// add takes: the v1 infohash when the torrent has v1 metadata (v1 or hybrid), otherwise
    /// the v2 infohash cut to its first 40 hex digits (BEP-52's truncated form).
    QString torrentId() const;

    /// Builds a magnet link (`magnet:?xt=urn:btih:...&dn=...&tr=...`) for this torrent: the
    /// v1 topic when it has v1 metadata, `xt=urn:btmh:1220<sha256>` when it has v2 (both for
    /// a hybrid), and every tracker from announce/announce-list, deduplicated in order.
    QString magnetLink() const;

private:
    bool m_valid = false;
    /// The info dict carries v1 "pieces" (v1-only or hybrid).
    bool m_hasV1 = false;
};

} // namespace omaseed::core
