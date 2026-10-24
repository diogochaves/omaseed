#include "core/MadeHere.hpp"

#include <QCryptographicHash>

namespace omaseed::core {

namespace {

bool sameFiles(const QList<TorrentFileEntry> &a, const QList<TorrentFileEntry> &b)
{
    if (a.size() != b.size() || a.isEmpty())
        return false;
    QHash<QString, qint64> sizes;
    for (const TorrentFileEntry &entry : a)
        sizes.insert(entry.path, entry.size);
    for (const TorrentFileEntry &entry : b) {
        const auto it = sizes.constFind(entry.path);
        if (it == sizes.cend() || it.value() != entry.size)
            return false;
    }
    return true;
}

bool sameHash(const MadeHere::Record &record, const TorrentFile &file)
{
    return (!record.infoHashV1.isEmpty() && record.infoHashV1 == file.infoHashV1) ||
           (!record.infoHashV2.isEmpty() && record.infoHashV2 == file.infoHashV2);
}

} // namespace

qint64 MadeHere::Record::totalSize() const
{
    qint64 total = 0;
    for (const TorrentFileEntry &entry : files)
        total += entry.size;
    return total;
}

QJsonObject MadeHere::Record::toJson() const
{
    QJsonArray list;
    for (const TorrentFileEntry &entry : files)
        list.append(QJsonObject{{QStringLiteral("path"), entry.path},
                                {QStringLiteral("size"), double(entry.size)}});
    return {{QStringLiteral("name"), name},
            {QStringLiteral("v1"), infoHashV1},
            {QStringLiteral("v2"), infoHashV2},
            {QStringLiteral("files"), list},
            {QStringLiteral("multiFile"), multiFile},
            {QStringLiteral("folder"), folder},
            {QStringLiteral("host"), host},
            {QStringLiteral("source"), source},
            {QStringLiteral("digest"), digest},
            {QStringLiteral("madeAt"), madeAt.toUTC().toString(Qt::ISODate)}};
}

MadeHere::Record MadeHere::Record::fromJson(const QJsonObject &object)
{
    Record record;
    record.name = object.value(QStringLiteral("name")).toString();
    record.infoHashV1 = object.value(QStringLiteral("v1")).toString();
    record.infoHashV2 = object.value(QStringLiteral("v2")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("files")).toArray()) {
        const QJsonObject entry = value.toObject();
        record.files.append({entry.value(QStringLiteral("path")).toString(),
                             qint64(entry.value(QStringLiteral("size")).toDouble())});
    }
    record.multiFile = object.value(QStringLiteral("multiFile")).toBool();
    record.folder = object.value(QStringLiteral("folder")).toString();
    record.host = object.value(QStringLiteral("host")).toString();
    record.source = object.value(QStringLiteral("source")).toString();
    record.digest = object.value(QStringLiteral("digest")).toString();
    record.madeAt =
        QDateTime::fromString(object.value(QStringLiteral("madeAt")).toString(), Qt::ISODate);
    return record;
}

MadeHere::Record MadeHere::Record::of(const TorrentFile &file,
                                      const QByteArray &bytes,
                                      const QString &folder,
                                      const QString &host,
                                      const QDateTime &madeAt)
{
    Record record;
    record.name = file.name;
    record.infoHashV1 = file.infoHashV1;
    record.infoHashV2 = file.infoHashV2;
    record.files = file.files;
    record.multiFile = file.multiFile;
    record.folder = folder;
    record.host = host;
    record.source = file.source;
    record.digest = digestOf(bytes);
    record.madeAt = madeAt;
    return record;
}

void MadeHere::add(const Record &record)
{
    // Made again from the same folder for the same tracker: the newer record stands.
    for (qsizetype i = m_records.size() - 1; i >= 0; --i) {
        const Record &old = m_records.at(i);
        if (old.folder == record.folder && old.host == record.host)
            m_records.removeAt(i);
    }
    m_records.append(record);
}

void MadeHere::forget(int index)
{
    if (index >= 0 && index < m_records.size())
        m_records.removeAt(index);
}

void MadeHere::forgetMatching(const TorrentFile &file, const QString &digest)
{
    for (qsizetype i = m_records.size() - 1; i >= 0; --i) {
        const Record &record = m_records.at(i);
        if (sameHash(record, file) || sameFiles(record.files, file.files) ||
            (!digest.isEmpty() && record.digest == digest))
            m_records.removeAt(i);
    }
}

bool MadeHere::dropExpired(const QDateTime &now)
{
    const qsizetype before = m_records.size();
    m_records.removeIf([&now](const Record &record) {
        return !record.madeAt.isValid() || record.madeAt.daysTo(now) > kKeepDays;
    });
    return m_records.size() != before;
}

MadeHere::Match MadeHere::match(const TorrentFile &file, const QString &digest) const
{
    Match found;
    // Newest first: a folder made twice answers with the last one.
    for (qsizetype i = m_records.size() - 1; i >= 0; --i) {
        const Record &record = m_records.at(i);
        Kind kind = Kind::None;
        if (!digest.isEmpty() && record.digest == digest)
            kind = Kind::TheMadeFile;
        else if (sameHash(record, file))
            kind = Kind::SameHash;
        else if (sameFiles(record.files, file.files))
            kind = Kind::SameFiles;
        if (kind == Kind::None)
            continue;
        // The made file outranks the same hash, which outranks the same files.
        if (found.kind == Kind::None || int(kind) < int(found.kind)) {
            found.kind = kind;
            found.record = record;
            found.index = int(i);
        }
    }
    return found;
}

QJsonArray MadeHere::toJson() const
{
    QJsonArray array;
    for (const Record &record : m_records)
        array.append(record.toJson());
    return array;
}

MadeHere MadeHere::fromJson(const QJsonArray &array)
{
    MadeHere made;
    for (const QJsonValue &value : array)
        made.m_records.append(Record::fromJson(value.toObject()));
    return made;
}

QString MadeHere::digestOf(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

} // namespace omaseed::core
