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
    /// DECISIONS C25 K24 (board 47's outcome strip): the engine stopped the ones that failed
    /// (a move on Transmission), so the strip offers "start it here" in place of "details".
    bool startHere = false;
    /// Board 47's strip on Deluge (C25 K24): it paused the ones that failed and only a
    /// recheck clears their error, so the strip offers "recheck" in place of "details".
    bool recheckFirst = false;
    /// C45 V18 (board 62 §5): an agent asked for it through `do`; the strip says "by an
    /// agent" after the headline, dim (`agent` in words()).
    bool byAgent = false;
    /// C50 GT6 (#295): an agent's remove kept these (RemovedKeep ids, one per torrent it
    /// removed); the strip offers "add back" while any are (`keptIds` in words()).
    QStringList keptIds;

    enum class Kind { Done, Failed, Mixed };
    Kind kind() const;

    /// The strip's words and what it is about: `{kind ("done", "failed", "mixed"), mark (✓ ✕
    /// !), headline, line, details, action, hashes, failedHashes, doneHashes, target,
    /// doneRow, failedRow, agent, keptIds}`; `agent` is "by an agent" for an agent's act, else "".
    /// `doneRow` and `failedRow` are the rows' marks ("moved · keep", "didn't move").
    QVariantMap words() const;
    /// The fields above from a map with the same names (QML's demo and tests).
    static ActionOutcome fromMap(const QVariantMap &map);
    /// "by an agent", the words V18 adds wherever an agent's act shows.
    static QString byAnAgent();
    /// #304 (C50 GT4): one strip for an agent's whole plan, its lines' outcomes in `parts`:
    /// the headline counts what landed by action, in the order the plan ran them ("Plan: 18
    /// located in 5 folders · 9 removed", ", 2 didn't" when some failed); `line` is the plan's
    /// own ("Done: 27 of 27 · 18 rechecking"), then the first failure's "NAME: why". The same
    /// keys as words(), `action` "plan"; `doneRow` "done" so "show" is there for what landed.
    static QVariantMap planWords(const QList<ActionOutcome> &parts, const QString &line);
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
    /// #304: a plan's one strip (ActionOutcome::planWords); each torrent's row keeps its own
    /// line's mark.
    void reportPlan(const QList<ActionOutcome> &parts, const QString &line);
    /// The same from QML (the demo, the tests): ActionOutcome's fields by name.
    Q_INVOKABLE void reportMap(const QVariantMap &map);
    /// A strip whose words are already made (#146: a quick creation, or a cancelled one):
    /// `{kind ("done", "failed", "dim"), mark, headline, line, doneHashes, more {id, text}}`.
    /// No row marks.
    void reportWords(const QVariantMap &words);
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
    /// The strip `words` on show, the rows' marks from `parts`.
    void show(QVariantMap words, const QList<ActionOutcome> &parts);
    void bumpRevision();

    QVariantMap m_current;
    QHash<QString, Mark> m_marks;
    std::chrono::milliseconds m_doneDuration{6000};
    int m_serial = 0;
    int m_revision = 0;
};

} // namespace omaseed::core
