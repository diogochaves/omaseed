#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::integration::Theme;
using omaseed::integration::ThemeReader;

namespace {

/// A little longer than the reader's 750 ms debounce, so a change has certainly landed.
constexpr int kAfterDebounceMs = 1400;

const char *kTokyoNight = R"(mode = "dark"
accent = "#7aa2f7"
background = "#1a1b26"
foreground = "#a9b1d6"
dark_foreground = "#565f89"
)";

const char *kLupine = R"(mode = "light"
accent = "#3264eb"
background = "#fafafa"
foreground = "#212121"
dark_foreground = "#757575"
)";

/// A light palette whose every text colour is under 4.5:1 against its white ground.
const char *kFaintText = R"(mode = "light"
background = "#ffffff"
foreground = "#202020"
dark_foreground = "#a0a0a0"
accent = "#7fb2ff"
red = "#ff8080"
yellow = "#e0c040"
orange = "#ffa060"
green = "#70d070"
cyan = "#60d0e0"
blue = "#80a0ff"
magenta = "#e080e0"
)";

/// A dark palette whose every text colour already reads at 4.5:1 or more.
const char *kReadableText = R"(mode = "dark"
background = "#101010"
foreground = "#e0e0e0"
dark_foreground = "#8a8a8a"
accent = "#6fa8ff"
red = "#ff6b6b"
yellow = "#e8c547"
orange = "#f59a52"
green = "#5fd068"
cyan = "#4fc8d8"
blue = "#7a9cff"
magenta = "#d77ee0"
)";

/// The roles Q19 puts a floor under, as `Theme` names them and as colors.toml does.
const QList<QPair<const char *, const char *>> kTextRoles{
    {"dimForeground", "dark_foreground"},
    {"accent", "accent"},
    {"red", "red"},
    {"yellow", "yellow"},
    {"orange", "orange"},
    {"green", "green"},
    {"cyan", "cyan"},
    {"blue", "blue"},
    {"magenta", "magenta"},
};

/// Each state role and the palette role it reads.
const QList<QPair<const char *, const char *>> kStateRoles{
    {"stateDownloading", "blue"},
    {"stateSeeding", "green"},
    {"stateStopped", "dimForeground"},
    {"stateError", "red"},
    {"stateChecking", "yellow"},
    {"stateQueued", "magenta"},
    {"stateStalled", "orange"},
};

QColor role(const Theme &theme, const char *name)
{
    return theme.property(name).value<QColor>();
}

/// The colour a colors.toml line gives `key`.
QColor paletteColour(const char *colors, const char *key)
{
    const QString prefix = QString::fromLatin1(key) + QStringLiteral(" = \"");
    for (const QString &line : QString::fromUtf8(colors).split(QLatin1Char('\n'))) {
        if (line.startsWith(prefix))
            return QColor::fromString(line.mid(prefix.size(), 7));
    }
    return {};
}

} // namespace

/// The reader's job away from the resolver: find the right file, notice a theme change once,
/// and stay quiet when nothing really changed.
class TestThemeReader : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void fallsBackToTheBundledPalette();
    void readsTheThemeFromTheStatePath();
    void readsOmarchy3sConfigPath();
    void reportsOneChangeWhenTheThemeDirectoryIsReplaced();
    void staysQuietWhenTheRewriteIsIdentical();
    void reloadIsImmediate();
    void userShellOverlayBeatsTheThemesShell();
    void spacingAndTypeFollowTheBaseSize();
    void windowAlphaFollowsTheLaunchersBackgroundAlpha();
    void dimForegroundIsLiftedToTheContrastFloor();
    void textColoursUnderTheFloorAreLifted();
    void textColoursAtTheFloorAreUntouched();

private:
    ThemeReader::Paths paths() const;
    QString stateDir() const { return m_dir.path() + QStringLiteral("/state/omarchy/current"); }
    QString configDir() const { return m_dir.path() + QStringLiteral("/config/omarchy"); }
    void writeFile(const QString &path, const QString &text) const;
    void installTheme(const char *colors, const QString &name) const;

    QTemporaryDir m_dir;
};

ThemeReader::Paths TestThemeReader::paths() const
{
    ThemeReader::Paths p;
    p.stateCurrentDir = stateDir();
    p.configOmarchyDir = configDir();
    return p;
}

void TestThemeReader::writeFile(const QString &path, const QString &text) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(text.toUtf8());
}

