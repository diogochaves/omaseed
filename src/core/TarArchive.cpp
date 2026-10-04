#include "core/TarArchive.hpp"

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

QByteArray TarArchive::write(const QList<Entry> &entries, QString *error)
{
    QByteArray out;
    for (const Entry &entry : entries) {
        if (!isSafeName(entry.name)) {
            if (error)
                *error = QStringLiteral("cannot store the name %1").arg(entry.name);
            return {};
        }
        const QByteArray full = entry.name.toUtf8();
        QByteArray name = full;
        QByteArray prefix;
        if (name.size() > 100) {
            const int cut = full.lastIndexOf('/', 155);
            if (cut <= 0 || full.size() - cut - 1 > 100) {
                if (error)
                    *error = QStringLiteral("the name %1 is too long").arg(entry.name);
                return {};
            }
            prefix = full.left(cut);
            name = full.mid(cut + 1);
        }
        char header[kBlock];
        std::memset(header, 0, sizeof header);
        std::memcpy(header, name.constData(), size_t(name.size()));
        putOctal(header + 100, 8, 0644);
        putOctal(header + 108, 8, 0);
        putOctal(header + 116, 8, 0);
        putOctal(header + 124, 12, entry.data.size());
        putOctal(header + 136, 12, std::max<qint64>(0, entry.mtime));
        header[156] = '0';
        std::memcpy(header + 257, "ustar", 6);
        std::memcpy(header + 263, "00", 2);
        std::memcpy(header + 345, prefix.constData(), size_t(prefix.size()));
        // The checksum field: six octal digits, a NUL, a space.
        const QByteArray sum = QByteArray::number(checksum(header), 8).rightJustified(6, '0');
        std::memcpy(header + 148, sum.constData(), 6);
        header[154] = '\0';
        header[155] = ' ';
        out.append(header, kBlock);
        out.append(entry.data);
        const int pad = (kBlock - entry.data.size() % kBlock) % kBlock;
        out.append(QByteArray(pad, '\0'));
    }
    out.append(QByteArray(2 * kBlock, '\0'));
    return out;
}

std::optional<QList<TarArchive::Entry>> TarArchive::read(const QByteArray &archive, QString *error)
{
    const auto fail = [error](const QString &why) -> std::optional<QList<Entry>> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    QList<Entry> entries;
    qint64 at = 0;
    const qint64 size = archive.size();
    while (true) {
        if (at + kBlock > size)
            return fail(QStringLiteral("the file ends before the archive does"));
        const char *header = archive.constData() + at;
        bool zero = true;
        for (int i = 0; i < kBlock && zero; ++i)
            zero = header[i] == '\0';
        if (zero)
            break; // the end marker
        if (std::memcmp(header + 257, "ustar", 5) != 0)
            return fail(QStringLiteral("not a tar archive"));
        if (getOctal(header + 148, 8) != checksum(header))
            return fail(QStringLiteral("a damaged header"));
        const qint64 length = getOctal(header + 124, 12);
        if (length < 0 || at + kBlock + length > size)
            return fail(QStringLiteral("an entry runs past the end of the file"));
        const char type = header[156];
        QString name = fieldString(header, 100);
        const QString prefix = fieldString(header + 345, 155);
        if (!prefix.isEmpty())
            name = prefix + QLatin1Char('/') + name;
        if (type == '0' || type == '\0') {
            if (!isSafeName(name))
                return fail(QStringLiteral("an entry with an unsafe name"));
            Entry entry;
            entry.name = name;
            entry.data = archive.mid(at + kBlock, length);
            entry.mtime = getOctal(header + 136, 12);
            entries.append(entry);
        } else if (type != '5') {
            return fail(QStringLiteral("an entry that is not a plain file"));
        }
        at += kBlock + ((length + kBlock - 1) / kBlock) * kBlock;
    }
    return entries;
}

} // namespace omaseed::core
