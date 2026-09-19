#include "integration/ThemeReader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using omaseed::integration::ColorMap;
using omaseed::integration::ThemeReader;

namespace {

/// `omarchy-theme-color`, wherever this machine keeps it. Empty when Omarchy is not here.
QString themeColorTool()
{
    const QString name = QStringLiteral("omarchy-theme-color");
    QString path = QStandardPaths::findExecutable(name);
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(name, {QStringLiteral("/usr/share/omarchy/bin")});
    return path;
}

/// Everything `--all` prints for one colors.toml, as key → value.
ColorMap runThemeColor(const QString &tool, const QString &colorsFile, bool *ok)
{
    QProcess process;
    process.start(tool, {QStringLiteral("--file"), colorsFile, QStringLiteral("--all")});
    *ok = process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit &&
          process.exitCode() == 0;

    ColorMap values;
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const qsizetype tab = line.indexOf(QLatin1Char('\t'));
        if (tab < 0)
            continue;
        values.insert(line.left(tab), line.mid(tab + 1));
    }
    return values;
}

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream stream(&file);
    return stream.readAll();
}

void write(const QString &path, const QString &text)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("cannot write %1").arg(path)));
    file.write(text.toUtf8());
}

/// Resolve a colors.toml the way ThemeReader does, from its text.
ColorMap resolveText(const QString &text, bool lightModeFile = false)
{
    return ThemeReader::resolvePalette(ThemeReader::parseToml(text), lightModeFile);
}

} // namespace

/// The resolver is only correct if it agrees with the tool the rest of the desktop uses, so
/// the main test runs `omarchy-theme-color --all` against every theme on this machine and
/// compares key by key. The rule tests below cover the branches no shipped theme takes.
class TestThemeResolver : public QObject
{
    Q_OBJECT

private slots:
    void matchesOmarchyThemeColor_data();
    void matchesOmarchyThemeColor();

    void matchesOmarchyThemeColorOnHandWrittenThemes_data();
    void matchesOmarchyThemeColorOnHandWrittenThemes();

    void parsesSectionsQuotesAndComments();
    void mixesLikeOmarchy();
    void ansiOnlyThemeGetsSemanticNames();
    void legacyShortNamesBecomeCanonical();
    void orangeFallsBackToYellowAndBrownIsDerived();
    void lightModeComesFromLuminance_data();
    void lightModeComesFromLuminance();
    void lightModeFileWins();
    void modeKeyBeatsEverything();

private:
    QTemporaryDir m_dir;
};

// -------------------------------------------------------------------------------------
// Parity with omarchy-theme-color
// -------------------------------------------------------------------------------------

void TestThemeResolver::matchesOmarchyThemeColor_data()
{
    QTest::addColumn<QString>("colorsFile");

    QStringList roots{QStringLiteral("/usr/share/omarchy/themes")};
    const QString home = QDir::homePath();
    roots << home + QStringLiteral("/.config/omarchy/themes");

    int found = 0;
    for (const QString &root : std::as_const(roots)) {
        const QStringList themes =
            QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &theme : themes) {
            const QString file = root + QLatin1Char('/') + theme + QStringLiteral("/colors.toml");
            if (!QFile::exists(file))
                continue;
            QTest::newRow(qPrintable(theme)) << file;
            ++found;
        }
    }
    if (found == 0)
        QTest::newRow("none installed") << QString();
}

void TestThemeResolver::matchesOmarchyThemeColor()
{
    QFETCH(QString, colorsFile);
    const QString tool = themeColorTool();
    if (tool.isEmpty() || colorsFile.isEmpty())
        QSKIP("omarchy-theme-color is not installed here");

    bool ok = false;
    const ColorMap expected = runThemeColor(tool, colorsFile, &ok);
    QVERIFY2(ok, "omarchy-theme-color failed");
    QVERIFY(!expected.isEmpty());

    const bool lightModeFile =
        QFile::exists(QFileInfo(colorsFile).absolutePath() + QStringLiteral("/light.mode"));
    const ColorMap actual = resolveText(readAll(colorsFile), lightModeFile);

    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        QVERIFY2(actual.contains(it.key()),
                 qPrintable(QStringLiteral("missing key %1").arg(it.key())));
        QVERIFY2(actual.value(it.key()) == it.value(),
                 qPrintable(QStringLiteral("%1: got %2, omarchy says %3")
                                .arg(it.key(), actual.value(it.key()), it.value())));
    }
    for (auto it = actual.constBegin(); it != actual.constEnd(); ++it) {
        QVERIFY2(expected.contains(it.key()),
                 qPrintable(QStringLiteral("we invented the key %1").arg(it.key())));
    }
}

