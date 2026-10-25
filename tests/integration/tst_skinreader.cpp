#include "integration/SkinReader.hpp"

#include <QSignalSpy>
#include <QTest>

using omaseed::integration::Skin;
using omaseed::integration::SkinGround;
using omaseed::integration::SkinReader;
using omaseed::integration::ThemeSkin;

/// The console skin's design note (#171, "The shape" and "Tests"): a skin file is read and
/// checked by SkinReader, all or nothing, and what it leaves out or gets wrong is Omarchy's.
class TestSkinReader : public QObject
{
    Q_OBJECT

private slots:
    void omarchyIsTheSkinWithNoOverrides();
    void consoleReadsWhole();
    void consolesGroundsAreBoard46s();
    void unknownSkinIsOmarchy();
    void requestedNameComesFromTheEnvironment();

    void fileLevelFailures_data();
    void fileLevelFailures();
    void anOlderVersionIsRead();
    void malformedTokensAreOmarchys_data();
    void malformedTokensAreOmarchys();
    void wellFormedTokensAreKept();
    void glowIsHeldAtClean();
    void groundRecipes();
    void shellMeansTheShellsAlpha();
    void aGroundOverItselfIsOmarchys();
    void groundsResolveAfterWhatTheyArePaintedOver();

    void themeSkinLooksTokensUp();
    void themeSkinSignalsOnlyOnAChange();
};

void TestSkinReader::omarchyIsTheSkinWithNoOverrides()
{
    QCOMPARE(SkinReader::builtInNames().first(), QStringLiteral("omarchy"));
    const Skin skin = SkinReader::builtIn(QStringLiteral("omarchy"));
    QVERIFY2(skin.drawn(), qPrintable(skin.reason));
    QCOMPARE(skin.name, QStringLiteral("omarchy"));
    QCOMPARE(skin.version, 1);
    QVERIFY(skin.dropped.isEmpty());
    QVERIFY(skin.grounds.isEmpty());
    QVERIFY(skin.tokens.isEmpty());
    QCOMPARE(skin.roles, SkinReader::omarchyRoles());
    QCOMPARE(SkinReader::groundsOf(skin), SkinReader::omarchyGrounds());
}

/// Console's file is read whole: every token it carries is in the right shape.
void TestSkinReader::consoleReadsWhole()
{
    const Skin skin = SkinReader::builtIn(QStringLiteral("console"));
    QVERIFY2(skin.drawn(), qPrintable(skin.reason));
    QCOMPARE(skin.name, QStringLiteral("console"));
    QCOMPARE(skin.version, SkinReader::kVersion);
    QVERIFY2(skin.dropped.isEmpty(), qPrintable(skin.dropped.join(QStringLiteral(", "))));
    for (const char *group : {"line",
                              "cut",
                              "radius",
                              "font",
                              "label",
                              "numerals",
                              "density",
                              "glyphs",
                              "labels",
                              "select",
                              "glow"})
        QVERIFY2(skin.tokens.contains(QLatin1String(group)), group);
    QCOMPARE(skin.tokens.value(QStringLiteral("glow")).toString(), QStringLiteral("clean"));
    QCOMPARE(skin.roles.value(QStringLiteral("signal")), QStringLiteral("cyan"));
    // Every glyph names a ligature, so a missing icon font falls back per glyph (step 3).
    const QVariantMap glyphs = skin.tokens.value(QStringLiteral("glyphs")).toMap();
    for (auto it = glyphs.constBegin(); it != glyphs.constEnd(); ++it)
        QVERIFY2(it.value().toString().startsWith(QLatin1String("ms:")), qPrintable(it.key()));
}

