#pragma once

// AddAgainJob — what frees a hybrid's leftover files, and clears libtorrent's
// torrent_inconsistent_hashes on a hybrid already in it (board 28; docs/44 §2 F2, DECISIONS
// C17 D3, #118): remove the torrent keeping its files, delete the leftovers, add the same
// .torrent (torrents/export) stopped with its save path, layout, category, tags, limits, file
// names and priorities, then start it if it was started. Its ratio, seeding time and added date
// are lost; the sheet says so before it runs.
//
// A qBittorrent surface (docs/36 §4.4): the record it keeps is C9's TorrentRecord, read from
// torrents/info, files, trackers and export, so it takes the engine and talks to its
// transport. App offers it when EngineFeatures::addAgain says the server can.
//
// Each step is logged ("free space: …"), the way omaseed logs what it does to a server.

#include "core/TorrentsBackup.hpp"

#include "core/engines/qbt/QbtClient.hpp"

#include <QFile>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace omaseed::core {

class QbtEngine;

class AddAgainJob : public QObject
{
    Q_OBJECT

public:
    struct Input
    {
        QString hash;
        /// Local paths deleted once the torrent is gone and before it comes back: a hybrid's
        /// skipped files that hold downloaded pieces. Empty for an errored hybrid (board 28).
        QStringList deletePaths;
        std::function<bool(const QString &)> remove = [](const QString &path) {
            return QFile::remove(path);
        };
        int pollMs = 500;
        int waitMs = 30000;
    };
    struct Outcome
    {
        bool ok = false;
        QString error;
        QString hash; ///< the torrent as it came back
        int deleted = 0;
        QStringList warnings; ///< steps after the add that the server refused
    };

    explicit AddAgainJob(QbtEngine *engine, QObject *parent = nullptr);

    /// The add's options for `record`: stopped, at its save path and incomplete folder, in its
    /// layout, category and tags, with its speed limits and download options.
    static QbtClient::AddOptions addOptions(const TorrentRecord &record);
    /// File ids by priority, for the non-normal priorities of `record` (one filePrio each).
    static QMap<int, QList<int>> priorityGroups(const TorrentRecord &record);

    void start(const Input &input);
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// "reading", "removing", "deleting", "adding", "restoring", "starting".
    void step(const QString &what);
    void finished(const omaseed::core::AddAgainJob::Outcome &outcome);

private:
    void readRecord();
    void waitGone(int waited);
    void add();
    void waitAdded(int waited);
    void restore();
    void finish(const QString &error = QString());

    QPointer<QbtClient> m_client;
    Input m_input;
    Outcome m_outcome;
    TorrentRecord m_record;
    QByteArray m_torrent;
    QString m_apiVersion;
    std::shared_ptr<std::function<void()>> m_chain;
    bool m_busy = false;
};

} // namespace omaseed::core
