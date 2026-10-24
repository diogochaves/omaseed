#include "integration/DesktopSkin.hpp"
#include "integration/SkinReader.hpp"
#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using omaseed::integration::DesktopSkin;
using omaseed::integration::SkinReader;
using omaseed::integration::Theme;
using omaseed::integration::ThemeReader;

namespace {

using Text = std::optional<QByteArray>;

constexpr char kGoodSkin[] = R"({"skin": "baseline", "version": 1, "cut": {"panel": 9}})";

} // namespace

/// Settings › Appearance's "Follow the desktop" (docs/DECISIONS.md C24 H11, #171 step 2): the
/// rule, read from the files omarchy-console's kit reads, followed live, and the reader's
/// choice that turns it into the skin Theme draws. Every file lives in a temporary folder.
class TestDesktopSkin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void rule_data();
    void rule();
    void readsTheFilesUnderPaths();
    void followsTheFilesAsTheyAreRenamed();

    void choiceReadsAsOmarchyUnlessKnown();
    void followingTheDesktopDrawsConsoleWhileItsSkinIsOn();
    void systemFontsOnlyWhileFollowing();
    void environmentWinsOverTheChoice();
    void themeRepaintsOnASwitch();

    void missingFontsAreNamed();

private:
    ThemeReader::Paths paths() const;
    QString stateDir() const { return m_dir->path() + QStringLiteral("/state/omarchy/current"); }
    QString configDir() const { return m_dir->path() + QStringLiteral("/config/omarchy"); }
    /// Written beside and renamed over, as omarchy-console's panel writes console.json.
    void writeByRename(const QString &path, const QByteArray &text) const;

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestDesktopSkin::init()
{
    // The reader honours OMASEED_SKIN, which ctest or a walk may have set.
    qunsetenv("OMASEED_SKIN");
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    QDir().mkpath(stateDir() + QStringLiteral("/theme"));
    QDir().mkpath(configDir());
}

void TestDesktopSkin::cleanup()
{
    qunsetenv("OMASEED_SKIN");
    m_dir.reset();
}

ThemeReader::Paths TestDesktopSkin::paths() const
{
    ThemeReader::Paths p;
    p.stateCurrentDir = stateDir();
    p.configOmarchyDir = configDir();
    return p;
}

void TestDesktopSkin::writeByRename(const QString &path, const QByteArray &text) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString next = path + QStringLiteral(".next");
    QFile file(next);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(next));
    file.write(text);
    file.close();
    QFile::remove(path);
    QVERIFY(QFile::rename(next, path));
}

void TestDesktopSkin::rule_data()
{
    QTest::addColumn<Text>("theme");
    QTest::addColumn<Text>("config");
    QTest::addColumn<Text>("console");
    QTest::addColumn<bool>("on");
    QTest::addColumn<QString>("reason");
    QTest::addColumn<QString>("source");
    QTest::addColumn<bool>("systemFonts");

    const Text none;
    const Text good{kGoodSkin};
    QTest::newRow("no files") << none << none << none << false << "missing" << "" << false;
    QTest::newRow("the theme's") << good << none << none << true << "" << "theme" << false;
    QTest::newRow("the config's") << none << good << none << true << "" << "config" << false;
    QTest::newRow("the theme's first, even when it fails")
        << Text{"{not json"} << good << none << false << "unparseable" << "theme" << false;
    QTest::newRow("an array") << Text{"[1]"} << none << none << false << "unparseable"
                              << "theme" << false;
    QTest::newRow("no version") << Text{R"({"skin": "x"})"} << none << none << false << "no version"
                                << "theme" << false;
    QTest::newRow("version 0") << Text{R"({"version": 0})"} << none << none << false << "no version"
                               << "theme" << false;
    QTest::newRow("a negative version")
        << Text{R"({"version": -2})"} << none << none << false << "no version" << "theme" << false;
    QTest::newRow("a fraction") << Text{R"({"version": 1.5})"} << none << none << false
                                << "no version" << "theme" << false;
    QTest::newRow("a string") << Text{R"({"version": "1"})"} << none << none << false
                              << "no version" << "theme" << false;
    // A newer file still means the desktop's skin is on: omaseed only switches to its own
    // Console and reads none of the file's values.
    QTest::newRow("a later version") << Text{R"({"version": 7})"} << none << none << true << ""
                                     << "theme" << false;
    QTest::newRow("fx.skin off") << good << none << Text{R"({"fx": {"skin": "off"}})"} << false
                                 << "off" << "theme" << false;
    QTest::newRow("fx.skin on") << good << none << Text{R"({"fx": {"skin": "on"}})"} << true << ""
                                << "theme" << false;
    QTest::newRow("fx.fonts system")
        << good << none << Text{R"({"fx": {"fonts": "system"}})"} << true << ""
        << "theme" << true;
    QTest::newRow("fx.fonts system, no skin")
        << none << none << Text{R"({"fx": {"fonts": "system"}})"} << false << "missing" << ""
        << true;
    QTest::newRow("console.json broken")
        << good << none << Text{"{"} << true << "" << "theme" << false;
}

