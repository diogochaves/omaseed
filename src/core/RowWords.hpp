#pragma once

// RowWords — what one row of the rows display says (board 46 `rowHtml()`, DECISIONS C24 H3,
// law 3 "words first"): the one thing that matters on the right, big ("3 min", "2.4×",
// "no access"), a caption under it ("to ready", "given", "needs you"), one sentence under the
// name ("48.9 % of 480 MB · from 3 seeds at 1.40 MB/s"), the meta beside the name
// ("films · 716 MB"), the first fix of a torrent that needs you ("retry", "move…") and the
// two letters of a shelf's cover ("OM").
//
// The words follow the question the torrent answers (TorrentFilterModel::Question) and, inside
// arriving, its state. Numbers stay in details and the table; a part with nothing to say is
// left out, never printed as a zero. Pure Qt Core; `tst_rowwords` has a row per case.

#include "core/Torrent.hpp"

#include <QString>
#include <QStringList>

namespace omaseed::core {

/// Why a torrent needs you, from TorrentModel's ProblemRole, FailureRole and the Status
/// column's cause (docs/28, docs/32, C20).
struct RowTrouble
{
    /// The Status column's short cause ("no access", "stopped"), or "".
    QString cause;
    /// The error block's headline and its actions' ids (TorrentProblem::toMap()).
    QString headline;
    QStringList actions;
    bool missingFiles = false;
    bool stuckMove = false;
    /// A failure the log holds (TorrentFailure::toMap()): its kind ("move"), headline and
    /// actions; empty kind for none.
    QString failureKind;
    QString failureHeadline;
    QStringList failureActions;
};

struct RowWords
{
    QString fact;
    QString caption;
    QString sentence;
    QString meta;
    /// The fix's words ("retry") and what it runs: "problem:<action>" (the error block's) or
    /// "failure:<action>" (the failure block's); both "" for none.
    QString fix;
    QString fixAction;

    /// The words for `torrent`, which answers `question` (a TorrentFilterModel::Question).
    static RowWords of(const Torrent &torrent, int question, const RowTrouble &trouble);

    /// A cover's two letters (board 39 L3, board 46 `.ab2`): the initials of the name's first
    /// two words, or the first word's first two letters when the second does not start with a
    /// letter (or there is none). Upper case; "" for a name with no letter.
    static QString monogram(const QString &name);

    /// The fix's words for an action id of the error or failure block.
    static QString fixWords(const QString &action, bool stuckMove);
};

} // namespace omaseed::core
