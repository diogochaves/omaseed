#pragma once

#include <QString>
#include <QStringList>

namespace omaseed::core {

/// A parsed `magnet:` URI (BEP-9 style). Parsing is pure string handling, no network I/O.
class MagnetLink
{
public:
    /// Parses a magnet URI. Anything that isn't a "magnet:" scheme, or that carries no
    /// recognisable info hash, yields an object with isValid() false.
    static MagnetLink parse(const QString &uri);
    /// A bare info hash, as the whole of `text` (surrounding whitespace aside): 40 hex or 32
    /// base32 for v1, 64 hex for v2, read as the `xt` of a magnet that has nothing else.
    /// Hex may be either case; base32 must be upper case, as clients print it, so a
    /// lower-case 32-character token (an MD5, a word) is not taken for one. Anything else,
    /// a hash inside other text included, yields an object with isValid() false.
    static MagnetLink fromInfoHash(const QString &text);

    bool isValid() const { return m_valid; }

    /// Lowercase hex SHA-1, normalised from either a 40-hex or 32-char base32 `xt=urn:btih:`.
    QString infoHashV1;
    /// Lowercase hex SHA-256, from an `xt=urn:btmh:1220<64-hex>` multihash topic.
    QString infoHashV2;

    /// `dn`, percent-decoded with '+' treated as a space.
    QString displayName;
    /// `tr` values, in the order they appeared.
    QStringList trackers;
    /// `ws` values, in the order they appeared.
    QStringList webSeeds;
    /// `xl`, or -1 when absent.
    qint64 exactLength = -1;
    /// `x.pe` values, in the order they appeared.
    QStringList peers;

    /// Canonical `magnet:?xt=...&dn=...&...` rendering of this link.
    QString toString() const;

private:
    bool m_valid = false;
};

} // namespace omaseed::core
