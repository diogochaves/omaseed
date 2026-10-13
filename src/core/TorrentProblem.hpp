#pragma once

#include "core/Torrent.hpp"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace omaseed::core {

/// One warning qBittorrent wrote to its log about a torrent's files, taken apart (docs/28 §2,
/// #54). The API never says why a torrent is in error; this line is the only place it does:
///
///   File error alert. Torrent: "<name>". File: "<path>". Reason: "<prefix> file_open (<path>)
///   error: Permission denied"
///   Failed to restore torrent. … Torrent: "<name>". Reason: "<prefix> fast resume rejected.
///   file_stat(<path>): mismatching file size"
///
/// qBittorrent translates its part of the line, so nothing here reads its English words: the
/// quoted segments are what it fills in, and the last one is libtorrent's own text, which is
/// never translated. libtorrent 2.1 starts that text with the first 6 hex characters of the
/// torrent's info hash (v2 when it has one, otherwise v1); 2.0 puts the torrent's name there.
struct ProblemLine
{
    enum class Kind {
        Unknown,        ///< not a line about a torrent's files
        FileError,      ///< "File error alert": libtorrent could not open, read or write a file
        RestoreFailed,  ///< "Failed to restore torrent": fast resume rejected (missing files)
        TorrentErrored, ///< "Torrent errored" (English logs only): the error code's text alone
    };

    Kind kind = Kind::Unknown;
    QStringList quoted;     ///< every "…" segment, in order; the torrent's name is among them
    QString file;           ///< the full path libtorrent was using, when the line has one
    QString hashPrefix;     ///< 6 lowercase hex characters (libtorrent 2.1), or empty
    QString libtorrentName; ///< libtorrent 2.0's name in place of the prefix, or empty
    QString operation;      ///< file_open, file_write, file_stat…, or empty
    QString systemMessage;  ///< "Permission denied", "mismatching file size"…

    /// Takes one log message apart; a line that is not about a torrent's files gives Unknown.
    static ProblemLine parse(const QString &message);
};

/// What omaseed shows for one errored torrent (docs/28 §4–§5).
struct TorrentProblem
{
    bool found = false; ///< a log line was matched; false is S4, "didn't log a reason"
    bool missingFiles = false;
    QString headline; ///< "Can't write to its save folder"
    /// The cause, untranslated: noAccess, readOnly, diskFull, missing, diskError,
    /// nameTooLong, badName, missingFiles, other, notLogged.
    QString causeKey;
    QString cause;          ///< the table's short word: "no access", "disk full"…
    QString detail;         ///< "Permission denied, opening x.bin", in libtorrent's words
    QString file;           ///< the full path, or empty
    QString folder;         ///< the file's folder, or empty
    QDateTime loggedAt;     ///< the newest matching line
    int count = 0;          ///< matching lines with the same file and message
    QString line;           ///< the newest matching line, whole, for "copy the line"
    bool ambiguous = false; ///< S5: another torrent of the same name may own the line
    QStringList actions;    ///< in order: "location", "start", "recheck", "rename"

    QVariantMap toMap() const;
    bool operator==(const TorrentProblem &other) const = default;
};

/// Ties log lines to torrents and words them.
namespace ProblemMatch {

/// The torrents among `candidates` that `line` is about. The hash prefix decides first, then
/// the name, then the file's path under a save, download or content path. When the line
/// still fits more than one torrent, all of them come back and `ambiguous` is set (D6 a).
QStringList match(const ProblemLine &line, const QList<Torrent> &candidates, bool *ambiguous);

/// The plain words and actions for a matched line (docs/28 §5).
TorrentProblem describe(const ProblemLine &line);

/// What an errored torrent with no line shows (S4).
TorrentProblem notLogged(bool missingFiles);

/// Whether `path` is `root` or lies under it, as the server writes paths.
bool isUnder(const QString &path, const QString &root);

} // namespace ProblemMatch

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::TorrentProblem)