void TestThemeResolver::matchesOmarchyThemeColorOnHandWrittenThemes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("lightModeFile");

    QTest::newRow("ansi only") << QStringLiteral(R"(color0 = "#101010"
color1 = "#cc2222"
color2 = "#22cc22"
color3 = "#cccc22"
color4 = "#2222cc"
color5 = "#cc22cc"
color6 = "#22cccc"
color7 = "#d0d0d0"
color8 = "#606060"
color15 = "#ffffff"
)") << false;

    QTest::newRow("legacy short names") << QStringLiteral(R"(bg = "#202030"
fg = "#c0c0d0"
dark_bg = "#101018"
darker_bg = "#08080c"
lighter_bg = "#303048"
dark_fg = "#707080"
light_fg = "#d0d0e0"
bright_fg = "#f0f0ff"
red = "#ff0000"
yellow = "#ffcc00"
green = "#00ff00"
cyan = "#00ffff"
blue = "#0000ff"
purple = "#aa00ff"
)") << false;

    QTest::newRow("two keys only") << QStringLiteral(R"(background = "#808080"
foreground = "#000000"
)") << false;

    QTest::newRow("light.mode beside a dark palette") << QStringLiteral(R"(background = "#303030"
foreground = "#e0e0e0"
accent = "#ff8800"
)") << true;

    QTest::newRow("no orange, no brown") << QStringLiteral(R"(background = "#1c1c1c"
foreground = "#dadada"
yellow = "#d7af5f"
red = "#d75f5f"
)") << false;
}

void TestThemeResolver::matchesOmarchyThemeColorOnHandWrittenThemes()
{
    QFETCH(QString, text);
    QFETCH(bool, lightModeFile);

    const QString tool = themeColorTool();
    if (tool.isEmpty())
        QSKIP("omarchy-theme-color is not installed here");
    QVERIFY(m_dir.isValid());

    const QString dir = m_dir.path() + QStringLiteral("/case");
    QDir().mkpath(dir);
    const QString colorsFile = dir + QStringLiteral("/colors.toml");
    write(colorsFile, text);
    const QString lightMarker = dir + QStringLiteral("/light.mode");
    QFile::remove(lightMarker);
    if (lightModeFile)
        write(lightMarker, QString());

    bool ok = false;
    const ColorMap expected = runThemeColor(tool, colorsFile, &ok);
    QVERIFY2(ok, "omarchy-theme-color failed");

    const ColorMap actual = resolveText(text, lightModeFile);
    QCOMPARE(actual.keys().size(), expected.keys().size());
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        QVERIFY2(actual.value(it.key()) == it.value(),
                 qPrintable(QStringLiteral("%1: got %2, omarchy says %3")
                                .arg(it.key(), actual.value(it.key()), it.value())));
    }
}

// -------------------------------------------------------------------------------------
// The rules, without the tool
// -------------------------------------------------------------------------------------

void TestThemeResolver::parsesSectionsQuotesAndComments()
{
    const QString text = QStringLiteral(R"(# a comment
mode = "dark"
background = "#101010"   # trailing comment inside quotes is ignored

[font]
base-size = 14
# body = 20
scale = 1.5  # roomier

[spacing]
scale-with-font = true
)");
    const QHash<QString, QString> values = ThemeReader::parseToml(text);

    QCOMPARE(values.value(QStringLiteral("mode")), QStringLiteral("dark"));
    QCOMPARE(values.value(QStringLiteral("background")), QStringLiteral("#101010"));
    QCOMPARE(values.value(QStringLiteral("font.base-size")), QStringLiteral("14"));
    QCOMPARE(values.value(QStringLiteral("font.scale")), QStringLiteral("1.5"));
    QCOMPARE(values.value(QStringLiteral("spacing.scale-with-font")), QStringLiteral("true"));
    QVERIFY(!values.contains(QStringLiteral("font.body")));
    QVERIFY(!values.contains(QStringLiteral("#body")));
}