/// Put a theme in place the way `omarchy-theme-set` does: build it beside the live one,
/// delete the live one, rename the new one over it, then write the name.
void TestThemeReader::installTheme(const char *colors, const QString &name) const
{
    const QString next = stateDir() + QStringLiteral("/theme.next");
    QDir(next).removeRecursively();
    writeFile(next + QStringLiteral("/colors.toml"), QString::fromUtf8(colors));

    const QString live = stateDir() + QStringLiteral("/theme");
    QDir(live).removeRecursively();
    QVERIFY(QDir().rename(next, live));
    writeFile(stateDir() + QStringLiteral("/theme.name"), name + QStringLiteral("\n"));
}

void TestThemeReader::init()
{
    QVERIFY(m_dir.isValid());
    QDir(m_dir.path() + QStringLiteral("/state")).removeRecursively();
    QDir(m_dir.path() + QStringLiteral("/config")).removeRecursively();
    QDir().mkpath(stateDir());
    QDir().mkpath(configDir());
}

void TestThemeReader::fallsBackToTheBundledPalette()
{
    const ThemeReader reader(paths());

    QVERIFY(reader.source().startsWith(QLatin1Char(':')));
    QCOMPARE(reader.themeName(), QStringLiteral("tokyo-night"));
    QCOMPARE(reader.palette().value(QStringLiteral("background")), QStringLiteral("#1a1b26"));
    QCOMPARE(reader.palette().value(QStringLiteral("accent")), QStringLiteral("#7aa2f7"));
    QCOMPARE(reader.mode(), QStringLiteral("dark"));
}

void TestThemeReader::readsTheThemeFromTheStatePath()
{
    installTheme(kLupine, QStringLiteral("lupine"));
    const ThemeReader reader(paths());

    QCOMPARE(reader.source(), stateDir() + QStringLiteral("/theme/colors.toml"));
    QCOMPARE(reader.themeName(), QStringLiteral("lupine"));
    QCOMPARE(reader.mode(), QStringLiteral("light"));
    QCOMPARE(reader.palette().value(QStringLiteral("background")), QStringLiteral("#fafafa"));
    // …and the cascade ran: dark_background is the background mixed a quarter towards black.
    QCOMPARE(reader.palette().value(QStringLiteral("dark_background")), QStringLiteral("#bcbcbc"));
}

void TestThemeReader::readsOmarchy3sConfigPath()
{
    writeFile(configDir() + QStringLiteral("/current/theme/colors.toml"),
              QString::fromUtf8(kTokyoNight));
    const ThemeReader reader(paths());

    QCOMPARE(reader.source(), configDir() + QStringLiteral("/current/theme/colors.toml"));
    QCOMPARE(reader.themeName(), QStringLiteral("theme"));
    QCOMPARE(reader.palette().value(QStringLiteral("background")), QStringLiteral("#1a1b26"));
}

void TestThemeReader::reportsOneChangeWhenTheThemeDirectoryIsReplaced()
{
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    ThemeReader reader(paths());
    QSignalSpy spy(&reader, &ThemeReader::changed);

    installTheme(kLupine, QStringLiteral("lupine"));

    QTest::qWait(kAfterDebounceMs);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(reader.themeName(), QStringLiteral("lupine"));
    QCOMPARE(reader.palette().value(QStringLiteral("background")), QStringLiteral("#fafafa"));
    QCOMPARE(reader.mode(), QStringLiteral("light"));

    // The watcher survives the rename: a second switch is seen too.
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    QTest::qWait(kAfterDebounceMs);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(reader.mode(), QStringLiteral("dark"));
}

void TestThemeReader::staysQuietWhenTheRewriteIsIdentical()
{
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    ThemeReader reader(paths());
    QSignalSpy spy(&reader, &ThemeReader::changed);

    // Same theme set twice, which is what a theme-set of the current theme looks like.
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    QTest::qWait(kAfterDebounceMs);
    QCOMPARE(spy.count(), 0);

    // A comment is not a palette change either.
    writeFile(stateDir() + QStringLiteral("/theme/colors.toml"),
              QStringLiteral("# just a comment\n") + QString::fromUtf8(kTokyoNight));
    QTest::qWait(kAfterDebounceMs);
    QCOMPARE(spy.count(), 0);
}

