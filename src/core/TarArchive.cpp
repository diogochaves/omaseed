#include "core/TarArchive.hpp"

#include <QBuffer>
#include <QIODevice>

#include <algorithm>
#include <cstring>

namespace omaseed::core {

namespace {

constexpr int kBlock = 512;

/// `value` as zero-padded octal in `width - 1` digits and a NUL, the ustar way.
void putOctal(char *field, int width, qint64 value)
{
    const QByteArray digits = QByteArray::number(value, 8).rightJustified(width - 1, '0');
    std::memcpy(field, digits.constData(), size_t(width - 1));
    field[width - 1] = '\0';
}

/// An octal field; -1 when it holds anything but octal digits, spaces and NULs.
qint64 getOctal(const char *field, int width)
{
    qint64 value = 0;
    bool any = false;
    for (int i = 0; i < width; ++i) {
        const char c = field[i];
        if (c == '\0' || c == ' ') {
            if (any)
                break;
            continue;
        }
        if (c < '0' || c > '7')
            return -1;
        any = true;
        value = value * 8 + (c - '0');
        if (value > (qint64(1) << 40))
            return -1; // a terabyte backup is not one of ours
    }
    return any ? value : 0;
}

qint64 checksum(const char *header)
{
    qint64 sum = 0;
    for (int i = 0; i < kBlock; ++i)
        sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(header[i]);
    return sum;
}

QString fieldString(const char *field, int width)
{
    return QString::fromUtf8(field, int(qstrnlen(field, size_t(width))));
}

} // namespace

bool TarArchive::isSafeName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('/')))
        return false;
    for (const QString &part : name.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e || c == QLatin1Char('\\'))
            return false;
    }
    return true;
}

namespace {

/// The 512-byte header for `name`, or empty with `error` set when it cannot be stored.
QByteArray headerFor(const QString &name, qint64 size, qint64 mtime, QString *error)
{
    if (!TarArchive::isSafeName(name)) {
        if (error)
            *error = QStringLiteral("cannot store the name %1").arg(name);
        return {};
    }
    const QByteArray full = name.toUtf8();
    QByteArray base = full;
    QByteArray prefix;
    if (base.size() > 100) {
        const int cut = full.lastIndexOf('/', 155);
        if (cut <= 0 || full.size() - cut - 1 > 100) {
            if (error)
                *error = QStringLiteral("the name %1 is too long").arg(name);
            return {};
        }
        prefix = full.left(cut);
        base = full.mid(cut + 1);
    }
    QByteArray block(kBlock, '\0');
    char *header = block.data();
    std::memcpy(header, base.constData(), size_t(base.size()));
    putOctal(header + 100, 8, 0644);
    putOctal(header + 108, 8, 0);
    putOctal(header + 116, 8, 0);
    putOctal(header + 124, 12, size);
    putOctal(header + 136, 12, std::max<qint64>(0, mtime));
    header[156] = '0';
    std::memcpy(header + 257, "ustar", 6);
    std::memcpy(header + 263, "00", 2);
    std::memcpy(header + 345, prefix.constData(), size_t(prefix.size()));
    // The checksum field: six octal digits, a NUL, a space.
    const QByteArray sum = QByteArray::number(checksum(header), 8).rightJustified(6, '0');
    std::memcpy(header + 148, sum.constData(), 6);
    header[154] = '\0';
    header[155] = ' ';
    return block;
}

qint64 padOf(qint64 size)
{
    return (kBlock - size % kBlock) % kBlock;
}

} // namespace

QByteArray TarArchive::write(const QList<Entry> &entries, QString *error)
{
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    TarWriter writer(&buffer);
    for (const Entry &entry : entries) {
        if (!writer.add(entry.name, entry.data, entry.mtime, error))
            return {};
    }
    if (!writer.finish(error))
        return {};
    buffer.close();
    return out;
}

std::optional<QList<TarArchive::Entry>> TarArchive::read(const QByteArray &archive, QString *error)
{
    QBuffer buffer;
    buffer.setData(archive);
    buffer.open(QIODevice::ReadOnly);
    TarReader reader(&buffer);
    QList<Entry> entries;
    while (const std::optional<TarReader::Header> header = reader.next())
        entries.append({header->name, archive.mid(header->offset, header->size), header->mtime});
    if (!reader.atEnd()) {
        if (error)
            *error = reader.error();
        return std::nullopt;
    }
    return entries;
}

TarWriter::TarWriter(QIODevice *device)
    : m_device(device)
{}

bool TarWriter::put(const QByteArray &bytes, QString *error)
{
    if (m_device->write(bytes) == bytes.size())
        return true;
    if (error)
        *error = m_device->errorString();
    return false;
}

bool TarWriter::add(const QString &name, const QByteArray &data, qint64 mtime, QString *error)
{
    const QByteArray header = headerFor(name, data.size(), mtime, error);
    return !header.isEmpty() && put(header, error) && put(data, error) &&
           put(QByteArray(padOf(data.size()), '\0'), error);
}

bool TarWriter::finish(QString *error)
{
    return put(QByteArray(2 * kBlock, '\0'), error);
}

TarReader::TarReader(QIODevice *device)
    : m_device(device)
{}

std::optional<TarReader::Header> TarReader::fail(const QString &why)
{
    m_error = why;
    return std::nullopt;
}

std::optional<TarReader::Header> TarReader::next()
{
    while (!m_end && m_error.isEmpty()) {
        const qint64 size = m_device->size();
        if (m_at + kBlock > size || !m_device->seek(m_at))
            return fail(QStringLiteral("the file ends before the archive does"));
        const QByteArray block = m_device->read(kBlock);
        if (block.size() != kBlock)
            return fail(QStringLiteral("the file ends before the archive does"));
        const char *header = block.constData();
        bool zero = true;
        for (int i = 0; i < kBlock && zero; ++i)
            zero = header[i] == '\0';
        if (zero) {
            m_end = true; // the end marker
            return std::nullopt;
        }
        if (std::memcmp(header + 257, "ustar", 5) != 0)
            return fail(QStringLiteral("not a tar archive"));
        if (getOctal(header + 148, 8) != checksum(header))
            return fail(QStringLiteral("a damaged header"));
        const qint64 length = getOctal(header + 124, 12);
        if (length < 0 || m_at + kBlock + length > size)
            return fail(QStringLiteral("an entry runs past the end of the file"));
        const char type = header[156];
        QString name = fieldString(header, 100);
        const QString prefix = fieldString(header + 345, 155);
        if (!prefix.isEmpty())
            name = prefix + QLatin1Char('/') + name;
        const qint64 at = m_at;
        m_at += kBlock + length + padOf(length);
        if (type == '0' || type == '\0') {
            if (!TarArchive::isSafeName(name))
                return fail(QStringLiteral("an entry with an unsafe name"));
            return Header{name, at + kBlock, length, getOctal(header + 136, 12)};
        }
        if (type != '5')
            return fail(QStringLiteral("an entry that is not a plain file"));
    }
    return std::nullopt;
}

QByteArray TarReader::read(const Header &header)
{
    QString why;
    QByteArray data = read(m_device, header, &why);
    if (!why.isEmpty())
        m_error = why;
    return data;
}

QByteArray TarReader::read(QIODevice *device, const Header &header, QString *error)
{
    if (!device->seek(header.offset)) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(header.name);
        return {};
    }
    QByteArray data = device->read(header.size);
    if (data.size() != header.size) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(header.name);
        return {};
    }
    return data;
}

} // namespace omaseed::core
