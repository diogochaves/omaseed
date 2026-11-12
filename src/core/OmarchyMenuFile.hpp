#pragma once

// OmarchyMenuFile — omaseed's block in Omarchy's menu extension file,
// `~/.config/omarchy/extensions/omarchy-menu.jsonc` (docs/DECISIONS.md C36 M13–M15, board 53 §6).
//
// omaseed owns only the lines between its two marker comments. The file is never parsed and
// written back: it is scanned (strings, comments, nesting) to find its top-level ids and its
// closing brace, and every byte outside the block stays as it was. The one exception is the
// comma a last entry needs before the block can follow it (M13: added only when nothing but
// whitespace follows that entry on its line; a comment there means no safe place, nothing is
// written). Writes go through QSaveFile.
//
// How the block lands in the menu: Omarchy's shell (shell/plugins/menu/MenuModel.js,
// mergeMenuSources) merges the extension file's entries over its default ones by id, and an
// entry's parent is its id up to the last dot (MenuModel.js, the `parent` default). So a
// top-level `"trigger.torrents"` key in the extension file is a child of the default
// `"trigger"`, i.e. Trigger › Torrents, listed after Trigger's own rows; its rows
// `"trigger.torrents.<row>"` go inside it. The block is therefore a run of top-level entries,
// each ending in a comma (JSONC allows the last one), and the class never needs to know which.
//
// Omarchy reads the file from `$HOME/.config` (not XDG_CONFIG_HOME), and its own menu from
// `$OMARCHY_PATH/default/omarchy/omarchy-menu.jsonc`; that file's presence is what "Omarchy's
// menu is installed" means (M15). Both paths are injectable for tests.

#include <QByteArray>
#include <QString>

namespace omaseed::core {

class OmarchyMenuFile
{
public:
    /// What the file holds: board 53 §6's table, one row each.
    enum class State {
        NoBlock,     ///< a menu file without omaseed's block: add() puts it before the last `}`
        Block,       ///< the block between its markers: rewrite() or remove()
        OldRow,      ///< the pre-C36 one-line `"torrents"` row running omaseed: add() replaces it
        UsersRow,    ///< a `trigger.torrents` (or a `"torrents"` that isn't ours) outside the
                     ///< block: the user's (M14), nothing is written
        NoSafePlace, ///< the last entry ends in a comment, or the file can't be read as a
                     ///< JSONC object, or the markers don't pair: nothing is written
        NoFile,      ///< no extension file, Omarchy's menu installed: add() creates it
        NoMenu,      ///< Omarchy's menu not installed (M15): nothing is written
    };

    /// The opening marker line (without indentation). Recognised by openMarkerPrefix() alone,
    /// so a later wording still finds an older block.
    static QByteArray openMarker();
    static QByteArray openMarkerPrefix();
    /// The closing marker line (without indentation).
    static QByteArray closeMarker();

    /// `$HOME/.config/omarchy/extensions/omarchy-menu.jsonc`, the one file Omarchy's menu loads.
    static QString defaultUserFile();
    /// `$OMARCHY_PATH/default/omarchy/omarchy-menu.jsonc` (`/usr/share/omarchy` when unset).
    static QString defaultOmarchyMenu();

    explicit OmarchyMenuFile(QString userFile = defaultUserFile(),
                             QString omarchyMenu = defaultOmarchyMenu());

    QString userFile() const { return m_userFile; }

    /// Reads the file afresh. `detail`, when given, says why for UsersRow and NoSafePlace
    /// (in English, for the log and the row's line).
    State state(QString *detail = nullptr) const;

    /// Puts `block` (top-level entries, each ending in a comma) between the markers: before the
    /// file's last `}` (NoBlock, OldRow after removing that row), in a new file (NoFile), or in
    /// place of the block already there (Block). False, with `error`, in any other state, for a
    /// malformed block, or when the write fails.
    bool add(const QString &block, QString *error = nullptr);
    /// Replaces what is between the markers with `block`; the marker lines stay. Block only.
    bool rewrite(const QString &block, QString *error = nullptr);
    /// Removes the block, marker to marker. True when there is no block afterwards (also when
    /// there was none); false when the markers don't pair or the write fails.
    bool remove(QString *error = nullptr);

private:
    bool write(const QByteArray &bytes, QString *error) const;

    QString m_userFile;
    QString m_omarchyMenu;
};

} // namespace omaseed::core