/// Board 46's `.sk-con` tiers as C30 grounds: the panel is the lighter background at 45 %
/// over the window, the selected and cursor rows the lighter background itself, a picked
/// row the accent at 20 %.
void TestSkinReader::consolesGroundsAreBoard46s()
{
    const Skin skin = SkinReader::builtIn(QStringLiteral("console"));
    const auto ground = [&](const char *name) { return skin.grounds.value(QLatin1String(name)); };
    const SkinGround panel{
        QStringLiteral("lighterBackground"), 0.45, false, QStringLiteral("window")};
    QCOMPARE(ground("panel"), panel);
    QCOMPARE(ground("hover"), panel);
    QCOMPARE(ground("selected").tint, QStringLiteral("lighterBackground"));
    QCOMPARE(ground("selected").alpha, 1.0);
    QCOMPARE(ground("cursor"), ground("selected"));
    QCOMPARE(ground("picked").tint, QStringLiteral("accent"));
    QCOMPARE(ground("picked").alpha, 0.2);
    const QVariantMap line = skin.tokens.value(QStringLiteral("line")).toMap();
    QCOMPARE(line.value(QStringLiteral("hairAlpha")).toDouble(), 0.21);
    QCOMPARE(line.value(QStringLiteral("menuEdgeRole")).toString(), QStringLiteral("hair"));
}

void TestSkinReader::unknownSkinIsOmarchy()
{
    QCOMPARE(SkinReader::builtInPath(QStringLiteral("neon")), QString());
    const Skin skin = SkinReader::builtIn(QStringLiteral("neon"));
    QCOMPARE(skin.reason, QStringLiteral("unknown skin"));
    QCOMPARE(skin.name, QStringLiteral("omarchy"));
    QVERIFY(skin.tokens.isEmpty());
    QCOMPARE(skin.roles, SkinReader::omarchyRoles());
}

void TestSkinReader::requestedNameComesFromTheEnvironment()
{
    const QByteArray before = qgetenv("OMASEED_SKIN");
    qunsetenv("OMASEED_SKIN");
    QCOMPARE(SkinReader::requestedName(), QStringLiteral("omarchy"));
    qputenv("OMASEED_SKIN", "console");
    QCOMPARE(SkinReader::requestedName(), QStringLiteral("console"));
    if (before.isNull())
        qunsetenv("OMASEED_SKIN");
    else
        qputenv("OMASEED_SKIN", before);
}

void TestSkinReader::fileLevelFailures_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("reason");
    QTest::addColumn<int>("version");
    QTest::newRow("not json") << QByteArray("{ skin: console") << "unparseable" << 0;
    QTest::newRow("an array") << QByteArray("[1]") << "unparseable" << 0;
    QTest::newRow("empty") << QByteArray("") << "unparseable" << 0;
    QTest::newRow("no version") << QByteArray(R"({"skin": "x"})") << "no version" << 0;
    QTest::newRow("version 0") << QByteArray(R"({"version": 0})") << "no version" << 0;
    QTest::newRow("version 1.5") << QByteArray(R"({"version": 1.5})") << "no version" << 0;
    QTest::newRow("version as text") << QByteArray(R"({"version": "1"})") << "no version" << 0;
    QTest::newRow("newer") << QByteArray(R"({"version": 2, "radius": 3})") << "newer" << 2;
}

/// A file that doesn't read, or is newer than omaseed, is drawn as Omarchy, whole, with the
/// reason why (omarchy-console's kitReads rule): never half of it.
void TestSkinReader::fileLevelFailures()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, reason);
    QFETCH(int, version);
    const Skin skin = SkinReader::parse(json);
    QCOMPARE(skin.reason, reason);
    QVERIFY(!skin.drawn());
    QCOMPARE(skin.version, version);
    QCOMPARE(skin.name, QStringLiteral("omarchy"));
    QVERIFY(skin.tokens.isEmpty());
    QVERIFY(skin.grounds.isEmpty());
    QCOMPARE(skin.roles, SkinReader::omarchyRoles());
}

void TestSkinReader::anOlderVersionIsRead()
{
    const Skin skin = SkinReader::parse(R"({"skin": "old", "version": 1, "radius": 4})", 3);
    QVERIFY(skin.drawn());
    QCOMPARE(skin.version, 1);
    QCOMPARE(skin.tokens.value(QStringLiteral("radius")).toInt(), 4);
}