void TestDesktopSkin::rule()
{
    QFETCH(Text, theme);
    QFETCH(Text, config);
    QFETCH(Text, console);
    const DesktopSkin::State state = DesktopSkin::resolve(theme, config, console);
    QTEST(state.on, "on");
    QTEST(state.reason, "reason");
    QTEST(state.source, "source");
    QTEST(state.systemFonts, "systemFonts");
}

void TestDesktopSkin::readsTheFilesUnderPaths()
{
    QCOMPARE(DesktopSkin::read(paths()).reason, QStringLiteral("missing"));
    writeByRename(configDir() + QStringLiteral("/console-skin.json"), kGoodSkin);
    QCOMPARE(DesktopSkin::read(paths()).source, QStringLiteral("config"));
    writeByRename(stateDir() + QStringLiteral("/theme/console-skin.json"), "{}");
    const DesktopSkin::State state = DesktopSkin::read(paths());
    QCOMPARE(state.source, QStringLiteral("theme"));
    QCOMPARE(state.on, false);
    writeByRename(configDir() + QStringLiteral("/console.json"), R"({"fx": {"fonts": "system"}})");
    QVERIFY(DesktopSkin::read(paths()).systemFonts);
}

void TestDesktopSkin::followsTheFilesAsTheyAreRenamed()
{
    DesktopSkin skin(paths());
    QSignalSpy changed(&skin, &DesktopSkin::changed);
    QVERIFY(!skin.on());

    writeByRename(stateDir() + QStringLiteral("/theme/console-skin.json"), kGoodSkin);
    QTRY_VERIFY(skin.on());
    QCOMPARE(changed.count(), 1);

    writeByRename(configDir() + QStringLiteral("/console.json"), R"({"fx": {"skin": "off"}})");
    QTRY_VERIFY(!skin.on());
    QCOMPARE(skin.state().reason, QStringLiteral("off"));

    writeByRename(configDir() + QStringLiteral("/console.json"),
                  R"({"fx": {"skin": "on", "fonts": "system"}})");
    QTRY_VERIFY(skin.on());
    QVERIFY(skin.systemFonts());

    // A theme switch renames the whole folder; the new theme has no skin.
    const QString theme = stateDir() + QStringLiteral("/theme");
    QVERIFY(QDir(theme).removeRecursively());
    QVERIFY(QDir().mkpath(theme));
    QTRY_VERIFY(!skin.on());
    QCOMPARE(skin.state().reason, QStringLiteral("missing"));
}

void TestDesktopSkin::choiceReadsAsOmarchyUnlessKnown()
{
    ThemeReader reader(paths());
    QCOMPARE(reader.skinChoice(), QStringLiteral("omarchy"));
    reader.setSkinChoice(QStringLiteral("console"));
    QCOMPARE(reader.skinChoice(), QStringLiteral("console"));
    QCOMPARE(reader.skinName(), QStringLiteral("console"));
    reader.setSkinChoice(QStringLiteral("neon"));
    QCOMPARE(reader.skinChoice(), QStringLiteral("omarchy"));
    QCOMPARE(reader.skinName(), QStringLiteral("omarchy"));
}

void TestDesktopSkin::followingTheDesktopDrawsConsoleWhileItsSkinIsOn()
{
    ThemeReader reader(paths());
    QSignalSpy skinChanged(&reader, &ThemeReader::skinChanged);

    // Off on the desktop: following draws Omarchy, which is what was drawn: no signal.
    reader.setSkinChoice(QStringLiteral("desktop"));
    QCOMPARE(reader.skinName(), QStringLiteral("omarchy"));
    QCOMPARE(skinChanged.count(), 0);

    writeByRename(configDir() + QStringLiteral("/console-skin.json"), kGoodSkin);
    QTRY_COMPARE(reader.skinName(), QStringLiteral("console"));
    QVERIFY(reader.desktopSkinOn());
    QCOMPARE(skinChanged.count(), 1);

    writeByRename(configDir() + QStringLiteral("/console.json"), R"({"fx": {"skin": "off"}})");
    QTRY_COMPARE(reader.skinName(), QStringLiteral("omarchy"));
    QCOMPARE(skinChanged.count(), 2);

    // Back on the desktop, then choosing Omarchy by hand stops following it.
    writeByRename(configDir() + QStringLiteral("/console.json"), "{}");
    QTRY_COMPARE(reader.skinName(), QStringLiteral("console"));
    reader.setSkinChoice(QStringLiteral("omarchy"));
    QCOMPARE(reader.skinName(), QStringLiteral("omarchy"));
    QVERIFY(!reader.desktopSkinOn());
    const int before = skinChanged.count();
    writeByRename(configDir() + QStringLiteral("/console.json"), R"({"fx": {"skin": "off"}})");
    QTest::qWait(300);
    QCOMPARE(skinChanged.count(), before);
}