void TestThemeReader::reloadIsImmediate()
{
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    ThemeReader reader(paths());
    QSignalSpy spy(&reader, &ThemeReader::changed);

    installTheme(kLupine, QStringLiteral("lupine"));
    reader.reload(); // what the theme-set hook does over D-Bus

    QCOMPARE(spy.count(), 1);
    QCOMPARE(reader.mode(), QStringLiteral("light"));

    // And the debounced watcher does not emit a second time for the same content.
    QTest::qWait(kAfterDebounceMs);
    QCOMPARE(spy.count(), 1);
}

void TestThemeReader::userShellOverlayBeatsTheThemesShell()
{
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    writeFile(stateDir() + QStringLiteral("/theme/shell.toml"),
              QStringLiteral("[font]\nbase-size = 12\n\n[controls]\n"
                             "normal-fill-alpha = 0.04\nselected-fill-alpha = 0.18\n"));
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[font]\nbase-size = 14\n"));

    const ThemeReader reader(paths());

    QCOMPARE(reader.shellValues().value(QStringLiteral("font.base-size")), QStringLiteral("14"));
    QCOMPARE(reader.shellValues().value(QStringLiteral("controls.normal-fill-alpha")),
             QStringLiteral("0.04"));
    QCOMPARE(reader.shellValues().value(QStringLiteral("controls.selected-fill-alpha")),
             QStringLiteral("0.18"));
}

void TestThemeReader::spacingAndTypeFollowTheBaseSize()
{
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    writeFile(stateDir() + QStringLiteral("/theme/shell.toml"),
              QStringLiteral("[font]\nbase-size = 12\n[spacing]\nscale = 1.0\n"
                             "scale-with-font = true\n"));
    ThemeReader reader(paths());
    Theme theme(&reader);

    QCOMPARE(theme.font()->baseSize(), 12);
    QCOMPARE(theme.font()->body(), 12);
    QCOMPARE(theme.space(10), 10);
    QCOMPARE(theme.space(0), 0);

    // The user's overlay takes the shell to 14: space(10) becomes round(10 * 14 / 12) = 12.
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[font]\nbase-size = 14\n"));
    reader.reload();

    QCOMPARE(theme.font()->baseSize(), 14);
    QCOMPARE(theme.font()->body(), 14);
    QCOMPARE(theme.font()->caption(), 12); // round(14 * 0.833)
    QCOMPARE(theme.font()->heading(), 19); // round(14 * 1.333)
    QCOMPARE(theme.space(10), 12);
    QCOMPARE(theme.rowHeight(), 34); // round(2.4 * 14)

    // A pinned [font] token overrides its step of the scale without moving the others.
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[font]\nbase-size = 14\nheading = 24\n"));
    reader.reload();
    QCOMPARE(theme.font()->heading(), 24);
    QCOMPARE(theme.font()->body(), 14);
}

/// docs/DECISIONS.md Q1: the ground's default alpha is `[launcher] background-alpha`, 0.95
/// when the key is missing, the user's overlay winning; an override set through
/// setWindowAlpha() survives a theme change.
void TestThemeReader::windowAlphaFollowsTheLaunchersBackgroundAlpha()
{
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));

    // No shell.toml at all: the fallback, and the ground starts there.
    {
        ThemeReader reader(paths());
        Theme theme(&reader);
        QCOMPARE(theme.defaultWindowAlpha(), 0.95);
        QCOMPARE(theme.windowAlpha(), 0.95);
    }

    // The theme's shell.toml sets the key.
    writeFile(stateDir() + QStringLiteral("/theme/shell.toml"),
              QStringLiteral("[font]\nbase-size = 12\n\n[launcher]\n"
                             "background = \"#11111b\"\nbackground-alpha = 0.80\n"));
    ThemeReader reader(paths());
    Theme theme(&reader);
    QCOMPARE(theme.defaultWindowAlpha(), 0.80);
    QCOMPARE(theme.windowAlpha(), 0.80);

    // The user's overlay beats the theme's value…
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[launcher]\nbackground-alpha = 0.70\n"));
    QSignalSpy changed(&theme, &Theme::changed);
    reader.reload();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(theme.defaultWindowAlpha(), 0.70);
    // …and, with nobody binding windowAlpha to the default, the ground keeps its value:
    // it is MainView's Binding that decides between the default and the override.
    QCOMPARE(theme.windowAlpha(), 0.80);

    // An override set through setWindowAlpha() is never stomped by a theme change.
    theme.setWindowAlpha(0.60);
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[launcher]\nbackground-alpha = 1.0\n"));
    reader.reload();
    QCOMPARE(theme.defaultWindowAlpha(), 1.0);
    QCOMPARE(theme.windowAlpha(), 0.60);

    // Out-of-range and non-numeric values clamp or fall back rather than leak into the ground.
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[launcher]\nbackground-alpha = 1.5\n"));
    reader.reload();
    QCOMPARE(theme.defaultWindowAlpha(), 1.0);
    writeFile(configDir() + QStringLiteral("/shell.toml"),
              QStringLiteral("[launcher]\nbackground-alpha = \"opaque\"\n"));
    reader.reload();
    QCOMPARE(theme.defaultWindowAlpha(), 0.95);
}

