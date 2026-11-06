#include "integration/SkinReader.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using omaseed::integration::SkinReader;
using omaseed::integration::Theme;
using omaseed::integration::ThemeInk;
using omaseed::integration::ThemeReader;

namespace {

constexpr qreal kFloor = Theme::kTextContrastFloor;

/// The smallest lift that reaches the floor: at it, but not by more than a single 1/256
/// step's worth of contrast.
constexpr qreal kStepEpsilon = 0.06;
/// The same on a fill, where a role may sit farther from the foreground it walks to, so one
/// step moves its contrast more.
constexpr qreal kGroundStepEpsilon = 0.1;

/// The fills a theme's shell.toml may move: none (Omarchy's own 4/8/18/22 %), the lighter
/// selected fill some themes set, and a heavy set that pushes every theme past where its
/// text reads, so C30 F6's cap is what keeps the floor.
const char *const kShells[][2] = {
    {"stock", ""},
    {"light selected", "[controls]\nselected-fill-alpha = 0.13\n"},
    {"heavy",
     "[controls]\nnormal-fill-alpha = 0.08\nhover-cursor-fill-alpha = 0.14\n"
     "selected-fill-alpha = 0.30\npressed-fill-alpha = 0.36\n"},
};

QString fixtureThemes()
{
    return QStringLiteral(OMASEED_FIXTURES_DIR "/themes");
}

QStringList fixtureThemeNames()
{
    return QDir(fixtureThemes()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

/// A Theme on one fixture theme and shell.toml, read the way the app reads them: from a
/// throwaway `omarchy/current` with no user overlay, drawn in one skin (#171).
class FixtureTheme
{
public:
    FixtureTheme(const QString &name,
                 const QByteArray &shell,
                 const QString &skin = QStringLiteral("omarchy"))
    {
        const QString current = m_dir.filePath(QStringLiteral("current"));
        QDir().mkpath(current + QStringLiteral("/theme"));
        QFile::copy(fixtureThemes() + QLatin1Char('/') + name + QStringLiteral("/colors.toml"),
                    current + QStringLiteral("/theme/colors.toml"));
        write(current + QStringLiteral("/theme/shell.toml"), shell);
        write(current + QStringLiteral("/theme.name"), name.toUtf8() + '\n');
        QDir().mkpath(m_dir.filePath(QStringLiteral("config")));
        m_reader = std::make_unique<ThemeReader>(
            ThemeReader::Paths{current, m_dir.filePath(QStringLiteral("config"))});
        m_theme = std::make_unique<Theme>(m_reader.get(), Theme::Hyprland{QString(), QString()});
        m_theme->setSkin(skin);
    }

    Theme &theme() { return *m_theme; }
    ThemeReader &reader() { return *m_reader; }
    QString colorsPath() const
    {
        return m_dir.filePath(QStringLiteral("current/theme/colors.toml"));
    }

    static void write(const QString &path, const QByteArray &text)
    {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(text);
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<ThemeReader> m_reader;
    std::unique_ptr<Theme> m_theme;
};

/// Every skin × every fixture theme × every fill set: a skin's ground that fails the floor
/// in one theme is a failing row (the console skin's design note, "Tests").
void addFixtureRows()
{
    QTest::addColumn<QString>("skin");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QByteArray>("shell");
    for (const QString &skin : SkinReader::builtInNames())
        for (const QString &name : fixtureThemeNames())
            for (const auto &shell : kShells)
                QTest::newRow(qPrintable(skin + QStringLiteral(" · ") + name +
                                         QStringLiteral(" · ") + QLatin1String(shell[0])))
                    << skin << name << QByteArray(shell[1]);
}

} // namespace

/// docs/DECISIONS.md Q19 (amended in #89): every colour omaseed paints text with reads at
/// 4.5:1 against the window background in every theme. The ratio is WCAG 2's, and the lift is
/// the least one that gets there.
class TestThemeContrast : public QObject
{
    Q_OBJECT

private slots:
    void theFloorIsWcagAaForNormalText();
    void contrastRatioIsWcags();
    void contrastRatioIsSymmetric();
    void liftIsTheSmallestThatReachesTheFloor_data();
    void liftIsTheSmallestThatReachesTheFloor();
    void themesAlreadyAtTheFloorAreUntouched_data();
    void themesAlreadyAtTheFloorAreUntouched();
    void degeneratePaletteGetsTheForeground();

    // docs/DECISIONS.md C30 F1–F7: every role on every ground.
    void theFixturesAreThere();
    void everyRoleReadsOnEveryGround_data();
    void everyRoleReadsOnEveryGround();
    void eachRoleGetsTheLeastLiftOnItsGround_data();
    void eachRoleGetsTheLeastLiftOnItsGround();
    void everforestsFillsAreCapped();
    void aThemeThatReadsKeepsItsFills();
    void captionsStayCaptionsOnASelectedRow();
    void theWindowGroundIsTheWindowsRoles();
    void groundsFollowAThemeChange();

    // #171: the skin's grounds.
    void omarchysSkinPaintsTheGroundsAsBefore_data();
    void omarchysSkinPaintsTheGroundsAsBefore();
    void consolesGroundsAreItsOwn();
    void aSkinChangeRepaintsTheGrounds();
    void nestedFillsLeftOnTheirGroundRead_data();
    void nestedFillsLeftOnTheirGroundRead();
};

void TestThemeContrast::theFloorIsWcagAaForNormalText()
{
    // Q19 as amended on 2026-10-16: WCAG 2.2 AA for normal-size text, not the 3:1 it was.
    QCOMPARE(Theme::kTextContrastFloor, 4.5);
}

void TestThemeContrast::contrastRatioIsWcags()
{
    QCOMPARE(Theme::contrastRatio(Qt::white, Qt::black), 21.0);
    QCOMPARE(Theme::contrastRatio(Qt::white, Qt::white), 1.0);
    // WCAG's own worked example: #767676 on white passes AA at 4.54.
    QVERIFY(qAbs(Theme::contrastRatio(QColor(QStringLiteral("#767676")), Qt::white) - 4.54) < 0.01);
    // The 1.72 for everforest in docs/DECISIONS.md Q19 came from the same formula.
    QVERIFY(qAbs(Theme::contrastRatio(QColor(QStringLiteral("#4f585e")),
                                      QColor(QStringLiteral("#2d353b"))) -
                 1.72) < 0.01);
}

void TestThemeContrast::contrastRatioIsSymmetric()
{
    const QColor a(QStringLiteral("#565f89"));
    const QColor b(QStringLiteral("#1a1b26"));
    QCOMPARE(Theme::contrastRatio(a, b), Theme::contrastRatio(b, a));
}

void TestThemeContrast::liftIsTheSmallestThatReachesTheFloor_data()
{
    QTest::addColumn<QColor>("background");
    QTest::addColumn<QColor>("foreground");
    QTest::addColumn<QColor>("dim");

    // The worst and the reference theme of the review's table, values from their colors.toml.
    QTest::newRow("everforest dim 1.72")
        << QColor(QStringLiteral("#2d353b")) << QColor(QStringLiteral("#d3c6aa"))
        << QColor(QStringLiteral("#4f585e"));
    QTest::newRow("tokyo-night dim 2.76")
        << QColor(QStringLiteral("#1a1b26")) << QColor(QStringLiteral("#a9b1d6"))
        << QColor(QStringLiteral("#565f89"));
    // A dim that passed the old 3:1 floor and is lifted by the new one.
    QTest::newRow("gruvbox dim 3.03")
        << QColor(QStringLiteral("#282828")) << QColor(QStringLiteral("#d4be98"))
        << QColor(QStringLiteral("#7c6f64"));
    // A light theme lifts the other way, towards a darker foreground.
    QTest::newRow("white dim 1.82")
        << QColor(QStringLiteral("#ffffff")) << QColor(QStringLiteral("#000000"))
        << QColor(QStringLiteral("#c0c0c0"));
    // Palette colours painted as text, the worst of #89's survey: rose-pine's yellow and
    // catppuccin-latte's magenta (light), miasma's red (dark).
    QTest::newRow("rose-pine yellow 2.05")
        << QColor(QStringLiteral("#faf4ed")) << QColor(QStringLiteral("#575279"))
        << QColor(QStringLiteral("#ea9d34"));
    QTest::newRow("catppuccin-latte magenta 2.34")
        << QColor(QStringLiteral("#eff1f5")) << QColor(QStringLiteral("#4c4f69"))
        << QColor(QStringLiteral("#ea76cb"));
    QTest::newRow("miasma red 2.30")
        << QColor(QStringLiteral("#222222")) << QColor(QStringLiteral("#c2c2b0"))
        << QColor(QStringLiteral("#685742"));
}

void TestThemeContrast::liftIsTheSmallestThatReachesTheFloor()
{
    QFETCH(QColor, background);
    QFETCH(QColor, foreground);
    QFETCH(QColor, dim);
    QVERIFY(Theme::contrastRatio(dim, background) < kFloor);

    const QColor lifted = Theme::liftToContrast(dim, foreground, background, kFloor);
    const qreal ratio = Theme::contrastRatio(lifted, background);
    QVERIFY2(ratio >= kFloor,
             qPrintable(QStringLiteral("%1 reads at %2").arg(lifted.name()).arg(ratio)));
    QVERIFY2(ratio < kFloor + kStepEpsilon,
             qPrintable(QStringLiteral("%1 overshoots at %2").arg(lifted.name()).arg(ratio)));
    QVERIFY(lifted != dim);
    QVERIFY(lifted != foreground);
    QCOMPARE(lifted.alpha(), 255);
    // Deterministic: the same inputs give the same byte values every time.
    QCOMPARE(Theme::liftToContrast(dim, foreground, background, kFloor), lifted);
}

void TestThemeContrast::themesAlreadyAtTheFloorAreUntouched_data()
{
    QTest::addColumn<QColor>("background");
    QTest::addColumn<QColor>("foreground");
    QTest::addColumn<QColor>("dim");

    // WCAG's own example sits just over the line at 4.54.
    QTest::newRow("#767676 on white 4.54")
        << QColor(Qt::white) << QColor(Qt::black) << QColor(QStringLiteral("#767676"));
    // Tokyo Night's accent reads already (values from its colors.toml).
    QTest::newRow("tokyo-night accent")
        << QColor(QStringLiteral("#1a1b26")) << QColor(QStringLiteral("#a9b1d6"))
        << QColor(QStringLiteral("#7aa2f7"));
    QTest::newRow("far above") << QColor(Qt::black) << QColor(Qt::white)
                               << QColor(QStringLiteral("#a0a0a0"));
}

void TestThemeContrast::themesAlreadyAtTheFloorAreUntouched()
{
    QFETCH(QColor, background);
    QFETCH(QColor, foreground);
    QFETCH(QColor, dim);
    QVERIFY(Theme::contrastRatio(dim, background) >= kFloor);
    QCOMPARE(Theme::liftToContrast(dim, foreground, background, kFloor).name(), dim.name());
}

void TestThemeContrast::degeneratePaletteGetsTheForeground()
{
    // Nothing on the line from dim to foreground reaches 4.5:1: the foreground is the best
    // the palette has, so that is what the role becomes.
    const QColor background(QStringLiteral("#202020"));
    const QColor foreground(QStringLiteral("#404040"));
    const QColor dim(QStringLiteral("#282828"));
    QVERIFY(Theme::contrastRatio(foreground, background) < kFloor);
    QCOMPARE(Theme::liftToContrast(dim, foreground, background, kFloor).name(), foreground.name());
}

void TestThemeContrast::theFixturesAreThere()
{
    // Omarchy's 23 themes as of 4.5; a theme added later only adds rows.
    QVERIFY2(fixtureThemeNames().size() >= 23, qPrintable(fixtureThemes()));
    QVERIFY(fixtureThemeNames().contains(QStringLiteral("everforest")));
}

void TestThemeContrast::everyRoleReadsOnEveryGround_data()
{
    addFixtureRows();
}

/// C30 F1, F5, F7: on the window, every fill and every tint, every text role reads at 4.5:1
/// against the ground it is painted on, and the key badge's background reads on its accent.
void TestThemeContrast::everyRoleReadsOnEveryGround()
{
    QFETCH(QString, skin);
    QFETCH(QString, name);
    QFETCH(QByteArray, shell);
    FixtureTheme fixture(name, shell, skin);
    const Theme &theme = fixture.theme();
    QCOMPARE(theme.themeName(), name);
    QCOMPARE(theme.skin()->name(), skin);

    const QList<ThemeInk *> grounds = theme.on()->all();
    QCOMPARE(grounds.size(), SkinReader::groundNames().size() + 1); // and the window
    const auto recipes = SkinReader::groundsOf(theme.skin()->skin());
    QStringList failing;
    for (const ThemeInk *ink : grounds) {
        // A fill is painted over the ground its recipe names: the window's, or a block's.
        const QString over = recipes.value(ink->name()).over;
        const ThemeInk *block = theme.on()->byName(over);
        const QColor under =
            block && ink != theme.on()->window() ? block->ground() : theme.background();
        QCOMPARE(ink->under(), under);
        QCOMPARE(ink->ground(), ThemeInk::over(ink->fill(), under, ink->alpha()));
        QCOMPARE(ink->ground().alpha(), 255);
        for (int role = 0; role < ThemeInk::RoleCount; ++role) {
            const qreal ratio =
                Theme::contrastRatio(ink->role(static_cast<ThemeInk::Role>(role)), ink->ground());
            if (ratio < kFloor)
                failing << QStringLiteral("%1 on %2: %3")
                               .arg(QLatin1String(ThemeInk::roleNames()[role]), ink->name())
                               .arg(ratio, 0, 'f', 2);
        }
    }
    // Kit/KeyBadge: the window's background on a solid accent, which Q19 already lifted.
    const qreal badge = Theme::contrastRatio(theme.background(), theme.accent());
    if (badge < kFloor)
        failing << QStringLiteral("background on accent: %1").arg(badge, 0, 'f', 2);
    QVERIFY2(failing.isEmpty(), qPrintable(failing.join(QStringLiteral("; "))));
}

void TestThemeContrast::eachRoleGetsTheLeastLiftOnItsGround_data()
{
    addFixtureRows();
}

/// C30 F2, F3: a role that reads on a ground keeps its window colour there byte for byte; one
/// that doesn't is lifted only as far as the floor, so a caption stays as far from the main
/// text as the ground allows.
void TestThemeContrast::eachRoleGetsTheLeastLiftOnItsGround()
{
    QFETCH(QString, skin);
    QFETCH(QString, name);
    QFETCH(QByteArray, shell);
    FixtureTheme fixture(name, shell, skin);
    const Theme &theme = fixture.theme();
    const ThemeInk *window = theme.on()->window();
    for (const ThemeInk *ink : theme.on()->all()) {
        for (int role = 0; role < ThemeInk::RoleCount; ++role) {
            const auto r = static_cast<ThemeInk::Role>(role);
            const QColor before = window->role(r);
            const QColor after = ink->role(r);
            const QString what = QStringLiteral("%1 on %2")
                                     .arg(QLatin1String(ThemeInk::roleNames()[role]), ink->name());
            if (Theme::contrastRatio(before, ink->ground()) >= kFloor) {
                QVERIFY2(after == before, qPrintable(what + QStringLiteral(" moved")));
                continue;
            }
            const qreal ratio = Theme::contrastRatio(after, ink->ground());
            // Lifted all the way to a text colour: the foreground, or the better of it and
            // the bright one, is where a walk ends.
            const bool atAnEnd =
                after == window->foreground() || after == window->brightForeground();
            QVERIFY2(atAnEnd || ratio < kFloor + kGroundStepEpsilon,
                     qPrintable(what + QStringLiteral(" overshoots at %1").arg(ratio)));
        }
    }
}

/// C30 F6: everforest's foreground and bright foreground are one colour, which reads 4.27:1
/// on its selected fill under the cursor (24 %) and 4.48:1 pressed (22 %). Its fills are held
/// at 0.215, the last 0.005 step where it reads; the lighter ones keep the shell's alpha.
void TestThemeContrast::everforestsFillsAreCapped()
{
    FixtureTheme fixture(QStringLiteral("everforest"), QByteArray());
    const Theme &theme = fixture.theme();
    QCOMPARE(theme.fillCap(), 0.215);
    QCOMPARE(theme.fillSelectedCursor(), 0.215);
    QCOMPARE(theme.fillPressed(), 0.215);
    QCOMPARE(theme.fillSelected(), 0.18);
    QCOMPARE(theme.fillHover(), 0.08);
    QCOMPARE(theme.on()->selectedCursor()->alpha(), 0.215);
    QCOMPARE(theme.on()->pressed()->alpha(), 0.215);
    QVERIFY(Theme::contrastRatio(theme.foreground(), theme.on()->selectedCursor()->ground()) >=
            kFloor);
    // Uncapped, the foreground would not read: the cap is doing the work, not luck.
    QVERIFY(Theme::contrastRatio(theme.foreground(),
                                 ThemeInk::over(theme.foreground(),
                                                theme.background(),
                                                Theme::kFillSelectedCursor)) < kFloor);
}

void TestThemeContrast::aThemeThatReadsKeepsItsFills()
{
    FixtureTheme fixture(QStringLiteral("tokyo-night"), QByteArray());
    const Theme &theme = fixture.theme();
    QVERIFY(theme.fillCap() > Theme::kFillSelectedCursor);
    QCOMPARE(theme.fillSelectedCursor(), Theme::kFillSelectedCursor);
    QCOMPARE(theme.fillPressed(), 0.22);
    QCOMPARE(theme.fillSelected(), 0.18);
}

/// C30 F2 against #90's proposal: a caption on a selected row is still told from the row's
/// main text (board 50 §1: a median of 1.91:1 over the 28 installed themes, 1.63 over
/// Omarchy's 23 here; full strength would be 1.00, one lift for every fill 1.39).
void TestThemeContrast::captionsStayCaptionsOnASelectedRow()
{
    QList<qreal> apart;
    for (const QString &name : fixtureThemeNames()) {
        FixtureTheme fixture(name, QByteArray());
        const ThemeInk *selected = fixture.theme().on()->selected();
        apart << Theme::contrastRatio(selected->dimForeground(), selected->brightForeground());
        QVERIFY2(selected->dimForeground() != selected->brightForeground(), qPrintable(name));
    }
    std::sort(apart.begin(), apart.end());
    const qreal median = apart.at(apart.size() / 2);
    QVERIFY2(median >= 1.5, qPrintable(QString::number(median)));
}

/// On the window nothing moves: `Theme.on.window` is the roles Q19 already resolved.
void TestThemeContrast::theWindowGroundIsTheWindowsRoles()
{
    FixtureTheme fixture(QStringLiteral("catppuccin-latte"), QByteArray());
    const Theme &theme = fixture.theme();
    const ThemeInk *window = theme.on()->window();
    QCOMPARE(window->alpha(), 0.0);
    QCOMPARE(window->ground(), theme.background());
    QCOMPARE(window->foreground(), theme.foreground());
    QCOMPARE(window->dimForeground(), theme.dimForeground());
    QCOMPARE(window->accent(), theme.accent());
    QCOMPARE(window->magenta(), theme.magenta());
    QCOMPARE(window->property("stateStopped").value<QColor>(), theme.dimForeground());
}

/// C30 F7: resolved once per theme change, and only a ground that moved says so.
void TestThemeContrast::groundsFollowAThemeChange()
{
    FixtureTheme fixture(QStringLiteral("tokyo-night"), QByteArray());
    Theme &theme = fixture.theme();
    const ThemeInk *selected = theme.on()->selected();
    const QColor before = selected->dimForeground();
    QSignalSpy spy(selected, &ThemeInk::changed);

    QFile::remove(fixture.colorsPath());
    QVERIFY(QFile::copy(fixtureThemes() + QStringLiteral("/everforest/colors.toml"),
                        fixture.colorsPath()));
    fixture.reader().reload();
    QCOMPARE(spy.count(), 1);
    QVERIFY(selected->dimForeground() != before);
    QCOMPARE(theme.on()->selected(), selected); // the same object, new colours

    fixture.reader().reload(); // nothing moved: nothing repaints
    QCOMPARE(spy.count(), 1);
}

void TestThemeContrast::omarchysSkinPaintsTheGroundsAsBefore_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QByteArray>("shell");
    for (const QString &name : fixtureThemeNames())
        for (const auto &shell : kShells)
            QTest::newRow(qPrintable(name + QStringLiteral(" · ") + QLatin1String(shell[0])))
                << name << QByteArray(shell[1]);
}

/// #171 changes nothing under Omarchy: each of C30's grounds is what Theme painted before
/// skins, tint for tint and alpha for alpha, the panel the window and picked board 46's 9 %.
void TestThemeContrast::omarchysSkinPaintsTheGroundsAsBefore()
{
    QFETCH(QString, name);
    QFETCH(QByteArray, shell);
    FixtureTheme fixture(name, shell);
    const Theme &t = fixture.theme();
    QCOMPARE(t.skin()->name(), QStringLiteral("omarchy"));
    const ThemeInk::Roles window = t.on()->window()->roles();
    const QColor bg = t.background();
    const auto expect =
        [&](const ThemeInk *ink, const QColor &tint, qreal alpha, const QColor &under) {
            ThemeInk before(ink->name());
            before.resolve(tint, alpha, under, window);
            QVERIFY2(before.alpha() == ink->alpha() && before.fill() == ink->fill() &&
                         before.ground() == ink->ground() && before.roles() == ink->roles(),
                     qPrintable(ink->name()));
        };
    const auto *on = t.on();
    expect(on->idle(), t.foreground(), t.fillIdle(), bg);
    expect(on->hover(), t.foreground(), t.fillHover(), bg);
    expect(on->cursor(), t.foreground(), t.fillHover(), bg);
    expect(on->selected(), t.foreground(), t.fillSelected(), bg);
    expect(on->selectedCursor(), t.foreground(), t.fillSelectedCursor(), bg);
    expect(on->pressed(), t.foreground(), t.fillPressed(), bg);
    expect(on->warning(), t.yellow(), Theme::warningTintAlpha(), bg);
    expect(on->attention(), t.red(), Theme::attentionTintAlpha(), bg);
    expect(on->danger(), t.red(), t.fillSelected(), bg);
    expect(on->ticked(), t.accent(), t.fillPressed(), bg);
    expect(on->accentIdle(), t.accent(), t.fillIdle(), bg);
    const QColor block = on->idle()->ground();
    expect(on->blockIdle(), t.foreground(), t.fillIdle(), block);
    expect(on->blockHover(), t.foreground(), t.fillHover(), block);
    expect(on->highlight(), t.accent(), Theme::highlightAlpha(), block);
    // #188: the stronger fills in a block, and every fill over the hover fill, are the same
    // fills over the ground they lie in.
    expect(on->blockSelected(), t.foreground(), t.fillSelected(), block);
    expect(on->blockSelectedCursor(), t.foreground(), t.fillSelectedCursor(), block);
    expect(on->blockPressed(), t.foreground(), t.fillPressed(), block);
    const QColor hover = on->hover()->ground();
    expect(on->hoverIdle(), t.foreground(), t.fillIdle(), hover);
    expect(on->hoverHover(), t.foreground(), t.fillHover(), hover);
    expect(on->hoverSelected(), t.foreground(), t.fillSelected(), hover);
    expect(on->hoverPressed(), t.foreground(), t.fillPressed(), hover);
    expect(on->panel(), t.foreground(), 0, bg);
    expect(on->picked(), t.accent(), Theme::pickedTintAlpha(), bg);
    expect(on->turtle(), t.yellow(), t.fillSelected(), bg);
    expect(on->burst(), t.green(), t.fillSelected(), bg);
    QCOMPARE(on->panel()->ground(), bg);
}

/// Board 46's `.sk-con` tiers, as Theme resolves them: the panel is the lighter background at
/// 45 % over the window, a selected row the lighter background, a picked one the accent at
/// 20 %; what Console leaves alone is Omarchy's.
void TestThemeContrast::consolesGroundsAreItsOwn()
{
    FixtureTheme omarchy(QStringLiteral("tokyo-night"), QByteArray());
    FixtureTheme console(QStringLiteral("tokyo-night"), QByteArray(), QStringLiteral("console"));
    const Theme &t = console.theme();
    QCOMPARE(t.skin()->name(), QStringLiteral("console"));
    QCOMPARE(t.on()->panel()->ground(),
             ThemeInk::over(t.lighterBackground(), t.background(), 0.45));
    QCOMPARE(t.on()->hover()->ground(), t.on()->panel()->ground());
    QCOMPARE(t.on()->selected()->ground(), t.lighterBackground());
    QCOMPARE(t.on()->cursor()->ground(), t.lighterBackground());
    QCOMPARE(t.on()->picked()->ground(), ThemeInk::over(t.accent(), t.background(), 0.2));
    QCOMPARE(t.on()->pressed()->ground(), omarchy.theme().on()->pressed()->ground());
    QCOMPARE(t.on()->idle()->ground(), omarchy.theme().on()->idle()->ground());
    QVERIFY(t.on()->selected()->ground() != omarchy.theme().on()->selected()->ground());
    // #188: panel2 in the servers block and on the action bar; the pressed and idle fills
    // nest as Omarchy's do, over Console's grounds.
    QCOMPARE(t.on()->blockSelected()->ground(), t.lighterBackground());
    QCOMPARE(t.on()->blockSelectedCursor()->ground(), t.lighterBackground());
    QCOMPARE(t.on()->hoverHover()->ground(), t.lighterBackground());
    QCOMPARE(t.on()->hoverSelected()->ground(), t.lighterBackground());
    QCOMPARE(t.on()->blockPressed()->ground(), omarchy.theme().on()->blockPressed()->ground());
    QCOMPARE(t.on()->hoverIdle()->ground(),
             ThemeInk::over(t.foreground(), t.on()->hover()->ground(), t.fillIdle()));
}

void TestThemeContrast::aSkinChangeRepaintsTheGrounds()
{
    FixtureTheme fixture(QStringLiteral("tokyo-night"), QByteArray());
    Theme &theme = fixture.theme();
    const ThemeInk *panel = theme.on()->panel();
    QSignalSpy themeChanged(&theme, &Theme::changed);
    QSignalSpy panelChanged(panel, &ThemeInk::changed);
    QSignalSpy pressedChanged(theme.on()->pressed(), &ThemeInk::changed);

    theme.setSkin(QStringLiteral("console"));
    QCOMPARE(themeChanged.count(), 1);
    QCOMPARE(panelChanged.count(), 1);
    QCOMPARE(pressedChanged.count(), 0); // Console paints it as Omarchy does
    QCOMPARE(theme.on()->panel(), panel);
    QVERIFY(panel->ground() != theme.background());

    theme.setSkin(QStringLiteral("console")); // nothing moved
    QCOMPARE(themeChanged.count(), 1);

    theme.setSkin(QStringLiteral("neon")); // no such skin: Omarchy's, with the reason
    QCOMPARE(theme.skin()->name(), QStringLiteral("omarchy"));
    QCOMPARE(theme.skin()->reason(), QStringLiteral("unknown skin"));
    QCOMPARE(panel->ground(), theme.background());
    QCOMPARE(themeChanged.count(), 2);
}

void TestThemeContrast::nestedFillsLeftOnTheirGroundRead_data()
{
    addFixtureRows();
}

/// #188: a Toggle's knob, on, keeps the ticked tint's colours on a hovered settings row,
/// because the tint over the hover fill still shows it in every skin, theme and fill set at
/// WCAG 1.4.11's 3:1 (the knob is no text). Off, the track is `hoverIdle`, a ground like the
/// rest; QbtListSheet's selection, `blockSelected` (under the heavy fill set the plain
/// selected fill's text read 3.86:1 there).
void TestThemeContrast::nestedFillsLeftOnTheirGroundRead()
{
    QFETCH(QString, skin);
    QFETCH(QString, name);
    QFETCH(QByteArray, shell);
    FixtureTheme fixture(name, shell, skin);
    const auto *on = fixture.theme().on();
    const QColor ticked =
        ThemeInk::over(on->ticked()->fill(), on->hover()->ground(), on->ticked()->alpha());
    const qreal knobOn = Theme::contrastRatio(on->ticked()->brightForeground(), ticked);
    QVERIFY2(knobOn >= 3.0, qPrintable(QString::number(knobOn, 'f', 2)));
}

QTEST_MAIN(TestThemeContrast)

#include "tst_themecontrast.moc"
