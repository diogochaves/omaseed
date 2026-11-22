#pragma once

// AgentGate — what an agent may do through omaseed's own way in (DECISIONS C50 GT3, GT8;
// C45 V14 as C50 amends it): one line for reads and one per kind of act, held in the app
// (state.json's "agents" section) and asked by every word before it answers.
//
// - Reads: on (default) · off. Off, every word that reads the library and `do` answer
//   not-allowed (GT8); check, skin and help read no library and stay.
// - Fixes (retry, start-here, recheck, reannounce, pause, resume, dismiss, locate):
//   off (default) · ask · allow.
// - Moves (move, move-back) and remove: off (default) · ask. They never run unasked: a
//   hand-edited "allow" reads as ask (GT3).
// - Every other action (delete, rename, categories, tags, adds, settings, servers, quit) is
//   never (C45 V14).
// - The gate binds only omaseed's own words; it is a courtesy, not a wall (GT7, GT8).
//
// Pure Qt Core; `tst_agentgate` has a case per rule.

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace omaseed::core {

/// Who asked for an act through `do` (C45 V17, V18): the program that ran the word (S5 reads
/// it from /proc), the pid the bus gave, and whether the person was asked in the window.
struct AgentOrigin
{
    QString caller; ///< the parent's comm ("claude"); "" until S5 reads it
    uint pid = 0;   ///< the calling process as the bus daemon knows it; 0 unknown
    bool asked = false;
};

class AgentGate
{
public:
    /// The kinds of act (GT3). None is an action no level ever allows.
    enum class Kind { None, Fixes, Moves, Remove };
    /// What the gate says to one action as it stands.
    enum class Decision {
        Off,   ///< not-allowed: the line is off, or reads are off
        Ask,   ///< ask in omaseed's window
        Allow, ///< run at once
        Never, ///< never through `do` (C45 V14)
    };

    /// The action's kind ("recheck" → Fixes, "move-back" → Moves); None for anything else.
    static Kind kindOf(const QString &action);
    /// The actions of a kind, in the order the row and `do --list` name them.
    static QStringList actionsOf(Kind kind);
    /// What `do` never does (C45 V14), as `do --list` names it.
    static QStringList neverActions();
    /// The levels a kind's line offers: fixes off · ask · allow, moves and remove off · ask.
    static QStringList levelsOf(Kind kind);
    /// The kind's key in state.json and on App ("fixes", "moves", "remove"); "" for None.
    static QString keyOf(Kind kind);

    bool reads() const { return m_reads; }
    /// "on" or "off".
    QString readsLevel() const;
    /// The kind's level as it stands ("off", "ask", "allow"); "off" for None.
    QString level(Kind kind) const;

    /// True when it changed anything.
    bool setReads(bool on);
    /// `level` read as the line reads it: anything the kind doesn't offer falls back (a moves
    /// "allow" is ask, any other word the default off). True when it changed anything.
    bool setLevel(Kind kind, const QString &level);

    /// The decision for `action` now (GT3, GT8): reads off turns every action off; an action
    /// of no kind is never.
    Decision decide(const QString &action) const;
    static QString decisionName(Decision decision);

    /// The row's last line (C45 V18): what the last act through `do` was, and when.
    QString lastText() const { return m_lastText; }
    QDateTime lastAt() const { return m_lastAt; }
    void setLast(const QString &text, const QDateTime &at);

    /// C45 V18: an act's line in omaseed's journal (`journalctl --user`), `qInfo`'s words:
    /// "agent: recheck on Arcade Set 01, allowed in Settings (claude, pid 48213)". `subject`
    /// is the torrent's name, "3 torrents" or the server ("seedbox"); `how` what came of it
    /// ("allowed in Settings", "allowed in the window", "not allowed", "never").
    static QString journalLine(const QString &action,
                               const QString &subject,
                               const QString &how,
                               const AgentOrigin &origin);
    /// The row's last line as saved, before its time: "recheck · Arcade Set 01", "recheck ·
    /// 3 torrents · 1 didn't", "delete · never". `result` is "" when it all landed.
    static QString lastLine(const QString &action, const QString &subject, const QString &result);
    /// The row's last line as shown (board 62 §8, board 67's row): "last: TEXT · today 15:20".
    static QString lastShown(const QString &text, const QDateTime &at);

    /// state.json's "agents" section: {reads, fixes, moves, remove, last: {text, at}}.
    QJsonObject toJson() const;
    /// The section read back; a missing or unknown value is the default (reads on, the rest
    /// off), a moves or remove "allow" is ask.
    static AgentGate fromJson(const QJsonObject &section);

    /// `do --list` as data (GT3): one object per line, {kind, level, levels, actions}, reads
    /// first; `never` is neverActions().
    QJsonArray listLines() const;

    bool operator==(const AgentGate &other) const = default;

private:
    bool m_reads = true;
    QString m_fixes = QStringLiteral("off");
    QString m_moves = QStringLiteral("off");
    QString m_remove = QStringLiteral("off");
    QString m_lastText;
    QDateTime m_lastAt;
};

} // namespace omaseed::core