void TestSkinReader::malformedTokensAreOmarchys_data()
{
    QTest::addColumn<QByteArray>("tokens");
    QTest::addColumn<QString>("dropped");
    QTest::newRow("a group not an object") << QByteArray(R"("line": 2)") << "line";
    QTest::newRow("unknown group") << QByteArray(R"("halo": {"x": 1})") << "halo";
    QTest::newRow("unknown key") << QByteArray(R"("line": {"thick": 1})") << "line.thick";
    QTest::newRow("width not integer") << QByteArray(R"("line": {"hair": 1.5})") << "line.hair";
    QTest::newRow("width too wide") << QByteArray(R"("line": {"hair": 40})") << "line.hair";
    QTest::newRow("hex for a role")
        << QByteArray(R"("line": {"litRole": "#ff0000"})") << "line.litRole";
    QTest::newRow("alpha over 1") << QByteArray(R"("line": {"hairAlpha": 1.2})")
                                  << "line.hairAlpha";
    QTest::newRow("negative cut") << QByteArray(R"("cut": {"panel": -4})") << "cut.panel";
    QTest::newRow("radius word") << QByteArray(R"("radius": "round")") << "radius";
    QTest::newRow("family not text") << QByteArray(R"("font": {"label": 3})") << "font.label";
    QTest::newRow("case") << QByteArray(R"("label": {"case": "title"})") << "label.case";
    QTest::newRow("weight") << QByteArray(R"("label": {"weight": 950})") << "label.weight";
    QTest::newRow("kana not bool") << QByteArray(R"("label": {"kana": "yes"})") << "label.kana";
    QTest::newRow("title case") << QByteArray(R"("label": {"titleCase": "lower"})")
                                << "label.titleCase";
    QTest::newRow("control size") << QByteArray(R"("label": {"controlSize": "huge"})")
                                  << "label.controlSize";
    QTest::newRow("tint") << QByteArray(R"("tint": {"notice": 2})") << "tint.notice";
    QTest::newRow("tags not bool") << QByteArray(R"("label": {"tags": 1})") << "label.tags";
    QTest::newRow("caption track")
        << QByteArray(R"("label": {"captionTrack": 3})") << "label.captionTrack";
    QTest::newRow("row case") << QByteArray(R"("label": {"rowCase": "lower"})") << "label.rowCase";
    QTest::newRow("card edge") << QByteArray(R"("line": {"card": 12})") << "line.card";
    QTest::newRow("cursor mark not bool")
        << QByteArray(R"("select": {"cursorMark": "yes"})") << "select.cursorMark";
    QTest::newRow("numerals") << QByteArray(R"("numerals": 2)") << "numerals";
    QTest::newRow("density text") << QByteArray(R"("density": {"railWidth": "wide"})")
                                  << "density.railWidth";
    QTest::newRow("density name") << QByteArray(R"("density": {"rail width": 3})")
                                  << "density.rail width";
    QTest::newRow("empty glyph") << QByteArray(R"("glyphs": {"add": ""})") << "glyphs.add";
    QTest::newRow("bad ligature") << QByteArray(R"("glyphs": {"add": "ms:Add Box"})")
                                  << "glyphs.add";
    QTest::newRow("empty label") << QByteArray(R"("labels": {"library": ""})") << "labels.library";
    QTest::newRow("face not a seam")
        << QByteArray(R"("faces": {"Window": "Window.qml"})") << "faces.Window";
    QTest::newRow("face out of faces/")
        << QByteArray(R"("faces": {"Plate": "../Plate.qml"})") << "faces.Plate";
    QTest::newRow("role to hex") << QByteArray(R"("roles": {"signal": "#00ffff"})")
                                 << "roles.signal";
    QTest::newRow("unknown role") << QByteArray(R"("roles": {"glow": "cyan"})") << "roles.glow";
    QTest::newRow("ground not a recipe")
        << QByteArray(R"("grounds": {"panel": 0.4})") << "grounds.panel";
    QTest::newRow("unknown ground")
        << QByteArray(R"("grounds": {"window": {"tint": "foreground", "alpha": 0.1}})")
        << "grounds.window";
    QTest::newRow("ground in hex")
        << QByteArray(R"("grounds": {"panel": {"tint": "#202020", "alpha": 1}})")
        << "grounds.panel";
    QTest::newRow("ground alpha")
        << QByteArray(R"("grounds": {"panel": {"tint": "foreground", "alpha": 2}})")
        << "grounds.panel";
    QTest::newRow("ground without alpha")
        << QByteArray(R"("grounds": {"panel": {"tint": "foreground"}})") << "grounds.panel";
    QTest::newRow("over unknown")
        << QByteArray(
               R"("grounds": {"panel": {"tint": "foreground", "alpha": 0.1, "over": "sky"}})")
        << "grounds.panel";
    QTest::newRow("over itself")
        << QByteArray(
               R"("grounds": {"panel": {"tint": "foreground", "alpha": 0.1, "over": "panel"}})")
        << "grounds.panel";
    QTest::newRow("ground extra key")
        << QByteArray(R"("grounds": {"panel": {"tint": "foreground", "alpha": 0.1, "blur": 3}})")
        << "grounds.panel";
}

/// A token in the wrong shape is Omarchy's (left out of the set) and named in `dropped`; the
/// rest of the file, here its glow, is still read.
void TestSkinReader::malformedTokensAreOmarchys()
{
    QFETCH(QByteArray, tokens);
    QFETCH(QString, dropped);
    const Skin skin =
        SkinReader::parse(R"({"skin": "t", "version": 1, "glow": "clean", )" + tokens + "}");
    QVERIFY2(skin.drawn(), qPrintable(skin.reason));
    QCOMPARE(skin.dropped, QStringList{dropped});
    QCOMPARE(skin.tokens.value(QStringLiteral("glow")).toString(), QStringLiteral("clean"));
    const QString group = dropped.section(QLatin1Char('.'), 0, 0);
    if (group == QLatin1String("grounds"))
        QVERIFY(skin.grounds.isEmpty());
    else if (group == QLatin1String("roles"))
        QCOMPARE(skin.roles, SkinReader::omarchyRoles());
    else
        QVERIFY(!skin.tokens.contains(group));
}

void TestSkinReader::wellFormedTokensAreKept()
{
    const Skin skin = SkinReader::parse(R"({
        "skin": "t", "version": 1,
        "roles": {"signal": "blue"},
        "line": {"hair": 0, "menuEdgeRole": "faint", "litRole": "signal"},
        "cut": {"panel": 0},
        "radius": "hyprland",
        "font": {"label": ""},
        "label": {"case": "none", "track": 0, "weight": 100, "kana": false},
        "numerals": "plain",
        "density": {"titleBarHeight": 54.5},
        "glyphs": {"add": "+", "help": "icon:help", "search": "ms:search"},
        "labels": {"library": "ライブラリ"},
        "faces": {"Plate": "Plate.qml", "SelectionMark": "Brackets.qml"}
    })");
    QVERIFY2(skin.dropped.isEmpty(), qPrintable(skin.dropped.join(QStringLiteral(", "))));
    QCOMPARE(skin.roles.value(QStringLiteral("signal")), QStringLiteral("blue"));
    QCOMPARE(skin.roles.value(QStringLiteral("alert")), QStringLiteral("red"));
    QCOMPARE(skin.tokens.value(QStringLiteral("radius")).toString(), QStringLiteral("hyprland"));
    QCOMPARE(skin.tokens.value(QStringLiteral("density"))
                 .toMap()
                 .value(QStringLiteral("titleBarHeight")),
             QVariant(54.5));
    QCOMPARE(skin.tokens.value(QStringLiteral("faces")).toMap().size(), 2);
    QCOMPARE(skin.tokens.value(QStringLiteral("font")).toMap().value(QStringLiteral("label")),
             QVariant(QString()));
    QCOMPARE(skin.tokens.value(QStringLiteral("line")).toMap().value(QStringLiteral("hair")),
             QVariant(0));
}

/// C24 H12: clean glow only. Any other glow is held at "clean" and named as dropped.
void TestSkinReader::glowIsHeldAtClean()
{
    for (const QByteArray &glow : {QByteArray(R"("neon")"), QByteArray("12"), QByteArray("{}")}) {
        const Skin skin = SkinReader::parse(R"({"version": 1, "glow": )" + glow + "}");
        QVERIFY(skin.drawn());
        QCOMPARE(skin.tokens.value(QStringLiteral("glow")).toString(), QStringLiteral("clean"));
        QCOMPARE(skin.dropped, QStringList{QStringLiteral("glow")});
    }
    const Skin clean = SkinReader::parse(R"({"version": 1, "glow": "clean"})");
    QVERIFY(clean.dropped.isEmpty());
}

void TestSkinReader::groundRecipes()
{
    const Skin skin = SkinReader::parse(R"({"version": 1, "grounds": {
        "panel": {"tint": "lighterBackground", "alpha": 0.45},
        "blockIdle": {"tint": "foreground", "alpha": 0.06, "over": "panel"},
        "warning": {"tint": "warn", "alpha": 0.09, "over": "window"}
    }})");
    QVERIFY2(skin.dropped.isEmpty(), qPrintable(skin.dropped.join(QStringLiteral(", "))));
    QCOMPARE(skin.grounds.size(), 3);
    QCOMPARE(skin.grounds.value(QStringLiteral("panel")).over, QStringLiteral("window"));
    QCOMPARE(skin.grounds.value(QStringLiteral("blockIdle")).over, QStringLiteral("panel"));
    QCOMPARE(skin.grounds.value(QStringLiteral("warning")).tint, QStringLiteral("warn"));
    const auto all = SkinReader::groundsOf(skin);
    QCOMPARE(all.size(), SkinReader::groundNames().size());
    QCOMPARE(all.value(QStringLiteral("blockIdle")).alpha, 0.06);
    // What the skin left alone is Omarchy's.
    QCOMPARE(all.value(QStringLiteral("selected")),
             SkinReader::omarchyGrounds().value(QStringLiteral("selected")));
}

