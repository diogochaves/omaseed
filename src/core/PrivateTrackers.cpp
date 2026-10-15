#include "core/PrivateTrackers.hpp"

#include <QUrl>

#include <algorithm>

namespace omaseed::core {
namespace {

/// A tracker URL's host in lower case, or "" for anything that is not a tracker with a host
/// (qBittorrent's "** [DHT] **" pseudo-trackers, an empty or broken URL).
QString hostOf(const QString &trackerUrl)
{
    const QString url = trackerUrl.trimmed();
    if (url.isEmpty() || url.startsWith(QLatin1String("**")))
        return {};
    return QUrl(url).host().toLower();
}

} // namespace

void PrivateTrackers::addTorrent(bool isPrivate, const QStringList &trackerUrls)
{
    if (!isPrivate)
        return;
    for (const QString &url : trackerUrls) {
        const QString host = hostOf(url);
        if (!host.isEmpty())
            m_hosts.insert(host);
    }
}

QStringList PrivateTrackers::hosts() const
{
    QStringList hosts(m_hosts.cbegin(), m_hosts.cend());
    std::sort(hosts.begin(), hosts.end());
    return hosts;
}

QString PrivateTrackers::match(const QStringList &trackerUrls) const
{
    for (const QString &url : trackerUrls) {
        const QString host = hostOf(url);
        if (!host.isEmpty() && m_hosts.contains(host))
            return host;
    }
    return {};
}

} // namespace omaseed::core
