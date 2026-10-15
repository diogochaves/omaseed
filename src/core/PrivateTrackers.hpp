#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace omaseed::core {

/// docs/29 D7 (F5, #44): the tracker hosts the library's private torrents announce to. A
/// magnet never says it is private, and qBittorrent can't fetch a private torrent's metadata
/// from one (libtorrent never hands it to a magnet), so a magnet whose `tr=` names one of these
/// hosts gets no lookup, only a line pointing to the site's .torrent. A private tracker the
/// library doesn't know goes through the lookup like a dead magnet.
///
/// Hosts are compared as Torrent's `trackerHost` reads them, so the http and udp announce URLs
/// of one site (another port, a passkey in the path) are the same tracker.
class PrivateTrackers
{
public:
    /// One library torrent: whether it carries the private flag (maindata `private`, 5.0+),
    /// and every tracker URL it announces to.
    void addTorrent(bool isPrivate, const QStringList &trackerUrls);

    /// The private hosts, sorted.
    QStringList hosts() const;

    /// The first of `trackerUrls` (a magnet's `tr=`) whose host is a private one, as that
    /// host; "" when none is.
    QString match(const QStringList &trackerUrls) const;

private:
    QSet<QString> m_hosts;
};

} // namespace omaseed::core