/// docs/DECISIONS.md Q19: a theme whose dark_foreground is under the floor (4.5:1 since #89)
/// against its background gets the role lifted towards the foreground; one already there
/// keeps it byte for byte.
void TestThemeReader::dimForegroundIsLiftedToTheContrastFloor()
{
    // Tokyo Night's #565f89 reads at 2.76 on #1a1b26: lifted, and to the same colour the
    // static helper gives, so the resolver and the role agree.
    installTheme(kTokyoNight, QStringLiteral("tokyo-night"));
    ThemeReader reader(paths());
    Theme theme(&reader);
    QVERIFY(theme.dimForeground() != QColor(QStringLiteral("#565f89")));
    QVERIFY(Theme::contrastRatio(theme.dimForeground(), theme.background()) >=
            Theme::kTextContrastFloor);
    QCOMPARE(theme.dimForeground(),
             Theme::liftToContrast(QColor(QStringLiteral("#565f89")),
                                   theme.foreground(),
                                   theme.background(),
                                   Theme::kTextContrastFloor));

    // Lupine's #757575 reads at 4.41 on #fafafa: over the old 3:1, under 4.5, so lifted now.
    installTheme(kLupine, QStringLiteral("lupine"));
    reader.reload();
    QVERIFY(theme.dimForeground() != QColor(QStringLiteral("#757575")));
    QVERIFY(Theme::contrastRatio(theme.dimForeground(), theme.background()) >=
            Theme::kTextContrastFloor);
}

/// Q19 amended (#89): every colour omaseed paints text with gets the floor, by the same lift,
/// and the state roles, which read them, follow.
void TestThemeReader::textColoursUnderTheFloorAreLifted()
{
    installTheme(kFaintText, QStringLiteral("faint-text"));
    ThemeReader reader(paths());
    Theme theme(&reader);
    const QColor background(QStringLiteral("#ffffff"));
    const QColor foreground(QStringLiteral("#202020"));
    QCOMPARE(theme.background(), background);
    QCOMPARE(theme.foreground(), foreground);

    for (const auto &[name, key] : kTextRoles) {
        const QColor own = paletteColour(kFaintText, key);
        QVERIFY2(own.isValid(), key);
        QVERIFY2(Theme::contrastRatio(own, background) < Theme::kTextContrastFloor, key);
        const QColor shown = role(theme, name);
        QVERIFY2(Theme::contrastRatio(shown, background) >= Theme::kTextContrastFloor,
                 qPrintable(QStringLiteral("%1 %2 reads at %3")
                                .arg(QLatin1String(name), shown.name())
                                .arg(Theme::contrastRatio(shown, background))));
        QCOMPARE(shown,
                 Theme::liftToContrast(own, foreground, background, Theme::kTextContrastFloor));
    }
    for (const auto &[state, base] : kStateRoles)
        QCOMPARE(role(theme, state), role(theme, base));

    // A role that is never text keeps the theme's colour: the bright foreground falls back
    // to the foreground in the cascade, and is not lifted (nor need it be).
    QCOMPARE(theme.brightForeground(), foreground);
}

void TestThemeReader::textColoursAtTheFloorAreUntouched()
{
    installTheme(kReadableText, QStringLiteral("readable-text"));
    ThemeReader reader(paths());
    Theme theme(&reader);
    for (const auto &[name, key] : kTextRoles) {
        const QColor own = paletteColour(kReadableText, key);
        QVERIFY2(own.isValid(), key);
        QVERIFY2(Theme::contrastRatio(own, theme.background()) >= Theme::kTextContrastFloor, key);
        QCOMPARE(role(theme, name).rgba(), own.rgba()); // byte for byte
    }
    for (const auto &[state, base] : kStateRoles)
        QCOMPARE(role(theme, state).rgba(), role(theme, base).rgba());
}

QTEST_MAIN(TestThemeReader)

#include "tst_themereader.moc"
