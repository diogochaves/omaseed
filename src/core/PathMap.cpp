#include "core/PathMap.hpp"

#include <QJsonObject>
#include <QJsonValue>

namespace omaseed::core {

namespace {

QString normalize(const QString &path)
{
    QString result = path;
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

/// True when `prefix` matches `path` at a path-component boundary: either the whole path, or
/// followed immediately by a '/'.
bool matchesPrefix(const QString &path, const QString &prefix)
{
    if (prefix.isEmpty() || !path.startsWith(prefix))
        return false;
    if (path.size() == prefix.size())
        return true;
    if (prefix.endsWith(QLatin1Char('/')))
        return true;
    return path.at(prefix.size()) == QLatin1Char('/');
}

} // namespace

void PathMap::addMapping(const QString &remotePrefix, const QString &localPrefix)
{
    m_mappings.append({normalize(remotePrefix), normalize(localPrefix)});
}

QString PathMap::remap(const QString &path, bool remoteToLocal) const
{
    const QString normalizedPath = normalize(path);

    const Mapping *best = nullptr;
    qsizetype bestPrefixLength = -1;
    for (const Mapping &mapping : m_mappings) {
        const QString &prefix = remoteToLocal ? mapping.remote : mapping.local;
        if (matchesPrefix(normalizedPath, prefix) && prefix.size() > bestPrefixLength) {
            best = &mapping;
            bestPrefixLength = prefix.size();
        }
    }
    if (!best)
        return path;

    const QString &fromPrefix = remoteToLocal ? best->remote : best->local;
    const QString &toPrefix = remoteToLocal ? best->local : best->remote;
    // Join on exactly one '/': a "/" prefix on either side keeps its slash after
    // normalize(), so the suffix may or may not start with one.
    QString suffix = normalizedPath.mid(fromPrefix.size());
    if (suffix.isEmpty())
        return toPrefix;
    if (!suffix.startsWith(QLatin1Char('/')))
        suffix.prepend(QLatin1Char('/'));
    if (toPrefix.endsWith(QLatin1Char('/')))
        return toPrefix + suffix.mid(1);
    return toPrefix + suffix;
}

QString PathMap::toLocal(const QString &remotePath) const
{
    return remap(remotePath, true);
}

QString PathMap::toRemote(const QString &localPath) const
{
    return remap(localPath, false);
}

bool PathMap::covers(const QString &remotePath) const
{
    const QString normalizedPath = normalize(remotePath);
    for (const Mapping &mapping : m_mappings) {
        if (matchesPrefix(normalizedPath, mapping.remote))
            return true;
    }
    return false;
}

QJsonArray PathMap::toJson() const
{
    QJsonArray array;
    for (const Mapping &mapping : m_mappings) {
        QJsonObject obj;
        obj[QStringLiteral("remote")] = mapping.remote;
        obj[QStringLiteral("local")] = mapping.local;
        array.append(obj);
    }
    return array;
}

PathMap PathMap::fromJson(const QJsonArray &array)
{
    PathMap map;
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        map.addMapping(obj.value(QStringLiteral("remote")).toString(),
                       obj.value(QStringLiteral("local")).toString());
    }
    return map;
}

} // namespace omaseed::core
