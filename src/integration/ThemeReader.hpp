#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace omaseed::integration {

/// A flat palette: every key `omarchy-theme-color --all` prints, with its resolved value.
using ColorMap = QHash<QString, QString>;

/// Reads the Omarchy theme the way the rest of the desktop does and tells us when it moves.
///
/// Three things come out of here: the resolved colour palette (identical to
/// `omarchy-theme-color --file <colors.toml> --all`), the flattened `shell.toml` values
/// (`section.key`, with the user's `~/.config/omarchy/shell.toml` winning over the theme's),
/// and the theme name. The lookup order for the palette is Omarchy 4's state path, then
/// Omarchy 3's config path, then the Tokyo Night copy bundled as a Qt resource, so the
/// application also runs on a machine that has never seen Omarchy.
///
/// `omarchy-theme-set` builds the new theme elsewhere and renames it over
/// `~/.local/state/omarchy/current/theme`, which is why the watcher sits on the stable parent
/// directory. Changes are debounced 750 ms and then coalesced by comparing a hash of the
/// resolved palette plus the shell values, so a rewrite with identical content is silent.
class ThemeReader : public QObject
{
    Q_OBJECT

public:
    /// Where a reader looks. The defaults follow XDG_STATE_HOME and XDG_CONFIG_HOME, which
    /// is what lets the tests point a reader at a temporary directory.
    struct Paths
    {
        /// `~/.local/state/omarchy/current`: holds `theme/` and `theme.name`.
        QString stateCurrentDir;
        /// `~/.config/omarchy`: holds Omarchy 3's `current/theme/` and the shell overlay.
        QString configOmarchyDir;

        static Paths fromEnvironment();
    };

    explicit ThemeReader(QObject *parent = nullptr);
    explicit ThemeReader(Paths paths, QObject *parent = nullptr);

    /// The resolved palette. Keys are Omarchy's: `background`, `dark_foreground`, `color8`…
    ColorMap palette() const { return m_palette; }
    /// `shell.toml` flattened to `section.key`, the user overlay merged over the theme's.
    QHash<QString, QString> shellValues() const { return m_shell; }
    /// Name from `theme.name`, the theme directory's basename, or "tokyo-night" when bundled.
    QString themeName() const { return m_themeName; }
    /// The `colors.toml` actually in use, `:/…` when the bundled fallback won.
    QString source() const { return m_source; }
    /// "dark" or "light", resolved the way `omarchy-theme-color mode` resolves it.
    QString mode() const { return m_palette.value(QStringLiteral("mode")); }

    /// Re-read everything now and emit changed() if anything actually differs.
    Q_INVOKABLE void reload();

    /// Put a `Reload()` method on the session bus at AppIdentity::dbusThemeObjectPath() so
    /// the theme-set hook can poke us without waiting out the debounce. The application
    /// calls this once; tests leave it alone. False when there is no session bus.
    bool exportToDBus();

    // ---- the pure parts, so the tests can hit them without a filesystem ----------------

    /// Parse the subset of TOML these files use: `[section]` headers, `key = value`, `#`
    /// comments, quoted or bare values. Keys come back as `section.key`, or bare at the top
    /// level (`colors.toml` has no sections). Keys and values outside the character set
    /// `omarchy-theme-color` accepts are dropped, exactly as it drops them.
    static QHash<QString, QString> parseToml(const QString &text);

    /// Apply Omarchy's alias and fallback cascade to a raw `colors.toml` map.
    static ColorMap resolvePalette(const ColorMap &raw, bool lightModeFile = false);

    /// Blend `start` towards `end` by `amount` (0…1) with Omarchy's `int(x + 0.5)` rounding.
    /// A side that is not a `#rrggbb` colour counts as black, which is what the shell's awk
    /// mixer does with a missing palette entry.
    static QString mixColor(const QString &start, const QString &end, double amount);

    /// "dark" or "light" for a raw map: `mode`, then `theme_type`, then a `light.mode` file
    /// beside the colours, then background luminance, then dark.
    static QString themeMode(const ColorMap &colors, bool lightModeFile);

    /// The bundled Tokyo Night fallback, as a Qt resource path.
    static QString bundledColorsPath();

Q_SIGNALS:
    /// The palette, the shell values or the theme name changed. Never emitted for a rewrite
    /// that resolves to the same thing.
    void changed();

private:
    void connectWatcher();
    void rewatch();
    QString findColorsFile(bool *lightModeFile) const;

    Paths m_paths;
    ColorMap m_palette;
    QHash<QString, QString> m_shell;
    QString m_themeName;
    QString m_source;
    QByteArray m_fingerprint;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

} // namespace omaseed::integration
