#pragma once

#include "core/ServerLog.hpp"

#include <QPointer>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>

namespace omaseed::core {

/// The lines the log face shows (docs/32 §6, D10, D11, D13): ServerLog's, at the levels that
/// are on (the chips, `1`–`4`), with every word of the filter (`/`, case ignored) and, from a
/// torrent, only the lines that name it (`t`, "its lines in the log"). It also keeps the
/// lines marked with `space`, `v` and `ctrl+a`, by id, and words what `y` copies.
class LogFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(omaseed::core::ServerLog *log READ log WRITE setLog NOTIFY logChanged)
    /// ServerLog::Level bits; all four by default.
    Q_PROPERTY(int levels READ levels WRITE setLevels NOTIFY levelsChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    /// The torrent the lines are narrowed to, "" for none; its name for the chip.
    Q_PROPERTY(QString torrentHash READ torrentHash NOTIFY torrentChanged)
    Q_PROPERTY(QString torrentName READ torrentName NOTIFY torrentChanged)
    /// Lines shown.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Lines at the levels that are on: the M of "N of M lines".
    Q_PROPERTY(int levelTotal READ levelTotal NOTIFY countChanged)
    /// Words or a torrent narrow the lines further than the levels.
    Q_PROPERTY(bool narrowed READ isNarrowed NOTIFY narrowedChanged)
    Q_PROPERTY(int markedCount READ markedCount NOTIFY marksChanged)

public:
    enum Roles { MarkedRole = Qt::UserRole + 100 };

    explicit LogFilterModel(QObject *parent = nullptr);

    ServerLog *log() const { return m_log; }
    void setLog(ServerLog *log);

    int levels() const { return m_levels; }
    void setLevels(int levels);
    /// `1`–`4`: a level on or off.
    Q_INVOKABLE void toggleLevel(int level);

    QString text() const { return m_text; }
    void setText(const QString &text);
    /// The filter's words, lowercased: each must be in the line's message.
    static QStringList words(const QString &text);
    static bool matchesWords(const QString &message, const QStringList &words);

    QString torrentHash() const { return m_torrentHash; }
    QString torrentName() const { return m_torrentName; }
    /// D13: only the lines that name `hash` (ServerLog::names()). A hash that isn't in the
    /// library changes nothing and answers false.
    Q_INVOKABLE bool setTorrent(const QString &hash);
    Q_INVOKABLE void clearTorrent();
    /// The torrent line `row` names, "" for none.
    Q_INVOKABLE QString torrentAt(int row) const;

    int count() const { return rowCount(); }
    int levelTotal() const;
    bool isNarrowed() const { return !m_words.isEmpty() || !m_torrentHash.isEmpty(); }

    /// `w`/`W`: the next (`step` 1) or previous (-1) warning or critical line after `from`,
    /// -1 when there is none.
    Q_INVOKABLE int nextProblem(int from, int step) const;
    /// The row of the line with log id `id`, -1 when it isn't shown.
    Q_INVOKABLE int rowOfId(int id) const;
    Q_INVOKABLE int idAt(int row) const;
    /// The message of line `row`, "" out of range.
    Q_INVOKABLE QString messageAt(int row) const;

    // ---- marks (the click and key rules every list follows) --------------------------------
    int markedCount() const { return int(m_marked.size()); }
    Q_INVOKABLE bool isMarked(int row) const;
    /// `space`, ctrl+click.
    Q_INVOKABLE void toggleMark(int row);
    /// `v`, shift+click: `from`..`to` marked; `add` keeps the others (ctrl+shift+click).
    Q_INVOKABLE void markRange(int from, int to, bool add = false);
    /// `ctrl+a`: every line shown.
    Q_INVOKABLE void markAll();
    Q_INVOKABLE void clearMarks();
    /// D11: the marked lines shown, oldest first, or line `cursor` when none is, one per line
    /// as "time level message".
    Q_INVOKABLE QString copyText(int cursor) const;
    /// How many lines copyText(`cursor`) holds.
    Q_INVOKABLE int copyCount(int cursor) const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void logChanged();
    void levelsChanged();
    void textChanged();
    void torrentChanged();
    void countChanged();
    void narrowedChanged();
    void marksChanged();
    /// New lines came in from the server and `count` of them are shown: the face follows
    /// them, or counts them while paused (D12). A change of filter is never this.
    void linesArrived(int count);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    void refilter();
    QList<int> markedRows() const;

    QPointer<ServerLog> m_log;
    int m_levels = 15;
    QString m_text;
    QStringList m_words;
    QString m_torrentHash;
    QString m_torrentName;
    QString m_torrentPrefix;
    QSet<int> m_marked; ///< log ids
};

} // namespace omaseed::core
