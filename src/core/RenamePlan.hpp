#pragma once

// RenamePlan — the Files tab's "rename many files" sheet (issue #11, docs/18), as pure logic.
//
// Given the files to rename (their paths inside the torrent, in the order the tab lists them),
// every other file path of the torrent, and a find/replace, it works out each file's new path
// and whether the whole set can run:
//
// - Find is plain text, or a regular expression with the regex option. Without everyMatch only
//   the first match in a name changes. Matching ignores case unless matchCase.
// - Only the file's own name is searched, never its folders; and only the part before the
//   last dot unless includeExtension (the inline editor selects the same part).
// - The replacement takes `$1`…`$9` (a regex's groups; literal text in plain mode) and
//   `{n}`, `{nn}`, `{nnn}`…: a counter over the files that match, padded to that many digits.
// - A new name that is empty, "." or "..", or holds a '/', is invalid. Two files ending on the
//   same path, or one landing on a file that is not renamed, collide (paths compare exactly:
//   Linux filesystems tell case apart). Renames that chase each other (a→b while b→c) are
//   ordered so each target is free when its turn comes (executionOrder()); a cycle (a→b,
//   b→a) cannot be ordered and collides.
//
// The API renames one file per request, so a plan runs as many requests in executionOrder().

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

namespace omaseed::core {

struct RenameOptions
{
    QString find;
    QString replace;
    bool regex = false;
    bool everyMatch = false;
    bool matchCase = false;
    bool includeExtension = false;
};

struct RenameRow
{
    enum class Status { Unchanged, Changed, Collides, Invalid };

    QString path;
    QString newPath;
    /// The new file name alone (the last path segment of newPath).
    QString newName;
    Status status = Status::Unchanged;
    /// Where the replacements landed in newName, as (start, length): the preview tints them.
    QList<std::pair<qsizetype, qsizetype>> marks;
};

class RenamePlan
{
public:
    static RenamePlan
    make(const QStringList &paths, const QStringList &otherPaths, const RenameOptions &options);

    QList<RenameRow> rows() const { return m_rows; }
    /// The regex's own complaint when it does not compile; empty otherwise.
    QString error() const { return m_error; }
    int changed() const { return m_changed; }
    int collisions() const { return m_collisions; }
    int invalid() const { return m_invalid; }
    /// Something changes and nothing collides or is invalid.
    bool canRun() const;
    /// Indexes into rows() of the Changed rows, in an order where every target is free when
    /// its turn comes. Empty unless canRun().
    QList<int> executionOrder() const { return m_order; }

private:
    QList<RenameRow> m_rows;
    QString m_error;
    int m_changed = 0;
    int m_collisions = 0;
    int m_invalid = 0;
    QList<int> m_order;
};

} // namespace omaseed::core
