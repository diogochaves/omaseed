#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>

namespace omaseed::core {

/// Maps between the path a qBittorrent server sees ("remote", e.g. inside a Docker container
/// or over SSH) and the path this machine sees for the same file ("local"). Used to open a
/// torrent's folder or files from the UI when the server isn't running on this filesystem.
class PathMap
{
public:
    struct Mapping
    {
        QString remote;
        QString local;

        bool operator==(const Mapping &other) const = default;
    };

    /// Adds a mapping. Prefixes are normalised (backslashes to '/', trailing '/' trimmed)
    /// before storing, so lookups and JSON round-trips see a consistent form.
    void addMapping(const QString &remotePrefix, const QString &localPrefix);
    void clear() { m_mappings.clear(); }
    QList<Mapping> mappings() const { return m_mappings; }
    qsizetype count() const { return m_mappings.size(); }

    /// Rewrites a remote path to its local equivalent using the longest matching remote
    /// prefix, matched at a path-component boundary ("/data" does not match "/database").
    /// Returns the input unchanged when no mapping applies.
    QString toLocal(const QString &remotePath) const;
    /// The inverse of toLocal(): rewrites a local path to its remote equivalent.
    QString toRemote(const QString &localPath) const;

    QJsonArray toJson() const;
    static PathMap fromJson(const QJsonArray &array);

private:
    QString remap(const QString &path, bool remoteToLocal) const;

    QList<Mapping> m_mappings;
};

} // namespace omaseed::core
