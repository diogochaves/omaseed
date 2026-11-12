#pragma once

// TransmissionTorrentsExport — writes a Transmission's torrents to a TorrentsBackup file
// (docs/57 §2, §4; docs/DECISIONS.md C40 X1, X3; #88), in TorrentsExport's shape: the same
// start, cancel, progress and Outcome, so whoever drives an export needs to know the engine
// only to make one.
//
// It reads the session (version, default folders, the speed unit), then every torrent's hash,
// labels and place in the queue, writes the manifest, and goes through the torrents 20 at a
// time (`kBatch`): one torrent_get per batch for what a record holds, then each .torrent, the
// batch written as soon as it is in. A torrent removed while the export runs is left out and
// counted.
//
// The .torrent (C40 X3). Transmission's API never hands over the bytes, only `torrent_file`:
// a path on the daemon's disk (`<config_dir>/torrents/<hash>.torrent`). It is read where the
// daemon says it is, or, with setTorrentsFolder(), under that folder by its file name (the
// daemon's folder as this machine sees it: a container's volume, a share). A file that can't
// be read, or whose info hash isn't the torrent's, makes that torrent go in as its magnet link
// (`magnet_link`) with its whole record but its renames, which only the .torrent's own names
// can tell. A magnet that never got its metadata is always a link. reach() answers beforehand
// whether the files can be read from here.
//
// A record keeps qBittorrent's fields (TorrentsBackup's header): `category` empty and `tags`
// the labels in the daemon's order; the queue position counted among the unfinished torrents
// from 1, as qBittorrent's; file priorities in qBittorrent's values (not wanted 0, high 6,
// low and normal 1, which has no low).
//
// The file is written to a temporary name beside the target and only takes its place when
// every torrent is in; it is private (0600) from the start (DECISIONS C9).

#include "core/Reply.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/qbt/TorrentsExport.hpp"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <memory>

namespace omaseed::core {

class TransmissionEngine;
class TransmissionRpc;

class TransmissionTorrentsExport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = TorrentsExport::kBatch;
    /// How many torrents' .torrent files reach() tries.
    static constexpr int kReachSample = 20;

    /// The qBittorrent export's, so one handler takes either engine's `finished`. `magnets`
    /// counts every torrent that went in as a link: without metadata, or without a .torrent
    /// this machine could read.
    using Outcome = TorrentsExport::Outcome;

    /// Whether the daemon's .torrent files can be read from this machine (C40 X3).
    struct Reach
    {
        bool readable = false;
        /// Where the daemon keeps them, in its own words: the folder of a `torrent_file`, or
        /// `<config_dir>/torrents` when it has no torrent to ask about.
        QString daemonFolder;
        int tried = 0;   ///< .torrent files looked for (up to kReachSample)
        int missing = 0; ///< of which not there, unreadable, or another torrent's
    };

    /// Talks to `engine`'s wire; null for none.
    explicit TransmissionTorrentsExport(TransmissionEngine *engine, QObject *parent = nullptr);
    ~TransmissionTorrentsExport() override;

    /// The daemon's `torrents` folder as this machine sees it; empty (the default) reads each
    /// `torrent_file` path as the daemon gives it. Set before start().
    void setTorrentsFolder(const QString &folder);
    QString torrentsFolder() const { return m_torrentsFolder; }

    void start(const QString &path,
               const QString &serverName,
               const QString &serverUrl,
               const QString &appVersion);
    void cancel();
    bool isBusy() const { return m_busy; }

    /// Asks `engine`'s daemon where its .torrent files are and tries up to kReachSample of
    /// them from here: under `torrentsFolder` when given, else where the daemon says.
    /// `readable` when every one tried is the torrent's own .torrent; with no torrent that has
    /// its metadata, when the folder itself can be read. `parent` owns the reply until it
    /// settles (the engine when null).
    static Reply<Reach> *reach(TransmissionEngine *engine,
                               const QString &torrentsFolder = {},
                               QObject *parent = nullptr);

    /// Where this machine reads the .torrent the daemon calls `torrentFile`: the path itself,
    /// or its file name under `torrentsFolder` when one is given. Empty for an empty path.
    static QString localTorrentPath(const QString &torrentFile, const QString &torrentsFolder);
    /// The .torrent at `path` when it can be read and is `hash`'s (its v1 info hash, which is
    /// what Transmission keys a torrent by); empty otherwise.
    static QByteArray readTorrent(const QString &path, const QString &hash);
    /// The torrent_get fields a record is made of.
    static QStringList recordFields();
    /// One torrent's record from its torrent_get object (recordFields()). `speedBytes` is the
    /// session's `units.speed_bytes`; `queuePosition` its place among the unfinished torrents
    /// from 1, 0 for none; `torrent` its .torrent, empty to make it a link.
    static TorrentRecord recordFrom(const QJsonObject &json,
                                    qint64 speedBytes,
                                    int queuePosition,
                                    const QByteArray &torrent);

Q_SIGNALS:
    /// `done` of `total` torrents are in the file.
    void progress(int done, int total);
    void finished(const omaseed::core::TorrentsExport::Outcome &outcome);

private:
    void readSession();
    void readList();
    void open();
    void nextBatch();
    void writeBatch(const QList<QJsonObject> &rows);
    void fail(const QString &error);
    void finish(Outcome outcome);

    QPointer<TransmissionRpc> m_rpc;
    QString m_torrentsFolder;
    TorrentsManifest m_manifest;
    qint64 m_speedBytes = 1000;
    QStringList m_hashes;        ///< every torrent, in the daemon's order
    QHash<QString, int> m_queue; ///< hash → place among the unfinished, from 1
    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<TarWriter> m_writer;
    QString m_path;
    int m_next = 0;
    int m_generation = 0;
    Outcome m_outcome;
    bool m_busy = false;
};

} // namespace omaseed::core
