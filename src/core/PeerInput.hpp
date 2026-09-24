#pragma once

// PeerInput — what the details pane's "add tracker" and "add peer" sheets accept (review
// VIEW-6, ruling Q4). Pure string handling: the server has the final word, this only keeps
// obvious typos from becoming a silent no-op (qBittorrent answers 200 to an unusable tracker
// URL and drops a malformed peer without saying so).
//
// Both take free text, one entry per line or several on a line, since that is how lists get
// pasted: tracker URLs are separated by white space (a URL cannot hold one, but a passkey
// query may hold a comma), peers by white space or commas. Blank entries are ignored.

#include <QString>
#include <QStringList>

namespace omaseed::core {

struct PeerInputResult
{
    /// The accepted entries, trimmed, in the order typed, without duplicates.
    QStringList entries;
    /// The first entry that was refused, as typed; empty when every entry passed.
    QString invalid;

    /// True when there is something to send and nothing was refused.
    bool isValid() const { return invalid.isEmpty() && !entries.isEmpty(); }
};

namespace PeerInput {

/// Tracker announce URLs: each must have a scheme (http, https, udp, wss or ws) and a host.
PeerInputResult trackerUrls(const QString &text);

/// Peers as `host:port`: an IPv4 address or a host name before the colon, an IPv6 address
/// in brackets (`[2001:db8::1]:6881`), and a port 1–65535 after it.
PeerInputResult peers(const QString &text);

/// One peer entry (the rule peers() applies to each): true for `1.2.3.4:6881`,
/// `[::1]:6881`, `seed.example:51413`; false for `1.2.3.4`, `::1:6881`, `host:0`.
bool isPeerAddress(const QString &entry);

} // namespace PeerInput

} // namespace omaseed::core