void TestThemeResolver::mixesLikeOmarchy()
{
    // int(x + 0.5) rounding on each channel, and a missing colour counts as black.
    QCOMPARE(ThemeReader::mixColor(QStringLiteral("#7f7f7f"), QStringLiteral("#000000"), 0.25),
             QStringLiteral("#5f5f5f"));
    QCOMPARE(ThemeReader::mixColor(QStringLiteral("#7f7f7f"), QStringLiteral("#000000"), 0.5),
             QStringLiteral("#404040"));
    QCOMPARE(ThemeReader::mixColor(QStringLiteral("#cc2222"), QStringLiteral("#ffffff"), 0.2),
             QStringLiteral("#d64e4e"));
    QCOMPARE(ThemeReader::mixColor(QString(), QStringLiteral("#ffffff"), 0.2),
             QStringLiteral("#333333"));
}

void TestThemeResolver::ansiOnlyThemeGetsSemanticNames()
{
    const ColorMap c = resolveText(QStringLiteral(R"(color0 = "#101010"
color1 = "#cc2222"
color2 = "#22cc22"
color3 = "#cccc22"
color4 = "#2222cc"
color5 = "#cc22cc"
color6 = "#22cccc"
color7 = "#d0d0d0"
color8 = "#606060"
color15 = "#ffffff"
)"));

    QCOMPARE(c.value(QStringLiteral("background")), QStringLiteral("#101010"));
    QCOMPARE(c.value(QStringLiteral("foreground")), QStringLiteral("#d0d0d0"));
    QCOMPARE(c.value(QStringLiteral("red")), QStringLiteral("#cc2222"));
    QCOMPARE(c.value(QStringLiteral("magenta")), QStringLiteral("#cc22cc"));
    QCOMPARE(c.value(QStringLiteral("purple")), QStringLiteral("#cc22cc"));
    QCOMPARE(c.value(QStringLiteral("muted")), QStringLiteral("#606060"));
    QCOMPARE(c.value(QStringLiteral("dark_foreground")), QStringLiteral("#606060"));
    QCOMPARE(c.value(QStringLiteral("bright_foreground")), QStringLiteral("#ffffff"));
    QCOMPARE(c.value(QStringLiteral("cursor")), QStringLiteral("#ffffff"));
    QCOMPARE(c.value(QStringLiteral("selection")), QStringLiteral("#606060"));
    QCOMPARE(c.value(QStringLiteral("lighter_background")), QStringLiteral("#101010"));
    QCOMPARE(c.value(QStringLiteral("dark_background")), QStringLiteral("#0c0c0c"));
    QCOMPARE(c.value(QStringLiteral("darker_background")), QStringLiteral("#080808"));
    QCOMPARE(c.value(QStringLiteral("bright_red")), QStringLiteral("#d64e4e"));
    QCOMPARE(c.value(QStringLiteral("mode")), QStringLiteral("dark"));
}

void TestThemeResolver::legacyShortNamesBecomeCanonical()
{
    const ColorMap c = resolveText(QStringLiteral(R"(bg = "#202030"
fg = "#c0c0d0"
dark_bg = "#101018"
darker_bg = "#08080c"
lighter_bg = "#303048"
dark_fg = "#707080"
light_fg = "#d0d0e0"
bright_fg = "#f0f0ff"
purple = "#aa00ff"
)"));

    QCOMPARE(c.value(QStringLiteral("background")), QStringLiteral("#202030"));
    QCOMPARE(c.value(QStringLiteral("foreground")), QStringLiteral("#c0c0d0"));
    QCOMPARE(c.value(QStringLiteral("dark_background")), QStringLiteral("#101018"));
    QCOMPARE(c.value(QStringLiteral("darker_background")), QStringLiteral("#08080c"));
    QCOMPARE(c.value(QStringLiteral("lighter_background")), QStringLiteral("#303048"));
    QCOMPARE(c.value(QStringLiteral("dark_foreground")), QStringLiteral("#707080"));
    QCOMPARE(c.value(QStringLiteral("light_foreground")), QStringLiteral("#d0d0e0"));
    QCOMPARE(c.value(QStringLiteral("bright_foreground")), QStringLiteral("#f0f0ff"));
    QCOMPARE(c.value(QStringLiteral("magenta")), QStringLiteral("#aa00ff"));
    // …and the short names survive for consumers that still ask for them.
    QCOMPARE(c.value(QStringLiteral("bg")), QStringLiteral("#202030"));
    QCOMPARE(c.value(QStringLiteral("bright_fg")), QStringLiteral("#f0f0ff"));
}

void TestThemeResolver::orangeFallsBackToYellowAndBrownIsDerived()
{
    const ColorMap c = resolveText(QStringLiteral(R"(background = "#1c1c1c"
foreground = "#dadada"
yellow = "#d7af5f"
)"));

    QCOMPARE(c.value(QStringLiteral("orange")), QStringLiteral("#d7af5f"));
    // brown = orange mixed halfway to black.
    QCOMPARE(c.value(QStringLiteral("brown")), QStringLiteral("#6c5830"));

    // An explicit orange and brown are left alone.
    const ColorMap kept = resolveText(QStringLiteral(R"(background = "#1c1c1c"
yellow = "#d7af5f"
orange = "#ff8800"
brown = "#403020"
)"));
    QCOMPARE(kept.value(QStringLiteral("orange")), QStringLiteral("#ff8800"));
    QCOMPARE(kept.value(QStringLiteral("brown")), QStringLiteral("#403020"));
}

void TestThemeResolver::lightModeComesFromLuminance_data()
{
    QTest::addColumn<QString>("background");
    QTest::addColumn<QString>("mode");

    // The threshold is r + g + b > 382.
    QTest::newRow("just over") << QStringLiteral("#808080") << QStringLiteral("light");
    QTest::newRow("just under") << QStringLiteral("#7f7f7f") << QStringLiteral("dark");
    QTest::newRow("paper") << QStringLiteral("#fafafa") << QStringLiteral("light");
    QTest::newRow("ink") << QStringLiteral("#1a1b26") << QStringLiteral("dark");
}

void TestThemeResolver::lightModeComesFromLuminance()
{
    QFETCH(QString, background);
    QFETCH(QString, mode);

    const ColorMap c = resolveText(QStringLiteral("background = \"%1\"\n").arg(background));
    QCOMPARE(c.value(QStringLiteral("mode")), mode);
    QCOMPARE(c.value(QStringLiteral("theme_type")), mode);
}

void TestThemeResolver::lightModeFileWins()
{
    const QString text = QStringLiteral("background = \"#303030\"\nforeground = \"#e0e0e0\"\n");
    QCOMPARE(resolveText(text, false).value(QStringLiteral("mode")), QStringLiteral("dark"));
    QCOMPARE(resolveText(text, true).value(QStringLiteral("mode")), QStringLiteral("light"));
}

void TestThemeResolver::modeKeyBeatsEverything()
{
    // An explicit mode, then the legacy theme_type, then the light.mode file.
    QCOMPARE(resolveText(QStringLiteral("mode = \"light\"\nbackground = \"#000000\"\n"), true)
                 .value(QStringLiteral("mode")),
             QStringLiteral("light"));
    QCOMPARE(resolveText(QStringLiteral("mode = \"dark\"\nbackground = \"#ffffff\"\n"), true)
                 .value(QStringLiteral("mode")),
             QStringLiteral("dark"));
    QCOMPARE(resolveText(QStringLiteral("theme_type = \"dark\"\nbackground = \"#ffffff\"\n"), true)
                 .value(QStringLiteral("mode")),
             QStringLiteral("dark"));
}

QTEST_MAIN(TestThemeResolver)

#include "tst_themeresolver.moc"
