#pragma once

#include "integration/ThemeReader.hpp"

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

namespace omaseed::integration {

/// Whether omarchy-console's skin is on, for Settings › Appearance's "Follow the desktop"
/// (docs/DECISIONS.md C24 H11; the console skin's design note, "Follow the desktop"). It reads
/// what omarchy-console's kit reads, and nothing more: the theme's `console-skin.json` first,
/// else the one in `~/.config/omarchy`, and `console.json`'s `fx` section. Following only
/// switches omaseed to its own built-in Console: the desktop file's values are not taken
/// (ruled for v0.5.0), save `fx.fonts = system`, which is recorded for the skin's fonts.
class DesktopSkin : public QObject
{
    Q_OBJECT

public:
    struct State
    {
        /// The desktop's skin is on: a skin file that reads, and `fx.skin` not `off`.
        bool on = false;
        /// `console.json`'s `fx.fonts` is `system`: the shell's font everywhere, skin or not.
        bool systemFonts = false;
        /// Which skin file was read: "theme", "config", or "" when there is none.
        QString source;
        /// Why the skin is off, "" when it is on: "off" (fx.skin), "missing", "unparseable",
        /// "no version" (omarchy-console's own words, its Skin.js `resolveSkin`).
        QString reason = QStringLiteral("missing");
        bool operator==(const State &) const = default;
    };

    /// The rule, on the files' texts; std::nullopt is a file that isn't there. Pure.
    static State resolve(const std::optional<QByteArray> &themeSkin,
                         const std::optional<QByteArray> &configSkin,
                         const std::optional<QByteArray> &consoleJson);
    /// The rule, on the files under `paths` (ThemeReader's: the state and config folders).
    static State read(const ThemeReader::Paths &paths);

    /// Reads at once, then follows the three files: each is written by rename, so their
    /// folders are watched, and the watches rebuilt after every change.
    explicit DesktopSkin(ThemeReader::Paths paths, QObject *parent = nullptr);

    State state() const { return m_state; }
    bool on() const { return m_state.on; }
    bool systemFonts() const { return m_state.systemFonts; }

    /// Read again now (a theme switch, which ThemeReader sees first).
    void refresh();

Q_SIGNALS:
    /// The state moved (on, systemFonts, source or reason).
    void changed();

private:
    void rewatch();

    ThemeReader::Paths m_paths;
    State m_state;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

} // namespace omaseed::integration