void TestSkinReader::shellMeansTheShellsAlpha()
{
    const Skin skin = SkinReader::parse(
        R"({"version": 1, "grounds": {"hover": {"tint": "accent", "alpha": "shell"}}})");
    const SkinGround hover = skin.grounds.value(QStringLiteral("hover"));
    QVERIFY(hover.shellAlpha);
    QCOMPARE(hover.tint, QStringLiteral("accent"));
    for (const SkinGround &ground : SkinReader::omarchyGrounds())
        QVERIFY(ground.shellAlpha);
    QVERIFY(skin.dropped.isEmpty());
    const Skin word = SkinReader::parse(
        R"({"version": 1, "grounds": {"hover": {"tint": "accent", "alpha": "shelly"}}})");
    QCOMPARE(word.dropped, QStringList{QStringLiteral("grounds.hover")});
}

/// A recipe whose chain of `over` comes back to itself is Omarchy's, so every chain ends on
/// the window: idle over blockIdle, which Omarchy paints over idle.
void TestSkinReader::aGroundOverItselfIsOmarchys()
{
    const Skin skin = SkinReader::parse(R"({"version": 1, "grounds": {
        "idle": {"tint": "foreground", "alpha": 0.05, "over": "blockIdle"},
        "panel": {"tint": "foreground", "alpha": 0.05, "over": "picked"},
        "picked": {"tint": "accent", "alpha": 0.2, "over": "panel"}
    }})");
    const auto all = SkinReader::groundsOf(skin);
    for (const QString &start : SkinReader::groundNames()) {
        QString at = start;
        for (int hops = 0; at != QLatin1String("window"); ++hops) {
            QVERIFY2(hops <= SkinReader::groundNames().size(), qPrintable(start));
            at = all.value(at).over;
        }
    }
    QCOMPARE(all.value(QStringLiteral("idle")),
             SkinReader::omarchyGrounds().value(QStringLiteral("idle")));
    // Of the panel–picked loop one goes back, the other keeps its recipe.
    const bool panelKept = all.value(QStringLiteral("panel")).over == QLatin1String("picked");
    const bool pickedKept = all.value(QStringLiteral("picked")).over == QLatin1String("panel");
    QVERIFY(panelKept != pickedKept);
}

