#pragma once

// LooseCarry — carries the files no torrent owns to the folder's new place, after the torrents
// have landed (board 42 §7, DECISIONS C15 M17, #98). qBittorrent has no API for files it
// doesn't own, so omaseed does this itself, on this machine, through the path map.
//
// For each file of the preflight's dry run (LooseFiles), in order:
//
// - same drive: a rename that never replaces (renameat2 RENAME_NOREPLACE).
// - another drive: copy to a temporary name beside the target, hashing as it reads; flush it
//   to disk, read the copy back and compare; give it the source's time and mode; move it into
//   place without replacing anything; only then remove the source.
// - already there: the same size and hash at the target counts as carried, and the source
//   goes. A different file: both stay, and it is listed. Nothing at the target is replaced.
// - changed since the dry run (size or time), or changed in the last 15 min: it stays.
// - a full disk: the partial copy is removed, the source kept, and the carry stops; every
//   file not yet carried stays, "the destination is full".
// - stopped (pause, quit): the partial copy is removed and the file is still to do. Running
//   again picks up where it left off: a source that is gone with its target in place was
//   carried by the earlier run.
//
// Folders left empty at the source by the carry are removed, only when empty. Nothing is ever
// deleted that has no checked copy at the target.

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>

#include <chrono>
#include <functional>

namespace omaseed::core {

struct CarryFile
{
    enum class Outcome {
        ToDo,
        Carried,      ///< at the target, source removed
        AlreadyThere, ///< the same bytes were at the target; the source removed
        Different,    ///< a different file at the target: both stay
        Changed,      ///< changed since the dry run, or too recently: stays
        Recent,       ///< the dry run found it too recent (never tried)
        Unknown,      ///< the dry run couldn't read it (never tried)
        Gone,         ///< neither here nor there any more
        NoRoom,       ///< the destination filled up
        Failed,       ///< anything else; `why` says what
        Kept,         ///< copied and checked, but the source couldn't be removed
    };

    QString relative; ///< below the folder, the same at both ends
    qint64 size = 0;
    QDateTime modified; ///< as the dry run saw it
    Outcome outcome = Outcome::ToDo;
    QString why;

    bool carried() const { return outcome == Outcome::Carried || outcome == Outcome::AlreadyThere; }
    bool operator==(const CarryFile &other) const = default;
};

class LooseCarry
{
public:
    struct Progress
    {
        int done = 0; ///< files settled, carried or not
        int total = 0;
        qint64 bytesDone = 0;
        qint64 bytesTotal = 0;
        QString current;       ///< relative path of the file being carried
        bool checking = false; ///< reading the copy back
    };

    struct Result
    {
        QList<CarryFile> files;
        int carried = 0; ///< Carried + AlreadyThere
        int stayed = 0;  ///< every other settled file
        qint64 bytesCarried = 0;
        int foldersRemoved = 0;
        bool full = false;    ///< stopped on a full destination
        bool stopped = false; ///< `stop` said so; some files are still ToDo
    };

    /// The suffix of the temporary copy beside a target: ".NAME.omaseed-carry".
    static QString tempPathFor(const QString &target);

    /// Carries every ToDo file of `files` from the folder `from` to the folder `to` (both on
    /// this machine). Files already settled are kept as they are. `now` and `minAge` decide
    /// what is too recent. `stop`, asked between files and between reads, ends early;
    /// `progress` hears of each file and of each 64 MiB copied. Blocking: run it off the main
    /// thread.
    static Result run(const QString &from,
                      const QString &to,
                      QList<CarryFile> files,
                      const QDateTime &now,
                      std::chrono::seconds minAge = std::chrono::minutes(15),
                      const std::function<bool()> &stop = {},
                      const std::function<void(const Progress &)> &progress = {});

    /// Counts a list as run() does (for a list read back from disk).
    static Result tally(const QList<CarryFile> &files);

    static QJsonArray toJson(const QList<CarryFile> &files);
    static QList<CarryFile> fromJson(const QJsonArray &array);
};

} // namespace omaseed::core
