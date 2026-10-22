#pragma once

// MetadataLookup — the file list of one magnet (or of a link to a .torrent) asked of
// qBittorrent before it is added, so the add sheet can show the tree a .torrent shows
// (docs/29, #44). qBittorrent 5.2's `torrents/fetchMetadata` looks the metadata up without
// adding anything; this object asks, asks again on the board's schedule, and says what the
// sheet's Files row shows meanwhile.
//
// Phases (docs/29 §3, D1-D3, D11):
//   idle         nothing asked
//   private      not asked: the magnet's trackers are the library's private ones (D7, F5)
//   waiting      a source, but the server's login and version are not known yet (F7)
//   quiet        asked, under 0.5 s: nothing shows, so a quick answer just shows the tree
//   fetching     0.5 s to 10 s (F1)
//   slow         10 s to 60 s, "still looking" (F3)
//   never        60 s on, "may never come" (F4); polls slow to every 5 s and never stop
//   done         the metadata is in (F2): metadata()
//   unsupported  the server cannot (EngineFeatures::fetchMetadata: qBittorrent below Web API
//                2.11.9), or answered 404: today's sheet (T2)
//   failed       the server refused the source outright (a 4xx other than 404): today's sheet
//
// The clock starts once the server is ready (F7). The polls go at 0, 0.25, 0.5 and 1 s, then
// every second, and every 5 s after a minute, one at a time; a lost poll is just asked again.
// Nothing here stops the lookup on the server: qBittorrent has no way to (D6). Every poll
// must go through the same engine, whose session keeps the result: `setFetch` is that
// engine's fetchMetadata.

#include "core/Reply.hpp"
#include "core/TorrentFile.hpp"
#include "core/engines/TorrentMetadata.hpp"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QTimer;

namespace omaseed::core {

class MetadataLookup : public QObject
{
    Q_OBJECT
    /// One of the phases above, by name.
    Q_PROPERTY(QString phase READ phaseName NOTIFY changed)
    /// Whole seconds since the lookup started (0 before it has).
    Q_PROPERTY(int seconds READ seconds NOTIFY changed)
    /// "magnet" or "url": the sheet words a link's wait differently (F8).
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    /// What is being looked up (the magnet, the link), or "".
    Q_PROPERTY(QString source READ source NOTIFY changed)
    /// For "private": the tracker host the magnet shares with the library's private torrents.
    Q_PROPERTY(QString privateHost READ privateHost NOTIFY changed)

public:
    enum class Phase {
        Idle,
        Private,
        Waiting,
        Quiet,
        Fetching,
        Slow,
        Never,
        Done,
        Unsupported,
        Failed
    };
    Q_ENUM(Phase)

    /// What the 200 says, ready for the add sheet (TorrentMetadata).
    using Metadata = TorrentMetadata;

    using Fetch = std::function<Reply<FetchedMetadata> *(const QString &source)>;
    using Clock = std::function<qint64()>;

    explicit MetadataLookup(QObject *parent = nullptr);

    /// Where the polls go: the current server's EngineClient::fetchMetadata.
    void setFetch(Fetch fetch);
    /// Test seam: the time in ms comes from `clock`, and no timer is armed; the test calls
    /// wake() after moving its clock.
    void setManualClock(Clock clock);
    /// The walk's test-only clock (OMASEED_LOOKUP_CLOCK_SCALE): time runs this many times
    /// faster, so F3 and F4 come in seconds. 1 everywhere else.
    void setClockScale(double scale);

    /// Starts on `source` ("magnet" or "url" `kind`). `key` names it (a magnet's torrent id,
    /// a link's URL): asking again for the key already looked up changes nothing, so typing
    /// `&dn=…` after a full hash starts nothing new. Waits until setServer() says the server
    /// is ready.
    void start(const QString &key, const QString &source, const QString &kind);
    /// F5: the magnet's trackers are private ones; nothing is asked of the server.
    void startPrivate(const QString &key, const QString &source, const QString &host);
    /// The server's state: `ready` once it is signed in and has said its version, and whether
    /// it can look metadata up (EngineFeatures::fetchMetadata). A waiting lookup starts then,
    /// or turns "unsupported".
    void setServer(bool ready, bool canFetch);
    /// The sheet let go of it (closed, another source, a batch). The server goes on looking
    /// (D6 a); only the polling stops.
    void stop();
    /// Reads the clock: moves the phase and the seconds on and sends a poll when one is due.
    void wake();

    Phase phase() const { return m_phase; }
    QString phaseName() const;
    int seconds() const;
    QString key() const { return m_key; }
    QString kind() const { return m_kind; }
    QString source() const { return m_source; }
    QString privateHost() const { return m_privateHost; }
    /// Valid once the phase is Done.
    const Metadata &metadata() const { return m_metadata; }
    /// True while it is asking the server, or about to: waiting, quiet, fetching, slow, never.
    bool isActive() const;

    /// docs/29 §3: the poll after one at `elapsedMs`: 0.25, 0.5 and 1 s, then each whole
    /// second, then every 5 s from a minute on.
    static qint64 nextPollAt(qint64 elapsedMs);
    /// D2/D3: quiet under 0.5 s, fetching to 10 s, slow to 60 s, never after.
    static Phase phaseAt(qint64 elapsedMs);

Q_SIGNALS:
    void changed();
    /// The phase reached Done: metadata() holds the list.
    void fetched();

private:
    qint64 now() const;
    qint64 elapsed() const;
    void begin();
    void poll();
    void onAnswer(int generation, const Result<FetchedMetadata> &result);
    void setPhase(Phase phase);
    void arm();

    Fetch m_fetch;
    Clock m_manualClock;
    double m_scale = 1.0;
    QElapsedTimer m_monotonic;
    QTimer *m_timer = nullptr;

    Phase m_phase = Phase::Idle;
    QString m_key;
    QString m_source;
    QString m_kind;
    QString m_privateHost;
    bool m_serverReady = false;
    bool m_canFetch = false;
    qint64 m_startedAt = 0;
    qint64 m_nextPoll = 0;
    int m_lastSeconds = 0;
    bool m_inFlight = false;
    /// Moves on every start/stop, so an answer to an earlier lookup is dropped.
    int m_generation = 0;
    Metadata m_metadata;
};

} // namespace omaseed::core
