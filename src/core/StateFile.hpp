#pragma once

// StateFile — the one `~/.config/omaseed/state.json` on disk, shared by everything that
// keeps a small durable flag (Xdg's first-run magnet-handler prompt, App's per-event
// notification toggles, and whatever else Phase 4 adds). Every read/write goes through
// readSection()/writeSection() so two callers touching different top-level keys never clobber
// each other: a write reads the file fresh, replaces just its own key, and writes the whole
// object back.

#include <QJsonObject>
#include <QString>

namespace omaseed::core {

class StateFile
{
public:
    /// "$XDG_CONFIG_HOME/omaseed/state.json" ("~/.config/omaseed/state.json" when
    /// XDG_CONFIG_HOME is unset).
    static QString path();

    /// The whole file, or `{"version": 1}` when it does not exist or does not parse.
    static QJsonObject read();
    /// Writes `state` verbatim, creating the parent directory if needed.
    static void write(const QJsonObject &state);

    /// One top-level key, e.g. "notifications"; an empty object when absent.
    static QJsonObject readSection(const QString &key);
    /// Read-modify-write: replaces `key` in the file with `value`, keeping every other key.
    static void writeSection(const QString &key, const QJsonObject &value);
};

} // namespace omaseed::core
