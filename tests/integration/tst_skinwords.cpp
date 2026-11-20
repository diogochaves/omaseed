// DECISIONS C37 U1, U3, U6: `skin list` and `skin clone`'s answers and the file a clone
// writes. Every folder is the test's own; the skins are invented.

#include "integration/SkinReader.hpp"
#include "integration/SkinWords.hpp"
#include "integration/UserSkins.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using omaseed::integration::Skin;
using omaseed::integration::SkinReader;
using omaseed::integration::UserSkins;
namespace SkinWords = omaseed::integration::SkinWords;

class TestSkinWords : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listShape();
    void cloneWritesEveryToken();
    void aCloneReadsBackAsItsSource();
    void aCloneOfOmarchyReadsAsOmarchy();
    void aCloneOfAUserSkinKeepsItsOwn();
    void cloneRefusesAnExistingName();
    void cloneRefusesABadName();
    void cloneOfAnUnreadableSkinIs1();
    void cloneOfAnUnknownSkin();

private:
    void put(const QString &name, const QByteArray &json);
    QByteArray file(const QString &name) const;
    /// Clones `from` as `name` and reads the file back as the app would.
    Skin cloned(const QString &from, const QString &name);
    /// `source`'s tokens with Omarchy's under them, as the clone must read.
    static QVariantMap expected(const Skin &source);

    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_dir;
    const QDateTime m_at = QDateTime(QDate(2026, 11, 20), QTime(12, 0), QTimeZone::UTC);
};

void TestSkinWords::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    m_dir = m_tmp->filePath(QStringLiteral("skins"));
    QVERIFY(QDir().mkpath(m_dir));
}

void TestSkinWords::cleanup()
{
    m_tmp.reset();
}

void TestSkinWords::put(const QString &name, const QByteArray &json)
{
    QVERIFY(QDir(m_dir).mkpath(name));
    QFile out(m_dir + QLatin1Char('/') + name + QStringLiteral("/skin.json"));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(json);
}

QByteArray TestSkinWords::file(const QString &name) const
{
    QFile in(m_dir + QLatin1Char('/') + name + QStringLiteral("/skin.json"));
    return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
}

Skin TestSkinWords::cloned(const QString &from, const QString &name)
{
    const SkinWords::Outcome outcome =
        SkinWords::clone(m_dir, from, name, m_at, QStringLiteral("9"));
    if (!outcome.code.isEmpty())
        qWarning() << outcome.code << outcome.message;
    return UserSkins::readFolder(m_dir + QLatin1Char('/') + name, name);
}

QVariantMap TestSkinWords::expected(const Skin &source)
{
    QVariantMap tokens = SkinReader::omarchyTokens();
    for (auto it = source.tokens.constBegin(); it != source.tokens.constEnd(); ++it) {
        if (it.value().typeId() != QMetaType::QVariantMap) {
            tokens.insert(it.key(), it.value());
            continue;
        }
        QVariantMap group = tokens.value(it.key()).toMap();
        const QVariantMap own = it.value().toMap();
        for (auto k = own.constBegin(); k != own.constEnd(); ++k)
            group.insert(k.key(), k.value());
        tokens.insert(it.key(), group);
    }
    tokens.insert(QStringLiteral("numerals"),
                  source.tokens.value(QStringLiteral("numerals"), QStringLiteral("plain")));
    tokens.insert(QStringLiteral("glow"), QStringLiteral("clean"));
    QVariantMap faces = source.tokens.value(QStringLiteral("faces")).toMap();
    for (const QString &seam : SkinReader::seams())
        if (!faces.contains(seam))
            faces.insert(seam, QStringLiteral("omarchy"));
    tokens.insert(QStringLiteral("faces"), faces);
    return tokens;
}

