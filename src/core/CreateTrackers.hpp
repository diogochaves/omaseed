#pragma once

// CreateTrackers — the trackers the create sheet offers, from the library alone (#146, DECISIONS
// C27 CR4, CR5): omaseed ships no tracker list and names no site.
//
//   public   the announce URLs the library's public torrents use most, by how many use each
//            (CR4's "top 2–3"); none is fine (DHT and peer exchange)
//   private  the hosts the library's private torrents announce to, each with how many torrents,
//            its newest torrent (by added time) and the announce URL that torrent uses for it,
//            which holds the passkey (CR5). Two passkeys for one host (a reset, two accounts):
//            the newest torrent's wins, and `otherKeys` says another URL for the host exists, so
//            the sheet can name which torrent's it took and when.
//
// The URLs come from sync/maindata's tracker map (TrackerModel::byUrl): every announce URL of
// every torrent, so nothing is asked of the server. Hosts are compared as the rail groups them
// (trackerHost), so a site's http and udp URLs are one tracker.

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

class CreateTrackers
{
public:
    /// What the lists need of one library torrent.
    struct Torrent
    {
        QString hash;
        QString name;
        bool isPrivate = false;
        QDateTime addedOn;
    };
    struct PublicTracker
    {
        QString url;
        int count = 0;
    };
    struct PrivateHost
    {
        QString host;
        int count = 0;
        QString newestHash;
        QString newestName;
        QDateTime newestAdded;
        /// The newest torrent's announce URL for this host: the passkey is in it. Never
        /// shown, logged or stored (C27 CR5); only sent to addTask.
        QString announce;
        /// Another announce URL for this host is in the library (two passkeys).
        bool otherKeys = false;
    };

    CreateTrackers(const QList<Torrent> &torrents, const QHash<QString, QStringList> &byUrl);

    /// The public announce URLs most used, at most `count`, most used first (then by URL).
    QList<PublicTracker> publicTop(int count = 3) const;
    /// The private hosts, the most torrents first (then by host).
    QList<PrivateHost> privateHosts() const;
    std::optional<PrivateHost> privateHost(const QString &host) const;

    /// A URL a torrent can announce to: http, https, udp, ws or wss with a host.
    static bool isAnnounceUrl(const QString &url);

private:
    QList<PublicTracker> m_public;
    QList<PrivateHost> m_private;
};

} // namespace omaseed::core
