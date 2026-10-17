#pragma once

// TorrentsExport — writes a qBittorrent's torrents to a TorrentsBackup file (docs/24 §2, §4,
// §7 "Huge libraries"; #33).
//
// It reads the server's versions, default folders, categories, tags and torrents/info, writes
// the manifest, then goes through the torrents 20 at a time (`kBatch`): each one's .torrent
// (torrents/export), files and trackers, written as soon as its batch is in, so memory holds
// one batch whatever the library's size. A magnet without metadata (409 Missing metadata) goes
// in as its link; a torrent deleted while the export runs is left out and counted.
//
// The file is written to a temporary name beside the target and only takes its place when
// every torrent is in: a failure or `cancel()` leaves no half file behind. It is private
// (0600) from the start: announce URLs carry private trackers' passkeys (DECISIONS C9).

#include "core/TorrentsBackup.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QString>

#include <memory>

namespace omaseed::core {

class QbtClient;
class QbtEngine;

class TorrentsExport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBatch = 20;

    struct Outcome
    {
        bool ok = false;
        bool cancelled = false;
        QString error;
        QString path;
        int torrents = 0; ///< written, magnets included
        int magnets = 0;
        int gone = 0; ///< deleted on the server while the export ran
    };

    /// Talks to `engine`'s transport (docs/36 §4.4: a qBittorrent surface); null for none.
    explicit TorrentsExport(QbtEngine *engine, QObject *parent = nullptr);
    ~TorrentsExport() override;

    void start(const QString &path,
               const QString &serverName,
               const QString &serverUrl,
               const QString &appVersion);
    void cancel();
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    /// `done` of `total` torrents are in the file.
    void progress(int done, int total);
    void finished(const omaseed::core::TorrentsExport::Outcome &outcome);

private:
    struct Item
    {
        QJsonObject info;
        QByteArray torrent;
        QJsonArray files;
        QJsonArray trackers;
        bool gone = false;
        int waiting = 0;
    };

    void step();
    void nextBatch();
    void writeBatch();
    void fail(const QString &error);
    void finish(Outcome outcome);

    QPointer<QbtClient> m_client;
    TorrentsManifest m_manifest;
    QJsonArray m_infos;
    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<TarWriter> m_writer;
    QList<Item> m_batch;
    QString m_path;
    int m_step = 0;
    int m_next = 0;
    int m_waiting = 0;
    int m_generation = 0;
    Outcome m_outcome;
    bool m_busy = false;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::TorrentsExport::Outcome)
