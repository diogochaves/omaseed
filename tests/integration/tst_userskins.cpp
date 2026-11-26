#include "integration/Theme.hpp"
#include "integration/ThemeReader.hpp"
#include "integration/UserSkins.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>

using omaseed::integration::Skin;
using omaseed::integration::SkinReader;
using omaseed::integration::Theme;
using omaseed::integration::ThemeReader;
using omaseed::integration::UserSkin;
using omaseed::integration::UserSkins;

/// DECISIONS C37 U1, U2: the person's skins, one folder each under skins/, named by the
/// folder, held to a name rule, read as the built-ins are.
class TestUserSkins : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listsAfterTheBuiltIns();
    void aBuiltInNameIsIgnored();
    void oddNames_data();
    void oddNames();
    void aFacesFolderIsDropped();
    void aNewerVersionIsNamed();
    void noSkinJsonIsMissing();
    void aUserFileSkinKeyIsIgnored();

    // The watch (C37 U1): a save repaints, live.
    void aSaveRepaints();
    void aRenameOverSaveRepaints();
    void aBrokenSaveDrawsOmarchyThenBack();
    void savedTwiceDrawsTheSecond();
    void aRewriteThatReadsTheSameIsSilent();
    void aNewFolderIsListed();
    void theSkinsAccentIsTheAccent();

private:
    /// A reader on the temporary folder, its skins folder m_dir, drawing `choice`.
    std::unique_ptr<ThemeReader> reader(const QString &choice) const;
    /// Writes `json` to skins/<name>/skin.json.new, then renames it over skin.json, as an
    /// editor saves.
    void saveByRename(const QString &name, const QByteArray &json);
    /// Makes `skins/<name>/skin.json` with `json` (no file when `json` is null).
    void put(const QString &name, const QByteArray &json);
    QStringList names() const;

    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_dir;
};

void TestUserSkins::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    m_dir = m_tmp->filePath(QStringLiteral("skins"));
    QVERIFY(QDir().mkpath(m_dir));
}

void TestUserSkins::cleanup()
{
    m_tmp.reset();
}

