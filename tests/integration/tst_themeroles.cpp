#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaProperty>
#include <QTemporaryDir>
#include <QTest>

using omaseed::integration::Theme;
using omaseed::integration::ThemeReader;

namespace {

/// Tokyo Night's colours, with its dark_foreground under the floor so the role is a lifted one.
const char *kColors = R"(accent = "#7aa2f7"
background = "#1a1b26"
foreground = "#a9b1d6"
dark_foreground = "#565f89"
color1 = "#f7768e"
color2 = "#9ece6a"
color4 = "#7aa2f7"
)";

/// The theme's shell.toml moves one fill and the ground's alpha, so both are read, not
/// defaults.
const char *kShell = R"([controls]
hover-cursor-fill-alpha = 0.1
[launcher]
background-alpha = 0.9
)";

/// The Hyprland a role dump uses: none, as `omaseed --theme-roles` does.
Theme::Hyprland noHyprland()
{
    return Theme::Hyprland{QString(), QString()};
}

} // namespace

/// #76: `omaseed --theme-roles` prints the roles as Theme resolves them, for tools (a contrast
/// check across themes) that must not re-implement the palette's cascade or the Q19 lift.
class TestThemeRoles : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void hexColor_data();
    void hexColor();
    void rolesAreThoseThemeResolved();
    void everyColourAndAlphaRoleIsThere();
    void groundsAreThoseThemeResolved();

private:
    void writeFile(const QString &path, const QByteArray &text) const;

    QTemporaryDir m_dir;
    QByteArray m_state;
    QByteArray m_config;
};

void TestThemeRoles::writeFile(const QString &path, const QByteArray &text) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(text);
}

/// The theme where the app would look for it: XDG_STATE_HOME/omarchy/current, as
/// `omarchy-theme-set` leaves it, and an empty XDG_CONFIG_HOME with no shell overlay.
void TestThemeRoles::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_state = qgetenv("XDG_STATE_HOME");
    m_config = qgetenv("XDG_CONFIG_HOME");
    const QString current = m_dir.filePath(QStringLiteral("state/omarchy/current"));
    writeFile(current + QStringLiteral("/theme/colors.toml"), kColors);
    writeFile(current + QStringLiteral("/theme/shell.toml"), kShell);
    writeFile(current + QStringLiteral("/theme.name"), "fixture-night\n");
    QDir().mkpath(m_dir.filePath(QStringLiteral("config")));
    qputenv("XDG_STATE_HOME", QFile::encodeName(m_dir.filePath(QStringLiteral("state"))));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_dir.filePath(QStringLiteral("config"))));
}

void TestThemeRoles::cleanupTestCase()
{
    qputenv("XDG_STATE_HOME", m_state);
    qputenv("XDG_CONFIG_HOME", m_config);
}

void TestThemeRoles::hexColor_data()
{
    QTest::addColumn<QColor>("colour");
    QTest::addColumn<QString>("hex");

    QTest::newRow("opaque") << QColor(QStringLiteral("#1A1B26")) << QStringLiteral("#1a1b26");
    // Alpha last, as CSS has it, not Qt's #aarrggbb.
    QTest::newRow("translucent") << QColor(0x1a, 0x1b, 0x26, 0x80) << QStringLiteral("#1a1b2680");
    QTest::newRow("clear") << QColor(0xff, 0xff, 0xff, 0) << QStringLiteral("#ffffff00");
}

void TestThemeRoles::hexColor()
{
    QFETCH(QColor, colour);
    QFETCH(QString, hex);
    QCOMPARE(Theme::hexColor(colour), hex);
}