void TestDesktopSkin::systemFontsOnlyWhileFollowing()
{
    writeByRename(stateDir() + QStringLiteral("/theme/console-skin.json"), kGoodSkin);
    writeByRename(configDir() + QStringLiteral("/console.json"), R"({"fx": {"fonts": "system"}})");
    ThemeReader reader(paths());
    reader.setSkinChoice(QStringLiteral("console"));
    QVERIFY(!reader.skinSystemFonts());

    QSignalSpy skinChanged(&reader, &ThemeReader::skinChanged);
    reader.setSkinChoice(QStringLiteral("desktop"));
    QCOMPARE(reader.skinName(), QStringLiteral("console"));
    QVERIFY(reader.skinSystemFonts());
    QCOMPARE(skinChanged.count(), 1); // same skin, its fonts moved

    writeByRename(configDir() + QStringLiteral("/console.json"), "{}");
    QTRY_VERIFY(!reader.skinSystemFonts());
}

void TestDesktopSkin::environmentWinsOverTheChoice()
{
    qputenv("OMASEED_SKIN", "console");
    ThemeReader reader(paths());
    reader.setSkinChoice(QStringLiteral("omarchy"));
    QCOMPARE(reader.skinChoiceInForce(), QStringLiteral("console"));
    QCOMPARE(reader.skinName(), QStringLiteral("console"));

    // It may name Follow the desktop too, which is then watched whatever was chosen.
    qputenv("OMASEED_SKIN", "desktop");
    ThemeReader following(paths());
    QCOMPARE(following.skinName(), QStringLiteral("omarchy"));
    writeByRename(stateDir() + QStringLiteral("/theme/console-skin.json"), kGoodSkin);
    QTRY_COMPARE(following.skinName(), QStringLiteral("console"));
}

void TestDesktopSkin::themeRepaintsOnASwitch()
{
    ThemeReader reader(paths());
    Theme theme(&reader, Theme::Hyprland{QString(), QString()});
    QCOMPARE(theme.skin()->name(), QStringLiteral("omarchy"));
    QSignalSpy changed(&theme, &Theme::changed);
    const QColor panel = theme.on()->panel()->ground();

    reader.setSkinChoice(QStringLiteral("console"));
    QCOMPARE(theme.skin()->name(), QStringLiteral("console"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(theme.on()->panel()->ground() != panel);

    // Following a desktop whose skin is on, with fx.fonts = system: same skin, fonts recorded.
    writeByRename(configDir() + QStringLiteral("/console-skin.json"), kGoodSkin);
    writeByRename(configDir() + QStringLiteral("/console.json"), R"({"fx": {"fonts": "system"}})");
    QSignalSpy skinChanged(theme.skin(), &omaseed::integration::ThemeSkin::changed);
    reader.setSkinChoice(QStringLiteral("desktop"));
    QCOMPARE(theme.skin()->name(), QStringLiteral("console"));
    QVERIFY(theme.skin()->systemFonts());
    QVERIFY(theme.skin()->missingFonts().isEmpty());
    QCOMPARE(skinChanged.count(), 1);

    // The desktop's skin goes off: Omarchy at once.
    QFile::remove(configDir() + QStringLiteral("/console-skin.json"));
    QTRY_COMPARE(theme.skin()->name(), QStringLiteral("omarchy"));
    QVERIFY(!theme.skin()->systemFonts());
    QCOMPARE(theme.on()->panel()->ground(), panel);
}

void TestDesktopSkin::missingFontsAreNamed()
{
    const auto console = SkinReader::builtIn(QStringLiteral("console"));
    const QStringList families = SkinReader::fontFamilies(console);
    QCOMPARE(families.size(), 4);
    QCOMPARE(SkinReader::missingFonts(console, {}), families);
    QStringList installed = families;
    installed[0] = installed[0].toUpper(); // matched without case
    QVERIFY(SkinReader::missingFonts(console, installed).isEmpty());
    QCOMPARE(SkinReader::missingFonts(console, families.mid(1)), families.mid(0, 1));
    // Omarchy names no family: nothing is ever missing.
    QVERIFY(SkinReader::missingFonts(SkinReader::builtIn(QStringLiteral("omarchy")), {}).isEmpty());
}

QTEST_MAIN(TestDesktopSkin)
#include "tst_desktopskin.moc"