void TestUserSkins::put(const QString &name, const QByteArray &json)
{
    const QString folder = m_dir + QLatin1Char('/') + name;
    QVERIFY(QDir().mkpath(folder));
    if (json.isNull())
        return;
    QFile file(folder + QStringLiteral("/skin.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json);
}

QStringList TestUserSkins::names() const
{
    QStringList list;
    for (const UserSkin &skin : UserSkins(m_dir).list())
        list << skin.name;
    return list;
}

/// The folder holds only the person's skins, sorted without case; the built-ins are read by
/// their names and listed first by whoever lists (the folder never has them).
void TestUserSkins::listsAfterTheBuiltIns()
{
    put(QStringLiteral("warm"), R"({"version": 1, "description": "Warmer and rounder."})");
    put(QStringLiteral("Paper"), R"({"version": 1})");
    put(QStringLiteral("amber"), R"({"version": 1, "radius": 4})");
    const QList<UserSkin> skins = UserSkins(m_dir).list();
    QCOMPARE(names(), (QStringList{"amber", "Paper", "warm"}));
    QCOMPARE(skins.last().description, QStringLiteral("Warmer and rounder."));
    QCOMPARE(skins.last().path, m_dir + QStringLiteral("/warm"));
    QVERIFY(skins.first().skin.drawn());
    QCOMPARE(skins.first().skin.tokens.value(QStringLiteral("radius")).toInt(), 4);

    const UserSkins folder(m_dir);
    QCOMPARE(folder.read(QStringLiteral("console")),
             SkinReader::builtIn(QStringLiteral("console")));
    QCOMPARE(folder.read(QStringLiteral("omarchy")),
             SkinReader::builtIn(QStringLiteral("omarchy")));
    QCOMPARE(folder.read(QStringLiteral("warm")).description,
             QStringLiteral("Warmer and rounder."));
}

/// A folder named like a built-in skin or Follow the desktop is never a user skin, in any
/// case: the built-in's name reads the built-in.
void TestUserSkins::aBuiltInNameIsIgnored()
{
    for (const char *name : {"omarchy", "Console", "desktop"})
        put(QLatin1String(name), R"({"version": 1, "radius": 9})");
    put(QStringLiteral("warm"), R"({"version": 1})");
    QCOMPARE(names(), QStringList{QStringLiteral("warm")});
    QCOMPARE(UserSkins::nameProblem(QStringLiteral("Console")),
             QStringLiteral("named like a built-in skin"));
    QCOMPARE(UserSkins::nameProblem(QStringLiteral("OMARCHY")),
             QStringLiteral("named like a built-in skin"));
    QCOMPARE(UserSkins::nameProblem(QStringLiteral("desktop")),
             QStringLiteral("desktop is Follow the desktop"));
    const UserSkins folder(m_dir);
    QVERIFY(!folder.read(QStringLiteral("omarchy")).tokens.contains(QStringLiteral("radius")));
    QCOMPARE(folder.read(QStringLiteral("Console")).reason, QStringLiteral("missing"));
    QCOMPARE(folder.read(QStringLiteral("desktop")).reason, QStringLiteral("missing"));
}

void TestUserSkins::oddNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("problem");
    QTest::newRow("dots") << "warm.v2" << "";
    QTest::newRow("many dots") << "a.b.c" << "";
    QTest::newRow("dash") << "console-2" << "";
    QTest::newRow("underscore") << "my_skin" << "";
    QTest::newRow("63 characters") << QString(63, QLatin1Char('a')) << "";
    QTest::newRow("hidden") << ".hidden" << "hidden";
    QTest::newRow("space") << "my skin" << "a skin's name is letters, digits, . _ and -";
    QTest::newRow("not ascii") << "ünï" << "a skin's name is letters, digits, . _ and -";
    QTest::newRow("64 characters") << QString(64, QLatin1Char('a')) << "longer than 63 characters";
    QTest::newRow("trailing dot") << "x." << "a skin's name doesn't end in .";
    QTest::newRow("leading dash") << "-x" << "a skin's name is letters, digits, . _ and -";
}

/// The name rule: what passes is listed and read, what fails is left out with why.
void TestUserSkins::oddNames()
{
    QFETCH(QString, name);
    QFETCH(QString, problem);
    QCOMPARE(UserSkins::nameProblem(name), problem);
    put(name, R"({"version": 1, "radius": 3})");
    const UserSkins folder(m_dir);
    if (problem.isEmpty()) {
        QCOMPARE(names(), QStringList{name});
        const Skin skin = folder.read(name);
        QVERIFY2(skin.drawn(), qPrintable(skin.reason));
        QCOMPARE(skin.name, name);
        QCOMPARE(skin.tokens.value(QStringLiteral("radius")).toInt(), 3);
    } else {
        QVERIFY(names().isEmpty());
        QCOMPARE(folder.read(name).reason, QStringLiteral("missing"));
    }
}

/// C37 U2: a faces/ folder beside skin.json is never loaded, and the reader says so.
void TestUserSkins::aFacesFolderIsDropped()
{
    put(QStringLiteral("warm"), R"({"version": 1, "faces": {"Plate": "console"}})");
    QVERIFY(QDir().mkpath(m_dir + QStringLiteral("/warm/faces")));
    QFile face(m_dir + QStringLiteral("/warm/faces/Plate.qml"));
    QVERIFY(face.open(QIODevice::WriteOnly));
    face.write("import QtQuick\nItem {}\n");
    face.close();

    const Skin skin = UserSkins(m_dir).read(QStringLiteral("warm"));
    QVERIFY(skin.drawn());
    QCOMPARE(skin.dropped, QStringList{QStringLiteral("faces/")});
    QCOMPARE(skin.drops.first().token, QStringLiteral("faces/"));
    QVERIFY(skin.drops.first().value.isNull());
    QCOMPARE(skin.drops.first().why,
             QStringLiteral("files in faces/ never load: a face names a built-in skin (C37 U2)"));
    // The faces token itself is read: it names a built-in.
    QCOMPARE(skin.tokens.value(QStringLiteral("faces")).toMap().value(QStringLiteral("Plate")),
             QVariant(QStringLiteral("console")));
}

void TestUserSkins::aNewerVersionIsNamed()
{
    put(QStringLiteral("warm"), R"({"version": 2, "radius": 4})");
    const QList<UserSkin> skins = UserSkins(m_dir).list();
    QCOMPARE(skins.size(), 1);
    QCOMPARE(skins.first().skin.reason, QStringLiteral("newer"));
    QCOMPARE(skins.first().skin.version, 2);
    QVERIFY(skins.first().skin.tokens.isEmpty());
    put(QStringLiteral("paper"), "{ not json");
    QCOMPARE(UserSkins(m_dir).read(QStringLiteral("paper")).reason, QStringLiteral("unparseable"));
}

/// A folder with no skin.json is listed, not drawn; a name with no folder is missing too.
void TestUserSkins::noSkinJsonIsMissing()
{
    put(QStringLiteral("warm"), QByteArray());
    const QList<UserSkin> skins = UserSkins(m_dir).list();
    QCOMPARE(skins.size(), 1);
    QCOMPARE(skins.first().name, QStringLiteral("warm"));
    QCOMPARE(skins.first().skin.reason, QStringLiteral("missing"));
    QCOMPARE(UserSkins(m_dir).read(QStringLiteral("gone")).reason, QStringLiteral("missing"));
    // No skins folder at all is an empty list.
    QVERIFY(UserSkins(m_tmp->filePath(QStringLiteral("nowhere"))).list().isEmpty());
}

/// C37 U1: the folder names the skin; the file's "skin" key says nothing.
void TestUserSkins::aUserFileSkinKeyIsIgnored()
{
    put(QStringLiteral("warm"), R"({"skin": "console", "version": 1})");
    const Skin skin = UserSkins(m_dir).read(QStringLiteral("warm"));
    QVERIFY(skin.drawn());
    QCOMPARE(skin.name, QStringLiteral("warm"));
    QVERIFY(skin.dropped.isEmpty());
    QCOMPARE(UserSkins(m_dir).list().first().skin.name, QStringLiteral("warm"));
}

std::unique_ptr<ThemeReader> TestUserSkins::reader(const QString &choice) const
{
    const ThemeReader::Paths paths{m_tmp->filePath(QStringLiteral("state/omarchy/current")),
                                   m_tmp->filePath(QStringLiteral("config/omarchy")),
                                   m_dir};
    auto reader = std::make_unique<ThemeReader>(paths);
    reader->setSkinChoice(choice);
    return reader;
}

void TestUserSkins::saveByRename(const QString &name, const QByteArray &json)
{
    const QString file = m_dir + QLatin1Char('/') + name + QStringLiteral("/skin.json");
    QFile temporary(file + QStringLiteral(".new"));
    QVERIFY(temporary.open(QIODevice::WriteOnly));
    temporary.write(json);
    temporary.close();
    // std::rename replaces the target, as an editor's save does; QFile::rename refuses to.
    QCOMPARE(std::rename(qPrintable(temporary.fileName()), qPrintable(file)), 0);
}

// No Hyprland: an empty hyprctl starts nothing (main.cpp's runThemeRoles does the same).
#define THEME(reader) Theme theme((reader).get(), Theme::Hyprland{QString(), QString()})

/// A save written in place reaches Theme's skin, with no restart.
void TestUserSkins::aSaveRepaints()
{
    put(QStringLiteral("warm"), R"({"version": 1, "description": "Warm."})");
    const auto watching = reader(QStringLiteral("warm"));
    THEME(watching);
    QCOMPARE(theme.skin()->name(), QStringLiteral("warm"));
    QCOMPARE(theme.skin()->description(), QStringLiteral("Warm."));
    QSignalSpy changed(&theme, &Theme::changed);
    put(QStringLiteral("warm"), R"({"version": 1, "description": "Warmer.", "radius": 6})");
    QTRY_COMPARE(theme.skin()->description(), QStringLiteral("Warmer."));
    QCOMPARE(theme.cornerRadius(), 6);
    QVERIFY(changed.count() >= 1);
}

/// Editors save by rename, which a watch on the file alone would lose: still repaints, and
/// keeps repainting after it.
void TestUserSkins::aRenameOverSaveRepaints()
{
    put(QStringLiteral("warm"), R"({"version": 1, "description": "One."})");
    const auto watching = reader(QStringLiteral("warm"));
    THEME(watching);
    saveByRename(QStringLiteral("warm"), R"({"version": 1, "description": "Two."})");
    QTRY_COMPARE(theme.skin()->description(), QStringLiteral("Two."));
    put(QStringLiteral("warm"), R"({"version": 1, "description": "Three."})");
    QTRY_COMPARE(theme.skin()->description(), QStringLiteral("Three."));
}

/// C37 U1: a save that breaks the file draws Omarchy's, with why; the next good one draws the
/// skin again (no "last good").
void TestUserSkins::aBrokenSaveDrawsOmarchyThenBack()
{
    put(QStringLiteral("warm"), R"({"version": 1, "radius": 5})");
    const auto watching = reader(QStringLiteral("warm"));
    THEME(watching);
    QCOMPARE(theme.skin()->name(), QStringLiteral("warm"));
    saveByRename(QStringLiteral("warm"), "{ \"version\": 1, ");
    QTRY_COMPARE(theme.skin()->reason(), QStringLiteral("unparseable"));
    QCOMPARE(theme.skin()->name(), QStringLiteral("omarchy"));
    QCOMPARE(theme.skin()->requested(), QStringLiteral("warm"));
    saveByRename(QStringLiteral("warm"), R"({"version": 1, "radius": 5})");
    QTRY_COMPARE(theme.skin()->name(), QStringLiteral("warm"));
    QVERIFY(theme.skin()->reason().isEmpty());
    QCOMPARE(theme.cornerRadius(), 5);
}

/// Two saves inside the debounce settle on the second.
void TestUserSkins::savedTwiceDrawsTheSecond()
{
    put(QStringLiteral("warm"), R"({"version": 1, "description": "First."})");
    const auto watching = reader(QStringLiteral("warm"));
    THEME(watching);
    QSignalSpy skinChanged(watching.get(), &ThemeReader::skinChanged);
    put(QStringLiteral("warm"), R"({"version": 1, "description": "Second."})");
    put(QStringLiteral("warm"), R"({"version": 1, "description": "Third."})");
    QTRY_COMPARE(theme.skin()->description(), QStringLiteral("Third."));
    QCOMPARE(skinChanged.count(), 1);
}

/// Compared by what reads: the same tokens written again, spaced otherwise, are silent.
void TestUserSkins::aRewriteThatReadsTheSameIsSilent()
{
    put(QStringLiteral("warm"), R"({"version": 1, "radius": 5})");
    const auto watching = reader(QStringLiteral("warm"));
    THEME(watching);
    QSignalSpy skinChanged(watching.get(), &ThemeReader::skinChanged);
    QSignalSpy listed(watching->userSkins(), &UserSkins::changed);
    QSignalSpy changed(&theme, &Theme::changed);
    saveByRename(QStringLiteral("warm"), "{\n  \"radius\": 5,\n  \"version\": 1\n}\n");
    QTest::qWait(3 * UserSkins::kDebounceMs);
    QCOMPARE(skinChanged.count(), 0);
    QCOMPARE(listed.count(), 0);
    QCOMPARE(changed.count(), 0);
}

/// A folder made while omaseed runs joins the list; skins/ itself may come later too.
void TestUserSkins::aNewFolderIsListed()
{
    QVERIFY(QDir(m_dir).removeRecursively());
    const auto watching = reader(QStringLiteral("omarchy"));
    UserSkins *folder = watching->userSkins();
    QVERIFY(folder);
    QVERIFY(folder->skins().isEmpty());
    QSignalSpy listed(folder, &UserSkins::changed);
    put(QStringLiteral("warm"), R"({"version": 1})");
    QTRY_COMPARE(folder->skins().size(), 1);
    QCOMPARE(folder->skins().first().name, QStringLiteral("warm"));
    put(QStringLiteral("paper"), R"({"version": 1, "description": "Paper."})");
    QTRY_COMPARE(folder->skins().size(), 2);
    QCOMPARE(folder->skins().first().name, QStringLiteral("paper"));
    QCOMPARE(folder->skins().first().description, QStringLiteral("Paper."));
    QVERIFY(listed.count() >= 2);
}

/// #287: a skin that casts another role as its accent repaints everything in the accent
/// (`Theme.accent` and each ground's accent ink: the selection mark, the progress bars, the
/// meter, links, the fix), not only its plates and edges; the theme role `accent` stays the
/// theme's own, for a skin that names it.
void TestUserSkins::theSkinsAccentIsTheAccent()
{
    put(QStringLiteral("warm"),
        R"({"version": 1, "roles": {"accent": "orange", "signal": "accent"}})");
    const auto plain = reader(QStringLiteral("omarchy"));
    Theme omarchy(plain.get(), Theme::Hyprland{QString(), QString()});
    QCOMPARE(omarchy.accent(), omarchy.themeAccent());
    QCOMPARE(omarchy.on()->window()->accent(), omarchy.themeAccent());

    const auto warmReader = reader(QStringLiteral("warm"));
    THEME(warmReader);
    QVERIFY(theme.orange() != theme.themeAccent());
    QCOMPARE(theme.accent(), theme.orange());
    QCOMPARE(theme.on()->window()->accent(), theme.orange());
    QCOMPARE(theme.skinColor(QStringLiteral("accent")), theme.orange());
    QCOMPARE(theme.skinColor(QStringLiteral("signal")), theme.themeAccent());
}

QTEST_MAIN(TestUserSkins)
#include "tst_userskins.moc"
