#pragma once

// OmarchyMenuBlock — the text of omaseed's block in Omarchy's menu, Trigger › Torrents
// (docs/DECISIONS.md C36 M2–M11, board 53 §4–§5), for OmarchyMenuFile to write.
//
// The rows come from one template, `data/omarchy-menu.jsonc`, built in as
// `:/omaseed/omarchy-menu.jsonc` and installed to /usr/share/omaseed/ for a hand copy: the lines
// between its two markers. Only Servers › is made here (M11), since it lists this machine's
// servers: with two servers or more, a row per server in place of the template's placeholder
// comment, each calling the Status bus's SwitchServer; with fewer, the placeholder goes. The ✓
// is not written: each row's `checked` asks the status file when the menu opens, so the block
// only changes when the list does (a server added, removed or renamed).
//
// A server's name and id are escaped twice: for the shell command they land in (single quotes;
// the menu runs actions with `bash -lc` and its tests in bash), then for the JSON string holding
// that command. The jq test takes the id with `--arg`, so jq never reads it as code.

#include <QList>
#include <QString>

namespace omaseed::core {

class OmarchyMenuBlock
{
public:
    struct Server
    {
        QString id;   ///< what SwitchServer takes (the server list's id)
        QString name; ///< the row's label
    };

    /// The block for these servers, in their order: top-level entries, each ending in a comma,
    /// no markers (what OmarchyMenuFile::add and rewrite take).
    static QString text(const QList<Server> &servers);

    /// The template's lines between its markers, as built in (the placeholder still there).
    /// Empty when the built-in template has no pair of markers.
    static QString templateText();

    /// How the placeholder line that Servers › replaces starts (trimmed).
    static QString serversPlaceholder();

    /// `value` as one POSIX shell word: single-quoted, a `'` inside as `'\''`.
    static QString shellQuoted(const QString &value);
    /// `value` as a JSON string, its quotes included.
    static QString jsonString(const QString &value);
};

} // namespace omaseed::core