void TestSkinWords::listShape()
{
    put(QStringLiteral("warm"),
        R"({"version": 1, "description": "Warmer and rounder.",
            "cloned_from": {"skin": "console", "version": 1},
            "cut": {"panel": 99}, "line": {"hairAlpha": 2}})");
    put(QStringLiteral("paper"), "{ not json");
    put(QStringLiteral(".hidden"), R"({"version": 1})");
    const QJsonObject answer =
        QJsonDocument::fromJson(
            SkinWords::list(m_dir, QStringLiteral("warm"), QString(), m_at, QStringLiteral("9")))
            .object();
    QCOMPARE(answer.value(QLatin1String("v")).toInt(), 1);
    QCOMPARE(answer.value(QLatin1String("command")).toString(), QStringLiteral("skin list"));
    QCOMPARE(answer.value(QLatin1String("at")).toString(), QStringLiteral("2026-11-20T12:00:00Z"));
    QCOMPARE(answer.value(QLatin1String("omaseed")).toString(), QStringLiteral("9"));
    QCOMPARE(answer.value(QLatin1String("choice")).toString(), QStringLiteral("warm"));
    QVERIFY(answer.value(QLatin1String("pinned")).isNull());

    const QJsonArray skins = answer.value(QLatin1String("skins")).toArray();
    QStringList names;
    for (const QJsonValue &skin : skins)
        names << skin.toObject().value(QLatin1String("name")).toString();
    // Built-ins first, then the folder's, without case; the hidden one left out (C37 U1).
    QCOMPARE(names,
             (QStringList{QStringLiteral("omarchy"),
                          QStringLiteral("console"),
                          QStringLiteral("paper"),
                          QStringLiteral("warm")}));
    const QJsonObject console = skins.at(1).toObject();
    QCOMPARE(console.value(QLatin1String("builtIn")).toBool(), true);
    QVERIFY(console.value(QLatin1String("path")).isNull());
    QCOMPARE(console.value(QLatin1String("reads")).toBool(), true);
    QVERIFY(console.value(QLatin1String("reason")).isNull());
    QCOMPARE(console.value(QLatin1String("version")).toInt(), 1);
    QCOMPARE(console.value(QLatin1String("dropped")).toInt(), 0);
    const QJsonObject paper = skins.at(2).toObject();
    QCOMPARE(paper.value(QLatin1String("reads")).toBool(), false);
    QCOMPARE(paper.value(QLatin1String("reason")).toString(), QStringLiteral("unparseable"));
    const QJsonObject warm = skins.at(3).toObject();
    QCOMPARE(warm.value(QLatin1String("builtIn")).toBool(), false);
    QCOMPARE(warm.value(QLatin1String("path")).toString(), m_dir + QStringLiteral("/warm"));
    QCOMPARE(warm.value(QLatin1String("description")).toString(),
             QStringLiteral("Warmer and rounder."));
    QCOMPARE(warm.value(QLatin1String("clonedFrom")).toObject(),
             (QJsonObject{{QStringLiteral("skin"), QStringLiteral("console")},
                          {QStringLiteral("version"), 1}}));
    QCOMPARE(warm.value(QLatin1String("dropped")).toInt(), 2);

    // The lines: a star on the choice, home as ~, the reason or the drops.
    const QString lines = SkinWords::lines(answer, m_tmp->path());
    QCOMPARE(lines,
             QStringLiteral("  omarchy   built in\n"
                            "  console   built in\n"
                            "  paper     ~/skins/paper   doesn't read: unparseable\n"
                            "* warm      ~/skins/warm   Warmer and rounder.   2 dropped\n"));
}

