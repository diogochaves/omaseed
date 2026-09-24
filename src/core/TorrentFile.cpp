#include "core/TorrentFile.hpp"

#include "core/Bencode.hpp"

#include <QCryptographicHash>
#include <QSet>
#include <QUrl>

namespace omaseed::core {

using bencode::Dict;
using bencode::Value;

namespace {

QString utf8(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes);
}

/// Walks a BEP-52 "file tree" dict, appending one TorrentFileEntry per leaf. A leaf is an
/// entry whose value is a dict containing the empty-string key (its properties, e.g.
/// "length"); anything else is a directory to recurse into.
void walkFileTree(const Value &node,
                  QStringList &path,
                  QList<TorrentFileEntry> &files,
                  qint64 &total)
{
    for (const auto &entry : node.toDict()) {
        const QString component = utf8(entry.first);
        const Value &child = entry.second;
        const Value leaf = child.value(QByteArray());
        path.append(component);
        if (leaf.isDict()) {
            TorrentFileEntry file;
            file.path = path.join(QLatin1Char('/'));
            file.size = leaf.value("length").toInteger();
            files.append(file);
            total += file.size;
        } else if (child.isDict()) {
            walkFileTree(child, path, files, total);
        }
        path.removeLast();
    }
}

void setError(QString *out, const QString &message)
{
    if (out)
        *out = message;
}

} // namespace

TorrentFile TorrentFile::parse(const QByteArray &data, QString *errorMessage)
{
    TorrentFile result;

    bencode::ParseError parseError;
    bencode::SpanMap topLevelSpans;
    const Value root = bencode::decode(data, &parseError, &topLevelSpans);
    if (parseError.hasError) {
        setError(errorMessage,
                 QStringLiteral("malformed torrent data at byte %1: %2")
                     .arg(parseError.offset)
                     .arg(parseError.message));
        return result;
    }
    if (!root.isDict()) {
        setError(errorMessage, QStringLiteral("top-level bencode value is not a dict"));
        return result;
    }

    const Value infoValue = root.value("info");
    if (!infoValue.isDict()) {
        setError(errorMessage, QStringLiteral("missing \"info\" dict"));
        return result;
    }
    const auto spanIt = topLevelSpans.constFind("info");
    if (spanIt == topLevelSpans.constEnd()) {
        setError(errorMessage, QStringLiteral("could not locate \"info\" bytes"));
        return result;
    }
    const QByteArray infoBytes = data.mid(spanIt->first, spanIt->second);

    result.infoHashV1 =
        QString::fromLatin1(QCryptographicHash::hash(infoBytes, QCryptographicHash::Sha1).toHex());

    const qint64 metaVersion = infoValue.value("meta version").toInteger(1);
    if (metaVersion == 2) {
        result.infoHashV2 = QString::fromLatin1(
            QCryptographicHash::hash(infoBytes, QCryptographicHash::Sha256).toHex());
    }

    result.name = utf8(infoValue.value("name").toString());
    result.pieceLength = infoValue.value("piece length").toInteger();
    result.isPrivate = infoValue.value("private").toInteger() != 0;

    const Value filesValue = infoValue.value("files");
    if (filesValue.isList()) {
        for (const Value &entry : filesValue.toList()) {
            // BEP-47 pad files ("attr" containing 'p') only align the next file to a piece
            // boundary. qBittorrent hides them and numbers the real files without them.
            if (entry.value("attr").toString().contains('p'))
                continue;
            QStringList pathParts;
            for (const Value &part : entry.value("path").toList())
                pathParts.append(utf8(part.toString()));
            TorrentFileEntry file;
            file.path = pathParts.join(QLatin1Char('/'));
            file.size = entry.value("length").toInteger();
            result.files.append(file);
            result.totalSize += file.size;
        }
    } else if (infoValue.value("length").isInteger()) {
        TorrentFileEntry file;
        file.path = result.name;
        file.size = infoValue.value("length").toInteger();
        result.files.append(file);
        result.totalSize = file.size;
    } else {
        const Value fileTree = infoValue.value("file tree");
        if (fileTree.isDict()) {
            QStringList path;
            walkFileTree(fileTree, path, result.files, result.totalSize);
        }
    }

    const QByteArray pieces = infoValue.value("pieces").toString();
    result.m_hasV1 = infoValue.value("pieces").isString();
    if (!pieces.isEmpty()) {
        result.pieceCount = pieces.size() / 20;
    } else if (result.pieceLength > 0) {
        result.pieceCount = (result.totalSize + result.pieceLength - 1) / result.pieceLength;
    }

    result.announce = utf8(root.value("announce").toString());
    for (const Value &tier : root.value("announce-list").toList()) {
        QStringList urls;
        for (const Value &url : tier.toList())
            urls.append(utf8(url.toString()));
        if (!urls.isEmpty())
            result.announceList.append(urls);
    }

    result.comment = utf8(root.value("comment").toString());
    result.createdBy = utf8(root.value("created by").toString());
    result.creationDate = root.value("creation date").toInteger();

    result.m_valid = true;
    setError(errorMessage, QString());
    return result;
}

QString TorrentFile::torrentIdFor(const QString &infoHashV1, const QString &infoHashV2)
{
    const QString v2 = infoHashV2.trimmed().toLower();
    return v2.isEmpty() ? infoHashV1.trimmed().toLower() : v2.left(40);
}

QString TorrentFile::torrentId() const
{
    return torrentIdFor(infoHashV1, infoHashV2);
}

QString TorrentFile::magnetLink() const
{
    // BEP-9 for v1, BEP-52 for v2: a v1 infohash only means something when the info dict
    // has v1 pieces; a v2 one is a SHA-256 multihash (0x12, 32 bytes). A hybrid carries both.
    QStringList topics;
    if (m_hasV1)
        topics.append(QStringLiteral("xt=urn:btih:") + infoHashV1);
    if (!infoHashV2.isEmpty())
        topics.append(QStringLiteral("xt=urn:btmh:1220") + infoHashV2);
    QString link = QStringLiteral("magnet:?") + topics.join(QLatin1Char('&'));
    if (!name.isEmpty())
        link += QStringLiteral("&dn=") + QString::fromUtf8(QUrl::toPercentEncoding(name));

    QSet<QString> seen;
    QStringList trackers;
    if (!announce.isEmpty()) {
        trackers.append(announce);
        seen.insert(announce);
    }
    for (const QStringList &tier : announceList) {
        for (const QString &url : tier) {
            if (!seen.contains(url)) {
                seen.insert(url);
                trackers.append(url);
            }
        }
    }
    for (const QString &url : trackers)
        link += QStringLiteral("&tr=") + QString::fromUtf8(QUrl::toPercentEncoding(url));

    return link;
}

} // namespace omaseed::core
