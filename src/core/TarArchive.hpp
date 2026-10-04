#pragma once

// TarArchive — plain POSIX ustar, written and read in memory (docs/24 D2 a).
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

} // namespace omaseed::core