void TestSkinWords::cloneWritesEveryToken()
{
    const SkinWords::Outcome outcome = SkinWords::clone(
        m_dir, QStringLiteral("console"), QStringLiteral("warm"), m_at, QStringLiteral("9"));
    QVERIFY2(outcome.code.isEmpty(), qPrintable(outcome.message));
    const QJsonObject answer = QJsonDocument::fromJson(outcome.json).object();
    QCOMPARE(answer.value(QLatin1String("command")).toString(), QStringLiteral("skin clone"));
    QCOMPARE(answer.value(QLatin1String("skin")).toString(), QStringLiteral("warm"));
    QCOMPARE(answer.value(QLatin1String("from")).toString(), QStringLiteral("console"));
    QCOMPARE(answer.value(QLatin1String("path")).toString(),
             m_dir + QStringLiteral("/warm/skin.json"));
    QCOMPARE(SkinWords::lines(answer, m_tmp->path()),
             QStringLiteral("warm: a copy of console in ~/skins/warm/skin.json. omaseed check "
                            "warm draws it.\n"));

    const QByteArray text = file(QStringLiteral("warm"));
    // The order the plan gives: $schema, version, cloned_from, then the tokens; two spaces.
    QVERIFY2(text.startsWith("{\n  \"$schema\": \"../../skin.schema.json\",\n  \"version\": 1,\n"
                             "  \"cloned_from\": { \"skin\": \"console\", \"version\": 1 },\n"
                             "  \"roles\": {\n    \"accent\": \"accent\",\n"),
             text.constData());
    QVERIFY(text.endsWith("}\n"));
    const QJsonObject root = QJsonDocument::fromJson(text).object();
    QVERIFY(!root.contains(QLatin1String("skin")));
    QCOMPARE(root.value(QLatin1String("grounds")).toObject().size(),
             SkinReader::groundNames().size());
    QCOMPARE(root.value(QLatin1String("roles")).toObject().size(), 4);
    QCOMPARE(root.value(QLatin1String("faces")).toObject().size(), SkinReader::seams().size());
    QCOMPARE(root.value(QLatin1String("density")).toObject().size(),
             SkinReader::omarchyTokens().value(QStringLiteral("density")).toMap().size());
    // Every Omarchy value is written out, the contextual ones only when the source sets them.
    const QVariantMap &omarchy = SkinReader::omarchyTokens();
    for (auto group = omarchy.constBegin(); group != omarchy.constEnd(); ++group)
        for (const QString &key : group.value().toMap().keys())
            QVERIFY2(root.value(group.key()).toObject().contains(key),
                     qPrintable(group.key() + QLatin1Char('.') + key));
    QVERIFY(!root.value(QLatin1String("label")).toObject().contains(QLatin1String("track")) ||
            SkinReader::builtIn(QStringLiteral("console"))
                .tokens.value(QStringLiteral("label"))
                .toMap()
                .contains(QStringLiteral("track")));
}

void TestSkinWords::aCloneReadsBackAsItsSource()
{
    const Skin source = SkinReader::builtIn(QStringLiteral("console"));
    const Skin copy = cloned(QStringLiteral("console"), QStringLiteral("warm"));
    QVERIFY2(copy.drawn(), qPrintable(copy.reason));
    QVERIFY2(copy.drops.isEmpty(), qPrintable(copy.dropped.join(QLatin1Char(' '))));
    QCOMPARE(copy.name, QStringLiteral("warm"));
    QCOMPARE(copy.clonedFrom.skin, QStringLiteral("console"));
    QCOMPARE(copy.clonedFrom.version, 1);
    QCOMPARE(copy.roles, source.roles);
    QCOMPARE(SkinReader::groundsOf(copy), SkinReader::groundsOf(source));
    QCOMPARE(copy.tokens, expected(source));
    QCOMPARE(SkinReader::fontFamilies(copy), SkinReader::fontFamilies(source));
}

void TestSkinWords::aCloneOfOmarchyReadsAsOmarchy()
{
    const Skin copy = cloned(QStringLiteral("omarchy"), QStringLiteral("plain"));
    QVERIFY2(copy.drawn(), qPrintable(copy.reason));
    QVERIFY(copy.drops.isEmpty());
    QCOMPARE(copy.roles, SkinReader::omarchyRoles());
    QCOMPARE(SkinReader::groundsOf(copy), SkinReader::omarchyGrounds());
    // Every value it wrote is Omarchy's: the table, plain numerals, clean glow, Omarchy's faces.
    const QVariantMap &omarchy = SkinReader::omarchyTokens();
    for (auto group = omarchy.constBegin(); group != omarchy.constEnd(); ++group)
        QCOMPARE(copy.tokens.value(group.key()).toMap(), group.value().toMap());
    QCOMPARE(copy.tokens.value(QStringLiteral("numerals")).toString(), QStringLiteral("plain"));
    QCOMPARE(copy.tokens.value(QStringLiteral("glow")).toString(), QStringLiteral("clean"));
    for (const QString &seam : SkinReader::seams())
        QCOMPARE(copy.tokens.value(QStringLiteral("faces")).toMap().value(seam).toString(),
                 QStringLiteral("omarchy"));
    QVERIFY(!copy.tokens.contains(QStringLiteral("glyphs")));
    QVERIFY(!copy.tokens.contains(QStringLiteral("labels")));
    QVERIFY(!copy.tokens.contains(QStringLiteral("radius")));
    // The contextual ones are left out (#283), so the place keeps its own.
    for (const QString &path : SkinReader::contextualTokens()) {
        const QString group = path.section(QLatin1Char('.'), 0, 0);
        QVERIFY2(!copy.tokens.value(group).toMap().contains(path.section(QLatin1Char('.'), 1)),
                 qPrintable(path));
    }
}

