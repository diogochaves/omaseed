#include "integration/UserSkins.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using omaseed::integration::Skin;
using omaseed::integration::SkinReader;
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

private:
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

QTEST_MAIN(TestUserSkins)
#include "tst_userskins.moc"
