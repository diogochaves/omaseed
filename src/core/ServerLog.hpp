#pragma once

#include "core/engines/LogEntry.hpp"

#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QPointer>
#include <QVariantMap>

#include <array>

namespace omaseed::core {

class EngineClient;
class TorrentModel;

/// qBittorrent's whole log, for the log face (docs/32 §6, D9–D15, #56): `log/main` at all four
/// levels, as a list model, oldest line first.
///
/// It reads only while `active` (the face is on screen and the window shows), once at once and
/// then after every applied poll (SyncEngine::synced), asking for the lines after the last id
/// it holds. Nothing reads while the face is away, and release() drops the lines when it
/// closes: they are kept only while the log is open.
///
/// qBittorrent keeps its log in memory, 20 000 lines at most, and numbers it from 0 again
/// when it restarts. So the model keeps as many (capacity()), dropping the oldest, and starts
/// over, as ProblemLog does, when a full update arrives (a restart always causes one) or when
/// the ids it gets go backwards. A log that holds line 0 starts where qBittorrent started
/// (fromStart, D15); the file it also writes on its own disk is never read.
class ServerLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    /// The log has been read since it last started over.
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY loadedChanged)
    Q_PROPERTY(int total READ total NOTIFY countsChanged)
    /// Lines per level: normal, info, warning, critical (the chips' counts, D10).
    Q_PROPERTY(QList<int> levelCounts READ levelCounts NOTIFY countsChanged)
    /// The oldest line held is qBittorrent's first since it started (id 0), D15.
    Q_PROPERTY(bool fromStart READ fromStart NOTIFY countsChanged)
    /// When the oldest line held was written: "13 Oct 19:52", "" with no line.
    Q_PROPERTY(QString firstAtText READ firstAtText NOTIFY countsChanged)

public:
    /// qBittorrent's `type`: one bit each, so a set of levels is a mask.
    enum Level { Normal = 1, Info = 2, Warning = 4, Critical = 8 };
    Q_ENUM(Level)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        LevelRole,     ///< Level
        LevelNameRole, ///< "normal", "info", "warning", "critical"
        TimestampRole, ///< seconds since the epoch
        TimeTextRole,  ///< "09:41:36" today, "13 Oct 19:52:44" another day
        MessageRole,
        MoveIdRole, ///< a move omaseed made (board 45 D7): its MoveHistory id, "" for qBittorrent's
    };

    struct Line
    {
        int id = 0;
        qint64 at = 0; ///< seconds since the epoch
        int level = Normal;
        QString message;
        QString moveId; ///< set on omaseed's own lines: a move it made (setMoveLines())
    };

    explicit ServerLog(QObject *parent = nullptr);

    /// The server to read. Not owned; changing it forgets every line.
    void setClient(EngineClient *client);
    /// The library, to tie a line to the torrent it names (torrentAt()). Not owned.
    void setTorrents(TorrentModel *torrents);

    bool isActive() const { return m_active; }
    void setActive(bool active);
    bool isLoaded() const { return m_loaded; }

    /// Board 45 D7 (#133): omaseed's moves, one line each, among qBittorrent's by their time.
    /// Replaces the ones given before; they stay through a start-over.
    void setMoveLines(QList<Line> lines);

    /// At most this many lines, the oldest dropped first: qBittorrent's own 20 000.
    void setCapacity(int lines);
    int capacity() const { return m_capacity; }

    /// What `log/main` returned, straight in, as a read that started at lastId(): the demo
    /// and the tests feed lines through this.
    Q_INVOKABLE void applyEntries(const QJsonArray &entries);
    /// The last id read; -1 before the first read, and after a start over.
    Q_INVOKABLE int lastId() const { return m_lastId; }

    int total() const { return int(m_lines.size()); }
    QList<int> levelCounts() const;
    /// Lines held at the levels in `mask`.
    Q_INVOKABLE int countAt(int mask) const;
    bool fromStart() const { return !m_lines.isEmpty() && m_lines.constFirst().id == 0; }
    QString firstAtText() const;
    const Line &lineAt(int row) const { return m_lines.at(row); }

    /// The torrent in the library that line `row` names (its name quoted, or libtorrent's hash
    /// prefix leading a quoted part), "" when none does. With two torrents of the same name, the
    /// first one the library lists.
    Q_INVOKABLE QString torrentAt(int row) const;
    /// What a line about `hash` carries: `{name, prefix}`, empty when it isn't in the library.
    Q_INVOKABLE QVariantMap torrentKey(const QString &hash) const;
    /// Whether `message` names the torrent called `name`, whose hash starts `prefix`: a quoted
    /// part is the name, or starts with the prefix (libtorrent 2.1) or the name (2.0) and a
    /// space.
    static bool names(const QString &message, const QString &name, const QString &prefix);

    /// The level's word, untranslated as qBittorrent's API names it.
    static QString levelName(int level);
    /// "09:41:36" on `now`'s day, "13 Oct 19:52:44" another day of the year, with the year
    /// another year.
    static QString timeText(qint64 seconds, const QDateTime &now = QDateTime::currentDateTime());
    /// D11: one line as `y` copies it, "2026-10-15 09:41:36 warning <message>".
    QString copyLine(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    /// One poll was applied: SyncEngine::synced. A full update starts over; while active the
    /// lines after lastId() are read (never when `mayRead` is false: a demo).
    void onSynced(bool fullUpdate, bool mayRead = true);
    /// The face closed: the lines go, and the next opening reads from the top.
    void release();

Q_SIGNALS:
    void activeChanged();
    void loadedChanged();
    void countsChanged();
    /// The log started over: a restart, another server, or release(). Marks and the cursor's
    /// line mean nothing now.
    void restarted();

private:
    void read();
    void startOver();
    void applyRead(const QList<LogEntry> &entries);

    QPointer<EngineClient> m_client;
    QPointer<TorrentModel> m_torrents;
    void mergeMoveLines();

    QList<Line> m_lines;
    QList<Line> m_moveLines; ///< omaseed's, oldest first
    std::array<int, 4> m_counts{};
    int m_capacity = 20000;
    int m_lastId = -1;
    bool m_active = false;
    bool m_loaded = false;
    bool m_reading = false;
    quint64 m_generation = 0; ///< start-over count: a read from before one is dropped
};

} // namespace omaseed::core
