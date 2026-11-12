#pragma once

// DelugeTorrentsExport — writes a Deluge's torrents to a TorrentsBackup file (docs/57 §2, §4;
// docs/DECISIONS.md C40 X1, X3; #88), in TorrentsExport's shape: the same start, cancel,
// progress and Outcome, so whoever drives an export needs to know the engine only to make one.
//
// It reads the daemon (version, core.conf, whether the Label plugin is on), then every
// torrent's id, place in the queue and whether it has its metadata, writes the manifest, and
// goes through the torrents 20 at a time (`kBatch`): one core.get_torrents_status per batch
// for what a record holds, then each .torrent, the batch written as soon as it is in. A
// torrent removed while the export runs is left out and counted.
//
// The .torrent (C40 X3). No method of Deluge's hands over the bytes: each is
// `<config>/state/<id>.torrent` on the daemon's disk, as it was added. Nothing in the API
// names the config folder either; core.conf's `plugins_location` is `<config>/plugins` unless
// someone moved it, so the state folder is taken to be its sibling (stateFolder()). A file is
// read there, or, with setTorrentsFolder(), under that folder (the daemon's `state` as this
// machine sees it: a container's volume, a share). A wrong guess costs nothing: a file that
// can't be read, or whose info hash isn't the torrent's, makes that torrent go in as its
// magnet link (core.get_magnet_uri) with its whole record. A magnet that never got its
// metadata is always a link. reach() answers beforehand whether the files can be read from
// here; with no `plugins_location` and no folder given it says the folder is unknown (an
// empty `daemonFolder`) and not readable. No folder is guessed from this machine's home.
//
// A record keeps qBittorrent's fields (TorrentsBackup's header): `category` its one label
// (none with the Label plugin off) and no tags; the queue position counted among the
// unfinished torrents from 1, as qBittorrent's; file priorities in qBittorrent's values
// (DelugeTranslate::filePriority: 0 not wanted, 1 normal, 6 high) and 2 for low, which
// qBittorrent has not. Its trackers are the API's: the .torrent on disk never follows a
// tracker edit. Its renames are told against the .torrent's own names when it is in the file
// (TorrentsBackup::renamesOf, as the other engines' records), and for a link against
// `orig_files`, which Deluge keeps beside `files`: a Deluge's link keeps its renames.
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

class DelugeEngine;
class DelugeRpc;

class DelugeTorrentsExport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = TorrentsExport::kBatch;
    /// How many torrents' .torrent files reach() tries.
    static constexpr int kReachSample = 20;

    /// The qBittorrent export's, so one handler takes any engine's `finished`. `magnets`
    /// counts every torrent that went in as a link: without metadata, or without a .torrent
    /// this machine could read.
    using Outcome = TorrentsExport::Outcome;

    /// Whether the daemon's .torrent files can be read from this machine (C40 X3).
    struct Reach
    {
        bool readable = false;
        /// Where the daemon keeps them, in its own words (stateFolder()); empty when its
        /// settings don't tell: only a folder given can be read then.
        QString daemonFolder;
        int tried = 0;   ///< .torrent files looked for (up to kReachSample)
        int missing = 0; ///< of which not there, unreadable, or another torrent's
    };

    /// Talks to `engine`'s wire; null for none.
    explicit DelugeTorrentsExport(DelugeEngine *engine, QObject *parent = nullptr);
    ~DelugeTorrentsExport() override;

    /// The daemon's `state` folder as this machine sees it; empty (the default) reads the
    /// folder the daemon's settings point at. Set before start().
    void setTorrentsFolder(const QString &folder);
    QString torrentsFolder() const { return m_torrentsFolder; }

    void start(const QString &path,
               const QString &serverName,
               const QString &serverUrl,
               const QString &appVersion);
    void cancel();
    bool isBusy() const { return m_busy; }

    /// Asks `engine`'s daemon where its .torrent files are and tries up to kReachSample of
    /// them from here: under `torrentsFolder` when given, else in the daemon's state folder.
    /// `readable` when every one tried is the torrent's own .torrent; with no torrent that has
    /// its metadata, when the folder itself can be read. `parent` owns the reply until it
    /// settles (the engine when null).
    static Reply<Reach> *
    reach(DelugeEngine *engine, const QString &torrentsFolder = {}, QObject *parent = nullptr);

    /// The daemon's state folder from its core.get_config: `state` beside `plugins_location`.
    /// Empty when the settings carry no such path.
    static QString stateFolder(const QJsonObject &config);
    /// Where this machine reads torrent `id`'s .torrent: `<id>.torrent` under `torrentsFolder`
    /// when one is given, else under `daemonFolder`. Empty with neither, or no id.
    static QString
    localTorrentPath(const QString &id, const QString &daemonFolder, const QString &torrentsFolder);
    /// The .torrent at `path` when it can be read and is `hash`'s (its v1 info hash, which is
    /// Deluge's id for a torrent); empty otherwise.
    static QByteArray readTorrent(const QString &path, const QString &hash);
    /// The status keys a record is made of.
    static QStringList recordFields();
    /// The link omaseed makes of a torrent's status itself (its id, name and trackers), for
    /// when core.get_magnet_uri gives none.
    static QString magnetOf(const QJsonObject &json);
    /// One torrent's record from its status (recordFields(), with its id as `hash`).
    /// `labelPlugin`: the Label plugin is on, so `label` is read; `queuePosition` its place
    /// among the unfinished torrents from 1, 0 for none; `torrent` its .torrent, empty to make
    /// it a link, which is then `magnet` (magnetOf() when that is empty too).
    static TorrentRecord recordFrom(const QJsonObject &json,
                                    bool labelPlugin,
                                    int queuePosition,
                                    const QByteArray &torrent,
                                    const QString &magnet = {});

Q_SIGNALS:
    /// `done` of `total` torrents are in the file.
    void progress(int done, int total);
    void finished(const omaseed::core::TorrentsExport::Outcome &outcome);

private:
    struct Pending
    {
        QJsonObject row;
        QByteArray torrent;
        QString magnet;
    };

    void readVersion();
    void readConfig();
    void readPlugins();
    void readList();
    void open();
    void nextBatch();
    void nextMagnet();
    void writeBatch();
    void fail(const QString &error);
    void finish(Outcome outcome);

    QPointer<DelugeRpc> m_rpc;
    QString m_torrentsFolder;
    QString m_daemonFolder;
    TorrentsManifest m_manifest;
    bool m_labelPlugin = false;
    QStringList m_ids;           ///< every torrent, by id
    QHash<QString, int> m_queue; ///< id → place among the unfinished, from 1
    QList<Pending> m_pending;    ///< the batch being read
    qsizetype m_magnetAt = 0;
    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<TarWriter> m_writer;
    QString m_path;
    int m_next = 0;
    int m_generation = 0;
    Outcome m_outcome;
    bool m_busy = false;
};

} // namespace omaseed::core