void TestSkinWords::aCloneOfAUserSkinKeepsItsOwn()
{
    put(QStringLiteral("warm"),
        R"({"version": 1, "description": "Warmer.", "cut": {"panel": 12},
            "glyphs": {"add": "ms:add_circle"}, "labels": {"library": "Shelf"},
            "line": {"hair": 99}})");
    const Skin source =
        UserSkins::readFolder(m_dir + QStringLiteral("/warm"), QStringLiteral("warm"));
    const Skin copy = cloned(QStringLiteral("warm"), QStringLiteral("warm.v2"));
    QVERIFY2(copy.drawn(), qPrintable(copy.reason));
    QVERIFY(copy.drops.isEmpty()); // the source's dropped hair is written as Omarchy's
    QCOMPARE(copy.description, QStringLiteral("Warmer."));
    QCOMPARE(copy.clonedFrom.skin, QStringLiteral("warm"));
    QCOMPARE(copy.tokens, expected(source));
    QCOMPARE(
        copy.tokens.value(QStringLiteral("line")).toMap().value(QStringLiteral("hair")).toInt(), 1);
}

void TestSkinWords::cloneRefusesAnExistingName()
{
    put(QStringLiteral("warm"), "{ my own, half written");
    const QByteArray before = file(QStringLiteral("warm"));
    for (const char *name : {"warm", "WARM"}) {
        const SkinWords::Outcome outcome = SkinWords::clone(
            m_dir, QStringLiteral("console"), QString::fromLatin1(name), m_at, QStringLiteral("9"));
        QCOMPARE(outcome.code, QStringLiteral("skin-exists"));
        QCOMPARE(outcome.message,
                 QStringLiteral("There's already a skin called %1, in %2. Pick another name, or "
                                "remove that folder first.")
                     .arg(QString::fromLatin1(name), m_dir + QStringLiteral("/warm")));
        QVERIFY(outcome.json.isEmpty());
    }
    QCOMPARE(file(QStringLiteral("warm")), before);
    QCOMPARE(QDir(m_dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("warm")});
    QCOMPARE(
        QDir(m_dir + QStringLiteral("/warm")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
        QStringList{QStringLiteral("skin.json")});
}

void TestSkinWords::cloneRefusesABadName()
{
    for (const char *name : {".warm", "warm skin", "warm.", "Console", "desktop", "a/b"}) {
        const SkinWords::Outcome outcome = SkinWords::clone(
            m_dir, QStringLiteral("console"), QString::fromLatin1(name), m_at, QStringLiteral("9"));
        QCOMPARE(outcome.code, QStringLiteral("bad-name"));
        QVERIFY2(outcome.message.startsWith(
                     QStringLiteral("%1 can't be a skin's name: ").arg(QString::fromLatin1(name))),
                 qPrintable(outcome.message));
    }
    QVERIFY(QDir(m_dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

void TestSkinWords::cloneOfAnUnreadableSkinIs1()
{
    put(QStringLiteral("paper"), R"({"version": 2})");
    const SkinWords::Outcome outcome = SkinWords::clone(
        m_dir, QStringLiteral("paper"), QStringLiteral("warm"), m_at, QStringLiteral("9"));
    QCOMPARE(outcome.code, QStringLiteral("unreadable"));
    QCOMPARE(outcome.message,
             QStringLiteral("paper doesn't read (newer): omaseed check paper says why."));
    QVERIFY(!QFileInfo::exists(m_dir + QStringLiteral("/warm")));
}

void TestSkinWords::cloneOfAnUnknownSkin()
{
    const SkinWords::Outcome outcome = SkinWords::clone(
        m_dir, QStringLiteral("neon"), QStringLiteral("warm"), m_at, QStringLiteral("9"));
    QCOMPARE(outcome.code, QStringLiteral("unknown-skin"));
    QCOMPARE(outcome.message, QStringLiteral("No skin called neon. omaseed skin list lists them."));
    QVERIFY(!QFileInfo::exists(m_dir + QStringLiteral("/warm")));
}

QTEST_GUILESS_MAIN(TestSkinWords)
#include "tst_skinwords.moc"