void TestSkinReader::groundsResolveAfterWhatTheyArePaintedOver()
{
    const Skin skin = SkinReader::parse(R"({"version": 1, "grounds": {
        "idle": {"tint": "foreground", "alpha": 0.05, "over": "panel"},
        "panel": {"tint": "lighterBackground", "alpha": 0.45}
    }})");
    const auto all = SkinReader::groundsOf(skin);
    const QStringList order = SkinReader::groundOrder(all);
    QCOMPARE(order.size(), SkinReader::groundNames().size());
    for (const QString &name : order) {
        const QString over = all.value(name).over;
        if (over != QLatin1String("window"))
            QVERIFY2(order.indexOf(over) < order.indexOf(name), qPrintable(name));
    }
}

void TestSkinReader::themeSkinLooksTokensUp()
{
    ThemeSkin themeSkin;
    QCOMPARE(themeSkin.name(), QStringLiteral("omarchy"));
    QCOMPARE(themeSkin.n(QStringLiteral("titleBarHeight"), 46), QVariant(46));
    QCOMPARE(themeSkin.value(QStringLiteral("line.hair"), 1), QVariant(1));
    QCOMPARE(themeSkin.glyph(QStringLiteral("add"), QStringLiteral("+")), QStringLiteral("+"));

    themeSkin.set(SkinReader::builtIn(QStringLiteral("console")), QStringLiteral("console"));
    QCOMPARE(themeSkin.name(), QStringLiteral("console"));
    QCOMPARE(themeSkin.requested(), QStringLiteral("console"));
    QCOMPARE(themeSkin.n(QStringLiteral("titleBarHeight"), 46).toDouble(), 58.0);
    QCOMPARE(themeSkin.n(QStringLiteral("noSuchToken"), 7), QVariant(7));
    QCOMPARE(themeSkin.value(QStringLiteral("line.menuEdgeRole"), QString()).toString(),
             QStringLiteral("hair"));
    QCOMPARE(themeSkin.value(QStringLiteral("cut.panel"), 0).toInt(), 16);
    QCOMPARE(themeSkin.value(QStringLiteral("radius"), QStringLiteral("hyprland")).toInt(), 0);
    QCOMPARE(themeSkin.value(QStringLiteral("radius.x"), 5), QVariant(5));
    QCOMPARE(themeSkin.glyph(QStringLiteral("search"), QString()), QStringLiteral("ms:search"));
    QCOMPARE(themeSkin.label(QStringLiteral("library"), QString()), QStringLiteral("ライブラリ"));
    QCOMPARE(themeSkin.roles().value(QStringLiteral("signal")).toString(), QStringLiteral("cyan"));
}

void TestSkinReader::themeSkinSignalsOnlyOnAChange()
{
    ThemeSkin themeSkin;
    QSignalSpy changed(&themeSkin, &ThemeSkin::changed);
    const Skin console = SkinReader::builtIn(QStringLiteral("console"));
    themeSkin.set(console, QStringLiteral("console"));
    themeSkin.set(console, QStringLiteral("console"));
    QCOMPARE(changed.count(), 1);
    themeSkin.set(SkinReader::builtIn(QStringLiteral("neon")), QStringLiteral("neon"));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(themeSkin.reason(), QStringLiteral("unknown skin"));
    QCOMPARE(themeSkin.requested(), QStringLiteral("neon"));
}

QTEST_MAIN(TestSkinReader)
#include "tst_skinreader.moc"
