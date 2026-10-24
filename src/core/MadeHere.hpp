#pragma once

// MadeHere — what omaseed remembers of a private torrent it made and saved for the upload
// (#146, DECISIONS C27 CR7–CR9, board 49 §4), so that when the site's copy comes back the add
// sheet points it at the folder it was made from and it seeds without downloading.
//
// One record per creation, per server: the name, both info hashes, the files (paths under the
// root and sizes), the folder it was made from, the tracker's host, a SHA-256 of the saved
// .torrent's bytes (to know the made file itself if it is added by mistake) and when. No
// passkey and no announce URL: the record is in state.json. Kept until the site's copy is
// added, 30 days have passed, or it is forgotten.
//
// match() is board 49 §4's table for one .torrent about to be added:
//   theMadeFile  its bytes are the saved file's: upload it first (a yellow line)
//   sameHash     its info hash is the record's (the site only changed the announce): seed mode
//   sameFiles    another hash, but every file matches by path and size: added there, checked

#include "core/TorrentFile.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace omaseed::core {

class MadeHere
{
public:
    struct Record
    {
        QString name;
        QString infoHashV1;
        QString infoHashV2;
        /// The torrent's files, root stripped, as TorrentFile lists them.
        QList<TorrentFileEntry> files;
        bool multiFile = false;
        /// The folder (or the one file) it was made from, as the server writes it.
        QString folder;
        QString host;
        /// Lowercase hex SHA-256 of the saved .torrent's bytes.
        QString digest;
        QDateTime madeAt;

        qint64 totalSize() const;
        QJsonObject toJson() const;
        static Record fromJson(const QJsonObject &object);
        /// A record for `file` made from `folder` for `host`, saved as `bytes`.
        static Record of(const TorrentFile &file,
                         const QByteArray &bytes,
                         const QString &folder,
                         const QString &host,
                         const QDateTime &madeAt);
    };

    enum class Kind { None, TheMadeFile, SameHash, SameFiles };
    struct Match
    {
        Kind kind = Kind::None;
        Record record;
        int index = -1;
    };

    /// How long a record is kept when nothing comes of it.
    static constexpr int kKeepDays = 30;

    QList<Record> records() const { return m_records; }
    bool isEmpty() const { return m_records.isEmpty(); }
    void add(const Record &record);
    /// Forgets the record at `index`.
    void forget(int index);
    /// Forgets every record with either hash of `file` or its files (the site's copy is in).
    void forgetMatching(const TorrentFile &file, const QString &digest);
    /// Drops the records older than kKeepDays at `now`; true when any went.
    bool dropExpired(const QDateTime &now);

    /// Board 49 §4 for `file`, whose bytes hash to `digest` ("" when unknown).
    Match match(const TorrentFile &file, const QString &digest) const;

    QJsonArray toJson() const;
    static MadeHere fromJson(const QJsonArray &array);
    /// Lowercase hex SHA-256 of `bytes`.
    static QString digestOf(const QByteArray &bytes);

private:
    QList<Record> m_records;
};

} // namespace omaseed::core
