#include "core/CreateTrackers.hpp"

#include "core/Torrent.hpp"

#include <QSet>
#include <QUrl>

#include <algorithm>

namespace omaseed::core {

bool CreateTrackers::isAnnounceUrl(const QString &url)
{
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    return parsed.isValid() && !parsed.host().isEmpty() &&
           (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
            scheme == QLatin1String("udp") || scheme == QLatin1String("ws") ||
            scheme == QLatin1String("wss"));
}

CreateTrackers::CreateTrackers(const QList<Torrent> &torrents,
                               const QHash<QString, QStringList> &byUrl)
{
    QHash<QString, const Torrent *> byHash;
    for (const Torrent &torrent : torrents)
        byHash.insert(torrent.hash, &torrent);

    struct HostAcc
    {
        QSet<QString> hashes;
        QStringList urls;
    };
    QHash<QString, HostAcc> hosts;
    for (auto it = byUrl.cbegin(); it != byUrl.cend(); ++it) {
        if (!isAnnounceUrl(it.key()))
            continue;
        int publicCount = 0;
        for (const QString &hash : it.value()) {
            const Torrent *torrent = byHash.value(hash);
            if (torrent == nullptr)
                continue;
            if (torrent->isPrivate) {
                HostAcc &acc = hosts[trackerHost(it.key())];
                acc.hashes.insert(hash);
                if (!acc.urls.contains(it.key()))
                    acc.urls.append(it.key());
            } else {
                ++publicCount;
            }
        }
        if (publicCount > 0)
            m_public.append({it.key(), publicCount});
    }
    std::sort(m_public.begin(), m_public.end(), [](const auto &a, const auto &b) {
        return a.count != b.count ? a.count > b.count : a.url < b.url;
    });

    for (auto it = hosts.cbegin(); it != hosts.cend(); ++it) {
        PrivateHost host;
        host.host = it.key();
        host.count = int(it.value().hashes.size());
        const Torrent *newest = nullptr;
        for (const QString &hash : it.value().hashes) {
            const Torrent *torrent = byHash.value(hash);
            if (newest == nullptr || torrent->addedOn > newest->addedOn ||
                (torrent->addedOn == newest->addedOn && torrent->hash < newest->hash))
                newest = torrent;
        }
        host.newestHash = newest->hash;
        host.newestName = newest->name;
        host.newestAdded = newest->addedOn;
        for (const QString &url : it.value().urls) {
            if (byUrl.value(url).contains(newest->hash)) {
                if (host.announce.isEmpty() || url < host.announce)
                    host.announce = url;
            }
        }
        host.otherKeys = it.value().urls.size() > 1;
        m_private.append(host);
    }
    std::sort(m_private.begin(), m_private.end(), [](const auto &a, const auto &b) {
        return a.count != b.count ? a.count > b.count : a.host < b.host;
    });
}

QList<CreateTrackers::PublicTracker> CreateTrackers::publicTop(int count) const
{
    return m_public.mid(0, count);
}

QList<CreateTrackers::PrivateHost> CreateTrackers::privateHosts() const
{
    return m_private;
}

std::optional<CreateTrackers::PrivateHost> CreateTrackers::privateHost(const QString &host) const
{
    for (const PrivateHost &candidate : m_private)
        if (candidate.host == host)
            return candidate;
    return std::nullopt;
}

} // namespace omaseed::core
