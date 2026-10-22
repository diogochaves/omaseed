#pragma once

// ActionOutcome — what a quick action on torrents came to, said where the eye already is
// (board 45 §2, DECISIONS C18 D1–D3, #130).
//
// An action from the table (move, start, stop, delete, rename, category, tags…) ends in one of
// three ways: it landed, it failed, or it became a job long enough for the band (D4: that one
// keeps the band under the top bar). The first two used to be a sentence in the footer, the
// one part of the window the eye isn't on. Now they are:
//
// - a strip in the selection bar's place (bottom middle of the table): done in green for 6 s,
//   its line running down; a failure in red until × / esc or the next action; several at
//   once as counts, never a list ("2 moved to /mnt/fast/keep, 1 didn't"), yellow when some
//   failed (D1, D3);
// - the row's own mark in its status cell while the row is on screen: "✓ moved · keep" for
//   the same 6 s, "✕ didn't move" while the failure stands (D1).
//
// `ActionOutcome::words()` is the words, pure (a QtTest row per action); `ActionOutcomes` holds
// the one strip on show and the rows' marks, for the window to draw. The details' error block
// (docs/28, docs/32) stays as it is.

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <chrono>

namespace omaseed::core {

struct ActionOutcome
{
    /// What was done, by the actions menu's names: "move", "moveBack", "start", "stop",
    /// "retry", "forceStart", "recheck", "reannounce", "category", "addTags", "removeTags",
    /// "limits", "rename", "queueUp", "queueDown", "delete", "deleteWithFiles", "sequential",
    /// "firstLast", "superSeeding", "autoManagement". Anything else reads as a plain "done".
    QString action;
    /// Every torrent the action was about, in order.
    QStringList hashes;
    /// Those of `hashes` that didn't make it; empty when all did.
    QStringList failedHashes;
    /// The torrent's name when the action was about one ("Arcade Set 01").
    QString name;
    /// The first failed torrent's name, for a mixed outcome's line ("Short Film 02: …").
    QString failedName;
    /// What it was done with: a move's new path, a category, the tags, the new name, "on" /
    /// "off" for an option.
    QString target;
    /// Why it failed, in plain words (ErrorWords' line, or the log's reason).
    QString cause;
    /// The technical text, for the strip's "details".
    QString details;

    enum class Kind { Done, Failed, Mixed };
    Kind kind() const;

    /// The strip's words and what it is about: `{kind ("done", "failed", "mixed"), mark (✓ ✕
    /// !), headline, line, details, action, hashes, failedHashes, doneHashes, target,
    /// doneRow, failedRow}`. `doneRow` and `failedRow` are the rows' marks ("moved · keep",
    /// "didn't move").
    QVariantMap words() const;
    /// The fields above from a map with the same names (QML's demo and tests).
    static ActionOutcome fromMap(const QVariantMap &map);
};

class ActionOutcomes : public QObject
{
    Q_OBJECT
    /// The strip on show (ActionOutcome::words(), plus `serial`); empty when none.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    /// Moves whenever a row's mark comes or goes, for the table's bindings.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit ActionOutcomes(QObject *parent = nullptr);

    /// D3: how long a done row keeps its mark (6 s). The strip's own 6 s are the window's,
    /// which holds them while the pointer rests on it.
    void setDoneDuration(std::chrono::milliseconds duration);

    /// A new outcome: it takes the strip, and the marks of the last one go (D3: a failure
    /// stays until the next action).
    void report(const ActionOutcome &outcome);
    /// The same from QML (the demo, the tests): ActionOutcome's fields by name.
    Q_INVOKABLE void reportMap(const QVariantMap &map);
    /// × / esc, or the done strip's time ran out: the strip goes, and a failure's row marks
    /// with it. A done row's mark keeps its own 6 s.
    Q_INVOKABLE void dismiss();
    /// The strip with this `serial` went (its time ran out): dismiss() unless another took
    /// its place meanwhile.
    Q_INVOKABLE void expire(int serial);
    /// The strip and every mark go at once: another server's torrents, or a fresh start.
    Q_INVOKABLE void clear();
    /// A row's mark: `{text, kind ("done" or "failed")}`, empty for none.
    Q_INVOKABLE QVariantMap rowMark(const QString &hash) const;

    QVariantMap current() const { return m_current; }
    int revision() const { return m_revision; }

Q_SIGNALS:
    void currentChanged();
    void revisionChanged();

private:
    struct Mark
    {
        QString text;
        bool failed = false;
        int serial = 0;
    };

    void dropDoneMarks(int serial);
    void bumpRevision();

    QVariantMap m_current;
    QHash<QString, Mark> m_marks;
    std::chrono::milliseconds m_doneDuration{6000};
    int m_serial = 0;
    int m_revision = 0;
};

} // namespace omaseed::core