void TestThemeRoles::rolesAreThoseThemeResolved()
{
    ThemeReader reader; // XDG_STATE_HOME and XDG_CONFIG_HOME, as main() builds it
    Theme theme(&reader, noHyprland());
    const QJsonObject roles = Theme::rolesJson(theme);

    QCOMPARE(roles.value(QStringLiteral("theme")).toString(), QStringLiteral("fixture-night"));
    QCOMPARE(roles.value(QStringLiteral("mode")).toString(), QStringLiteral("dark"));
    QCOMPARE(roles.value(QStringLiteral("source")).toString(),
             m_dir.filePath(QStringLiteral("state/omarchy/current/theme/colors.toml")));

    const QJsonObject colors = roles.value(QStringLiteral("colors")).toObject();
    QCOMPARE(colors.value(QStringLiteral("background")).toString(), QStringLiteral("#1a1b26"));
    QCOMPARE(colors.value(QStringLiteral("foreground")).toString(), QStringLiteral("#a9b1d6"));
    QCOMPARE(colors.value(QStringLiteral("accent")).toString(), QStringLiteral("#7aa2f7"));
    // Semantic roles come through under their own names: an error is the palette's red.
    QCOMPARE(colors.value(QStringLiteral("stateError")).toString(), QStringLiteral("#f7768e"));
    QCOMPARE(colors.value(QStringLiteral("stateSeeding")).toString(), QStringLiteral("#9ece6a"));

    // The dim foreground is the one Theme lifted to 4.5:1 (Q19), not the theme's #565f89.
    const QString dim = colors.value(QStringLiteral("dimForeground")).toString();
    QCOMPARE(dim, theme.dimForeground().name());
    QVERIFY(dim != QStringLiteral("#565f89"));
    QCOMPARE(colors.value(QStringLiteral("stateStopped")).toString(), dim);

    const QJsonObject alphas = roles.value(QStringLiteral("alphas")).toObject();
    QCOMPARE(alphas.value(QStringLiteral("fillIdle")).toDouble(), 0.04);
    QCOMPARE(alphas.value(QStringLiteral("fillHover")).toDouble(), 0.1);
    QCOMPARE(alphas.value(QStringLiteral("hairlineAlpha")).toDouble(), 0.12);
    QCOMPARE(alphas.value(QStringLiteral("faintAlpha")).toDouble(), 0.4);
    QCOMPARE(roles.value(QStringLiteral("windowAlpha")).toDouble(), 0.9);
    QCOMPARE(alphas.value(QStringLiteral("windowAlpha")).toDouble(), 0.9);
}

/// A role added to Theme lands in the dump without anyone touching rolesJson().
void TestThemeRoles::everyColourAndAlphaRoleIsThere()
{
    ThemeReader reader;
    Theme theme(&reader, noHyprland());
    const QJsonObject roles = Theme::rolesJson(theme);
    const QJsonObject colors = roles.value(QStringLiteral("colors")).toObject();
    const QJsonObject alphas = roles.value(QStringLiteral("alphas")).toObject();

    int colourRoles = 0;
    int alphaRoles = 0;
    const QMetaObject &meta = Theme::staticMetaObject;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
        const QMetaProperty property = meta.property(i);
        const QString name = QString::fromLatin1(property.name());
        if (property.metaType().id() == QMetaType::QColor) {
            ++colourRoles;
            QVERIFY2(colors.contains(name), qPrintable(name));
            QCOMPARE(colors.value(name).toString(),
                     Theme::hexColor(property.read(&theme).value<QColor>()));
        } else if (property.metaType().id() == QMetaType::Double &&
                   name != QLatin1String("spaceScale")) { // a size factor (#74)
            ++alphaRoles;
            QVERIFY2(alphas.contains(name), qPrintable(name));
        }
    }
    // 18 palette roles and 7 state roles; the shell's 7 alphas, the spec's 4, the ground's 2,
    // C30's cap and three tints.
    QCOMPARE(colourRoles, 25);
    QCOMPARE(colors.size(), colourRoles);
    QCOMPARE(alphaRoles, 17);
    QCOMPARE(alphas.size(), alphaRoles);
}

/// docs/DECISIONS.md C30 F7: every role on every ground, as `Theme.on` resolved them, so
/// contrast-board.py --check measures the app's colours, not its own idea of them.
void TestThemeRoles::groundsAreThoseThemeResolved()
{
    ThemeReader reader;
    Theme theme(&reader, noHyprland());
    const QJsonObject grounds = Theme::rolesJson(theme).value(QStringLiteral("grounds")).toObject();
    QCOMPARE(grounds.size(), theme.on()->all().size());
    for (const auto *ink : theme.on()->all()) {
        const QJsonObject ground = grounds.value(ink->name()).toObject();
        QCOMPARE(ground.value(QStringLiteral("alpha")).toDouble(), ink->alpha());
        QCOMPARE(ground.value(QStringLiteral("ground")).toString(), Theme::hexColor(ink->ground()));
        QCOMPARE(ground.value(QStringLiteral("fill")).toString(), Theme::hexColor(ink->fill()));
        const QJsonObject colors = ground.value(QStringLiteral("colors")).toObject();
        QCOMPARE(colors.size(), int(omaseed::integration::ThemeInk::RoleCount));
        QCOMPARE(colors.value(QStringLiteral("dimForeground")).toString(),
                 Theme::hexColor(ink->dimForeground()));
    }
    // The fixture's hover fill is the shell's 0.1, and the cursor's ground is the same one.
    QCOMPARE(grounds.value(QStringLiteral("hover")).toObject().value(QStringLiteral("alpha")),
             QJsonValue(0.1));
    QCOMPARE(grounds.value(QStringLiteral("cursor")).toObject().value(QStringLiteral("ground")),
             grounds.value(QStringLiteral("hover")).toObject().value(QStringLiteral("ground")));
}

QTEST_MAIN(TestThemeRoles)

#include "tst_themeroles.moc"
