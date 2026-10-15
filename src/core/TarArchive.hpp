#pragma once

// TarArchive — plain POSIX ustar, written and read in memory (docs/24 D2 a), or streamed
// through a QIODevice with TarWriter and TarReader (a torrents export can hold thousands of
// .torrent files, #33).
//
// omaseed's backups are tar files so any tool can open one and see what is inside. Only what
// they need is supported: regular files whose names fit the 100-byte name field (an optional
// directory part goes in the 155-byte prefix), no links, no long-name extensions. Reading is
// strict, because a backup is about to replace files: a header whose checksum is wrong, a
// size that runs past the end, a name that climbs out (`..`) or starts at `/`, or a missing
// end marker refuses the whole archive.

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

class QIODevice;

namespace omaseed::core {

class TarArchive
{
public:
    struct Entry
    {
        QString name; ///< relative, '/'-separated, e.g. "ledger/abc.sqlite"
        QByteArray data;
        qint64 mtime = 0; ///< unix seconds
    };

    /// The archive for `entries`, in order. Empty, with `error` set, when a name cannot be
    /// stored (too long, absolute, climbing out, not ASCII-safe).
    static QByteArray write(const QList<Entry> &entries, QString *error = nullptr);
    /// Every entry of `archive`, or nothing with `error` saying why.
    static std::optional<QList<Entry>> read(const QByteArray &archive, QString *error = nullptr);

    /// True for a name `write()` accepts.
    static bool isSafeName(const QString &name);
};

/// Writes an archive entry by entry to a device, so nothing but the entry at hand is held in
/// memory. `finish()` writes the end marker; an archive without one is refused by every reader.
class TarWriter
{
public:
    explicit TarWriter(QIODevice *device);

    /// Appends one file. False, with `error` set, for a name TarArchive cannot store or a
    /// device that will not take the bytes.
    bool add(const QString &name, const QByteArray &data, qint64 mtime, QString *error = nullptr);
    /// The end marker. Nothing may be added after it.
    bool finish(QString *error = nullptr);

private:
    bool put(const QByteArray &bytes, QString *error);

    QIODevice *m_device = nullptr;
};

/// Reads an archive's headers one by one from a seekable device and fetches an entry's data
/// only when asked, by where it lies. As strict as TarArchive::read: the first damaged header,
/// unsafe name, entry running past the end or missing end marker ends the walk with error().
class TarReader
{
public:
    struct Header
    {
        QString name;
        qint64 offset = 0; ///< where the data starts in the device
        qint64 size = 0;
        qint64 mtime = 0;
    };

    explicit TarReader(QIODevice *device);

    /// The next file entry, or nothing at the end marker (atEnd()) or on a fault (error()).
    std::optional<Header> next();
    bool atEnd() const { return m_end; }
    QString error() const { return m_error; }

    /// The data of an entry next() returned.
    QByteArray read(const Header &header);
    /// The same through any device holding the archive (another handle on the same file).
    static QByteArray read(QIODevice *device, const Header &header, QString *error = nullptr);

private:
    std::optional<Header> fail(const QString &why);

    QIODevice *m_device = nullptr;
    qint64 m_at = 0;
    bool m_end = false;
    QString m_error;
};

} // namespace omaseed::core
