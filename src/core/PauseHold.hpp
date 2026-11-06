#pragma once

// PauseHold — Pause Torrents and Resume Torrents (DECISIONS C36 M10, amending C13 D24 a and
// C20's stop all and start all): the torrents a pause stopped, held per server until a resume
// starts exactly those again.
//
// Start all used to send qBittorrent `hashes=all`, so it also started torrents kept stopped on
// purpose. Pause asks the server which torrents run (EngineClient::running(), qBittorrent's
// `filter=running`: anything not stopped, queued, stalled, checking and errored ones included),
// writes them into the store before the stop is sent (a crash can't lose them), and stops exactly
// those (never `all`, so a torrent added meanwhile isn't caught). A force-started one is held as
// such and comes back forced. A second pause while one is held adds what runs now. A stop the
// server refused takes the new ones back out; one that may have landed (a network error, a timeout,
// a switch cancelling it) keeps them: a resume's start is a no-op for a torrent already running.
//
// Resume asks which torrents are stopped (EngineClient::stoppedAmong(), `filter=stopped`) and
// starts the held ones among them, then forgets the server's record. A held torrent removed
// meanwhile, or started by hand, is skipped, never sent. A resume that fails keeps the record.
//
// Per server: the store keeps `{ "<server id>": { "hashes": [...], "forced": [...] } }`; App
// writes it to state.json, so a hold outlives a restart. A reply is booked to the server the
// call was made on, whatever server omaseed is on when it comes.

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

namespace omaseed::core {

class ApiError;
class EngineClient;
struct RunningTorrent;

class PauseHold : public QObject
{
    Q_OBJECT
    /// A pause is held on the current server: the menu offers Resume Torrents.
    Q_PROPERTY(bool held READ held NOTIFY changed)
    /// How many torrents it holds there.
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// A pause or a resume is on its way; another is refused until it ends.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    explicit PauseHold(QObject *parent = nullptr);

    void setClient(EngineClient *client);
    /// The server omaseed is on ("" for none).
    void setServer(const QString &id);
    QString server() const { return m_server; }

    /// Every server's held torrents, as state.json keeps them.
    QJsonObject store() const { return m_store; }
    void setStore(const QJsonObject &store);

    bool held() const { return count() > 0; }
    int count() const;
    bool busy() const { return m_busy; }
    /// The current server's held hashes, sorted.
    QStringList hashes() const;
    /// Those of them held force-started, sorted.
    QStringList forced() const;

    /// Stops what runs on the current server and holds it. False when it can't be sent: no
    /// client or server, or one already on its way.
    Q_INVOKABLE bool pause();
    /// Starts what the current server holds and forgets it. False with nothing held, or as
    /// pause.
    Q_INVOKABLE bool resume();

Q_SIGNALS:
    void changed();
    /// `store()` moved: App writes it to state.json at once.
    void storeChanged();
    /// A pause ended: `count` torrents stopped; `error` empty when it went through.
    void paused(int count, const QString &error);
    /// A resume ended: `count` started, `skipped` held but gone or already running.
    void resumed(int count, int skipped, const QString &error);

private:
    struct Entry
    {
        QSet<QString> hashes;
        QSet<QString> forced;
    };

    Entry entry(const QString &server) const;
    void setEntry(const QString &server, const Entry &entry);
    void finish();
    void pauseListed(const QString &server, const QList<RunningTorrent> &running);
    void resumeListed(const QString &server, const QStringList &stopped);
    void startForced(const QString &server, const QStringList &forced, int started, int skipped);
    void resumeFailed(const ApiError &error);

    QPointer<EngineClient> m_client;
    QJsonObject m_store;
    QString m_server;
    bool m_busy = false;
};

} // namespace omaseed::core
